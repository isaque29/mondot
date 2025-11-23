#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <chrono>

#include "util.h"
#include "fileutil.h"
#include "parser.h"
#include "bytecode.h"
#include "module.h"
#include "vm.h"
#include "host.h"

using namespace std;
namespace fs = std::filesystem;

int main(int argc, char **argv)
{
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    enable_terminal_colors();

    if(argc < 2)
    {
        cout << "Usage: mondot <scripts-dir>\n";
        return 1;
    }

    // register useful host functions
    GLOBAL_HOST.register_function("Print", [](const std::vector<Value> &args)->Value
    {
        if(!args.empty()) std::cout << value_to_string(args[0]) << std::endl;
        else std::cout << "nil" << std::endl;
        return Value::make_nil();
    });

    GLOBAL_HOST.register_function("Console.Write", [](const std::vector<Value> &args)->Value
    {
        if(!args.empty()) std::cout << value_to_string(args[0]);
        return Value::make_nil();
    });

    GLOBAL_HOST.register_function("strlen", [](const std::vector<Value> &args)->Value
    {
        if(!args.empty() && args[0].tag == Tag::String)
            return Value::make_number((double)args[0].s->size());
        return Value::make_number(0.0);
    });

    GLOBAL_HOST.register_function("str_char_at", [](const std::vector<Value> &args)->Value
    {
        if(args.size()>=2 && args[0].tag==Tag::String && args[1].tag==Tag::Number)
        {
            int idx = (int)args[1].num;
            if(idx >= 0 && idx < (int)args[0].s->size()) {
                std::string r(1, (*args[0].s)[idx]);
                return Value::make_string(r);
            }
        }
        return Value::make_string("");
    });

    GLOBAL_HOST.register_function("add", [](const std::vector<Value> &args)->Value
    {
        if(args.size()>=2 && args[0].tag==Tag::Number && args[1].tag==Tag::Number)
            return Value::make_number(args[0].num + args[1].num);
        return Value::make_number(0.0);
    });

    GLOBAL_HOST.register_function("sub", [](const std::vector<Value> &args)->Value
    {
        if(args.size()>=2 && args[0].tag==Tag::Number && args[1].tag==Tag::Number)
            return Value::make_number(args[0].num - args[1].num);
        return Value::make_number(0.0);
    });

    GLOBAL_HOST.register_function("lt", [](const std::vector<Value> &args)->Value
    {
        if(args.size()>=2 && args[0].tag==Tag::Number && args[1].tag==Tag::Number)
            return Value::make_number(args[0].num < args[1].num ? 1.0 : 0.0);
        return Value::make_number(0.0);
    });

    // VM
    VM vm(GLOBAL_HOST);

    string scripts_dir = argv[1];

    vector<ScriptFile> scripts;
    for(auto &p : fs::directory_iterator(scripts_dir))
    {
        if(p.is_regular_file())
        {
            string ext = p.path().extension().string();
            if(ext==".mdot" || ext==".mondot" || ext==".mon")
                scripts.push_back({p.path().string(), p.last_write_time()});
        }
    }

    // initial compile & load
    for(auto &sf : scripts)
    {
        try
        {
            string source = slurp_file(sf.path);
            Parser parser(source);
            auto prog = parser.parse_program();
            if(prog->units.empty()) continue;

            for(auto &u : prog->units)
            {
                CompiledUnit cu = compile_unit(u.get());
                Module *m = module_from_compiled(cu);
                G_MODULES.hot_swap(m);

                // call MdInit once per module (first time loaded)
                {
                    lock_guard<mutex> lk(G_MODULES.modules_mtx);
                    if(!m->mdinit_called && m->bytecode.handler_index.count("MdInit"))
                    {
                        vm.execute_handler(m, "MdInit");
                        m->mdinit_called = true;
                    }
                }
                if(m->bytecode.handler_index.count("MdSuperInit"))
                {
                    if(!super_called.test_and_set())
                    {
                        info("Calling MdSuperInit from module " + m->name);
                        vm.execute_handler(m, "MdSuperInit");
                    }
                }
            }
        }
        catch(exception &e)
        {
            errlog(string("compile error for ") + sf.path + ": " + e.what());
        }
    }

    atomic<bool> stop {false};
    thread watcher([&]
    {
        while(!stop.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            for(auto &p : fs::directory_iterator(scripts_dir))
            {
                if(!p.is_regular_file()) continue;

                string ext = p.path().extension().string();
                if(!(ext==".mdot"||ext==".mondot"||ext==".mon")) continue;

                string path = p.path().string();
                auto last = p.last_write_time();
                bool known=false;
                for(auto &sf : scripts)
                {
                    if(sf.path==path)
                    {
                        known=true;
                        if(last != sf.last_write)
                        {
                            dbg("Detected change in " + path);
                            sf.last_write = last;
                            try
                            {
                                string source = slurp_file(path);
                                Parser parser(source);
                                auto prog = parser.parse_program();
                                for(auto &u : prog->units)
                                {
                                    CompiledUnit cu = compile_unit(u.get());
                                    Module *m = module_from_compiled(cu);

                                    // hot-swap existing module with same name
                                    G_MODULES.hot_swap(m);

                                    // call MdReload if present
                                    if(m->bytecode.handler_index.count("MdReload"))
                                    {
                                        info("Calling MdReload for module " + m->name);
                                        vm.execute_handler(m, "MdReload");
                                    }
                                }
                            }
                            catch(exception &e)
                            {
                                errlog(string("reload/compile error for ") + path + ": " + e.what());
                            }
                        }
                    }
                }
                if(!known)
                {
                    dbg("New script discovered: " + path);
                    scripts.push_back({path, last});
                    try
                    {
                        string source = slurp_file(path);
                        Parser parser(source);
                        auto prog = parser.parse_program();
                        for(auto &u : prog->units)
                        {
                            CompiledUnit cu = compile_unit(u.get());
                            Module *m = module_from_compiled(cu);
                            G_MODULES.hot_swap(m);

                            // call MdInit
                            {
                                lock_guard<mutex> lk(G_MODULES.modules_mtx);
                                if(!m->mdinit_called && m->bytecode.handler_index.count("MdInit"))
                                {
                                    vm.execute_handler(m, "MdInit");
                                    m->mdinit_called = true;
                                }
                            }
                            if(m->bytecode.handler_index.count("MdSuperInit"))
                            {
                                if(!super_called.test_and_set())
                                {
                                    info("Calling MdSuperInit from module " + m->name);
                                    vm.execute_handler(m, "MdSuperInit");
                                }
                            }
                        }
                    }
                    catch(exception &e)
                    {
                        errlog(string("compile error for new file ") + path + ": " + e.what());
                    }
                }
            }
            G_MODULES.tick_reclaim();
        }
    });

    info("MonDot runtime watching " + scripts_dir + " - press Enter to exit");
    string dummy; getline(cin, dummy);
    stop.store(true);
    watcher.join();
    info("Exiting MonDot runtime");
    return 0;
}
