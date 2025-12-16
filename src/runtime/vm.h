#ifndef MONDOT_VM_H
#define MONDOT_VM_H

#include "host.h"
#include "bytecode.h"
#include <string>
#include <vector>
#include "module.h"

struct Frame
{
    Module *module = nullptr;
    ByteFunc *func = nullptr;
    std::vector<Value> locals;
    size_t ip = 0;
};

struct VM
{
    HostBridge &host;
    VM(HostBridge &h);
    Value execute_handler(Module* m, const std::string &handler_name);
    Value execute_handler_idx(Module* m, int idx);
    Value run_frame(Frame &fr);
private:
    std::vector<Value> eval_stack;
    std::vector<Value> arg_scratch;
    Value call_bytecode_function(Module* m, int func_idx, const std::vector<Value> &args);
};

#endif
