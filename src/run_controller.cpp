#include "run_controller.h"
#include <filesystem>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <type_traits>

#include "util.h"
#include "fileutil.h"
#include "parser.h"
#include "runtime/host.h"
#include "runtime/bytecode.h"
#include "runtime/module.h"
#include "runtime/vm.h"
#include "runtime/host_core_funcs.h"

using namespace std;
namespace fs = std::filesystem;

RunController::RunController(VM &vm_, const string &scripts_dir_, int argc, char **argv)
: vm(vm_), scripts_dir(scripts_dir_)
{
    parse_args(argc, argv);
}

RunController::~RunController()
{
    stop_flag.store(true);
    if (watcher_thread.joinable()) watcher_thread.join();
}

void RunController::parse_args(int argc, char **argv)
{
    for (int i = 2; i < argc; ++i)
    {
        string a = argv[i];
        if (a == "--test") mode = Mode::Test;
        else if (a == "--benchmark") mode = Mode::Benchmark;
        else if (a == "--production") mode = Mode::Production;
        else
            dbg("Unknown argument: " + a);
    }
}

bool RunController::is_script_ext(const fs::path &p)
{
    auto e = p.extension().string();
    return e==".mdot" || e==".mondot" || e==".mon";
}

void RunController::initial_scan_and_load()
{
    scripts_map.reserve(256);

    for (auto &ent : fs::recursive_directory_iterator(scripts_dir))
    {
        if (!ent.is_regular_file()) continue;
        if (!is_script_ext(ent.path())) continue;
        try
        {
            auto ft = fs::last_write_time(ent.path());
            scripts_map.emplace(ent.path().string(), ScriptFile{ent.path().string(), ft});
        }
        catch (const std::exception &e)
        {
            dbg("initial_scan: cannot stat " + ent.path().string() + " -> " + e.what());
        }
    }

    for (auto &kv : scripts_map)
        compile_and_register(fs::path(kv.first), true);
}

static std::string value_debug(const Value &v)
{
    switch (v.tag)
    {
        case Tag::Nil: return "nil";
        case Tag::Boolean: return v.boolean ? "true" : "false";
        case Tag::Number: return std::to_string(v.num);
        case Tag::String: return "\"" + *v.s + "\"";
        case Tag::Rule: return "<rule>";
    }
    return "<unknown>";
}

void RunController::compile_and_register(const fs::path &path, bool is_new)
{
    try
    {
        string src = slurp_file(path.string());
        Parser parser(std::move(src));
        auto prog = parser.parse_program();
#ifdef MONDOT_DEBUG
        dump_program_tokens(prog.get());
#endif
        if (prog->units.empty()) return;

        for (auto &u : prog->units)
        {
            CompiledUnit cu = compile_unit(u.get());
            Module *m = module_from_compiled(cu);
#ifdef MONDOT_DEBUG
            dump_module_bytecode(m);
#endif
            G_MODULES.hot_swap(m);

            {
                lock_guard<mutex> lk(G_MODULES.modules_mtx);
                if (!m->mdinit_called && m->bytecode.handler_index.count("MdInit"))
                {
                    vm.execute_handler(m, "MdInit");
                    m->mdinit_called = true;
                }
            }
            if (m->bytecode.handler_index.count("MdSuperInit"))
            {
                if (!super_called.test_and_set())
                {
                    info("Calling MdSuperInit from module " + m->name);
                    vm.execute_handler(m, "MdSuperInit");
                }
            }

            if (!is_new && m->bytecode.handler_index.count("MdReload"))
            {
                info("Calling MdReload for module " + m->name);
                vm.execute_handler(m, "MdReload");
            }
        }
    }
    catch (const std::exception &e)
    {
        errlog(string("compile error for ") + path.string() + ": " + e.what());
    }
    catch (...)
    {
        errlog(string("unknown compile error for ") + path.string());
    }
}

void RunController::start_watcher()
{
    stop_flag.store(false);
    watcher_thread = thread([this]{ watcher_loop(); });
}

void RunController::watcher_loop()
{
    using namespace std::chrono_literals;
    while (!stop_flag.load())
    {
        std::this_thread::sleep_for(400ms);

        for (auto &p : fs::recursive_directory_iterator(scripts_dir))
        {
            if (!p.is_regular_file()) continue;
            if (!is_script_ext(p.path())) continue;

            string path = p.path().string();
            std::filesystem::file_time_type ft;
            try
            {
                ft = fs::last_write_time(p.path());
            }
            catch (...)
            {
                continue;
            }

            auto it = scripts_map.find(path);
            if (it == scripts_map.end())
            {
                dbg("New script discovered: " + path);
                scripts_map.emplace(path, ScriptFile{path, ft});
                compile_and_register(p.path(), true);
            }
            else
            {
                if (ft != it->second.last_write) {
                    dbg("Detected change in " + path);
                    it->second.last_write = ft;
                    compile_and_register(p.path(), false);
                }
            }
        }

        vector<string> removed;
        removed.reserve(8);
        for (auto &kv : scripts_map)
            if (!fs::exists(kv.second.path))
                removed.push_back(kv.first);
        for (auto &k : removed)
        {
            dbg("Script removed: " + k);
            scripts_map.erase(k);
        }

        if (call_finalize_all())
        {
            info("Finalize requested stop. Stopping watcher.");
            stop_flag.store(true);
            break;
        }

        G_MODULES.tick_reclaim();
    }
}

bool RunController::call_handler_bool(Module *m, const string &handler_name)
{
    if (!m->bytecode.handler_index.count(handler_name)) return false;
    try
    {
        Value ret = vm.execute_handler(m, handler_name);

        if (ret.tag == Tag::Boolean) return ret.boolean;
        if (ret.tag == Tag::Number) return ret.num != 0.0;
        if (ret.tag == Tag::Nil) return false;
        return false;
    }
    catch (const std::exception &e)
    {
        errlog(string("handler ") + handler_name + " threw: " + e.what());
        return false;
    }
    catch (...)
    {
        errlog(string("handler ") + handler_name + " threw unknown exception");
        return false;
    }
}

void RunController::call_handler_void(Module *m, const string &handler_name)
{
    if (!m->bytecode.handler_index.count(handler_name)) return;
    try
    {
        vm.execute_handler(m, handler_name);
    }
    catch (const std::exception &e)
    {
        errlog(string("handler ") + handler_name + " threw: " + e.what());
    }
    catch (...)
    {
        errlog(string("handler ") + handler_name + " threw unknown exception");
    }
}

bool RunController::call_finalize_all()
{
    bool any_requested_stop = false;
    vector<Module*> mods;
    {
        lock_guard<mutex> lk(G_MODULES.modules_mtx);
        mods.reserve(G_MODULES.modules.size());
        for (auto &kv : G_MODULES.modules) mods.push_back(kv.second);
    }
    for (auto *m : mods)
    {
        if (m->bytecode.handler_index.count("Finalize"))
        {
            bool stop_requested = call_handler_bool(m, "Finalize");
            if (stop_requested) any_requested_stop = true;
        }
    }
    return any_requested_stop;
}

int RunController::run_tests()
{
    size_t total = 0, succeeded = 0, failed = 0;
    vector<Module*> mods;
    {
        lock_guard<mutex> lk(G_MODULES.modules_mtx);
        for (auto &kv : G_MODULES.modules) mods.push_back(kv.second);
    }
    for (auto *m : mods)
    {
        if (m->bytecode.handler_index.count("UTest"))
        {
            ++total;
            bool ok = call_handler_bool(m, "UTest");
            if (!ok)
            {
                Value raw = vm.execute_handler(m, "UTest");
                errlog(
                    "[UTest FAILED] module=" + m->name +
                    " expected=true got=" + value_debug(raw)
                );
                ++failed;
            }
            else ++succeeded;
        }
    }
    cout << "UTest: total=" << total << " succeeded=" << succeeded << " failed=" << failed << "\n";
    return (failed==0) ? 0 : 2;
}

int RunController::run_benchmarks()
{
    struct Result { string module; double ms; };
    vector<Result> results;
    vector<Module*> mods;
    {
        lock_guard<mutex> lk(G_MODULES.modules_mtx);
        for (auto &kv : G_MODULES.modules) mods.push_back(kv.second);
    }
    for (auto *m : mods)
    {
        if (m->bytecode.handler_index.count("UBenchmark"))
        {
            auto t0 = chrono::steady_clock::now();
            call_handler_void(m, "UBenchmark");
            auto t1 = chrono::steady_clock::now();
            double ms = chrono::duration<double, milli>(t1 - t0).count();
            results.push_back({m->name, ms});
        }
    }
    cout << "Benchmarks:\n";
    for (auto &r : results)
        cout << "  " << r.module << ": " << fixed << setprecision(3) << r.ms << " ms\n";
    return 0;
}

int RunController::run_production()
{
    initial_scan_and_load();
    call_finalize_all();
    return 0;
}

void RunController::record_new_script(const fs::path &p)
{
    try
    {
        auto ft = fs::last_write_time(p);
        scripts_map.emplace(p.string(), ScriptFile{p.string(), ft});
    }
    catch (...) {}
}

int RunController::run()
{
    initial_scan_and_load();

    switch (mode)
    {
        case Mode::Watch:
            start_watcher();
            break;
        case Mode::Test:
            return run_tests();
        case Mode::Benchmark:
            return run_benchmarks();
        case Mode::Production:
            return run_production();
    }

    info("MonDot runtime watching " + scripts_dir + " - press Enter to exit");
    string dummy;
    getline(cin, dummy);
    stop_flag.store(true);
    if (watcher_thread.joinable()) watcher_thread.join();

    call_finalize_all();

    info("Exiting MonDot runtime");
    return 0;
}
