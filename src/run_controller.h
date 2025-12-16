#pragma once
#include <string>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include "runtime/module.h"
#include "runtime/vm.h"
#include "fileutil.h"

class RunController
{
public:
    enum class Mode { Watch, Test, Benchmark, Production };

    RunController(VM &vm, const std::string &scripts_dir, int argc, char **argv);
    ~RunController();

    int run();

private:
    VM &vm;
    std::string scripts_dir;
    Mode mode = Mode::Watch;

    std::unordered_map<std::string, ScriptFile> scripts_map;

    std::atomic<bool> stop_flag{false};
    std::thread watcher_thread;

    void parse_args(int argc, char **argv);
    static bool is_script_ext(const std::filesystem::path &p);

    void initial_scan_and_load();
    void compile_and_register(const std::filesystem::path &path, bool is_new);

    void start_watcher();
    void watcher_loop();

    int run_tests();
    int run_benchmarks();
    int run_production();

    bool call_handler_bool(Module *m, const std::string &handler_name);
    void call_handler_void(Module *m, const std::string &handler_name);

    bool call_finalize_all();

    void record_new_script(const std::filesystem::path &p);
};
