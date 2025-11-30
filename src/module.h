#ifndef MONDOT_MODULE_H
#define MONDOT_MODULE_H

#include "bytecode.h"
#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

struct Module
{
    std::string name;
    ByteModule bytecode;
    std::atomic<int> active_calls{0};
    bool mdinit_called = false;
    Module() = default;
};

struct ModuleManager
{
    std::unordered_map<std::string, Module*> modules;
    std::mutex modules_mtx;

    std::mutex reclaim_mtx;
    std::vector<Module*> pending_reclaim;

    Module* get_module(const std::string &name);
    void hot_swap(Module* newm);
    void tick_reclaim();
};

extern ModuleManager G_MODULES;
extern std::atomic_flag super_called;

Module* module_from_compiled(const CompiledUnit &cu);

#endif
