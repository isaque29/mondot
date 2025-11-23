#ifndef MONDOT_VM_H
#define MONDOT_VM_H

#include "host.h"
#include "bytecode.h"
#include <string>
#include <vector>
#include "module.h"

struct Frame {
    Module *module = nullptr;
    ByteFunc *func = nullptr;
    std::vector<Value> locals;
    size_t ip = 0;
};

struct VM {
    HostBridge &host;
    VM(HostBridge &h);
    void execute_handler(Module* m, const std::string &handler_name);
    void execute_handler_idx(Module* m, int idx);
private:
    Value call_bytecode_function(Module* m, int func_idx, const std::vector<Value> &args);
    Value load_global(Module* m, const std::string &name);
};

#endif
