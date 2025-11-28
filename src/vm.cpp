#include "vm.h"
#include "util.h"
#include <iostream>
#include <stdexcept>

using namespace std;

struct ActiveCallGuard {
    Module *m;
    ActiveCallGuard(Module *mm): m(mm) { if(m) m->active_calls.fetch_add(1); }
    ~ActiveCallGuard() { if(m) m->active_calls.fetch_sub(1); }
};

static inline bool valid_const_idx(const ByteFunc &f, int idx) {
    return idx >= 0 && static_cast<size_t>(idx) < f.consts.size();
}
static inline bool valid_local_idx(const Frame &frame, int idx) {
    return idx >= 0 && static_cast<size_t>(idx) < frame.locals.size();
}

VM::VM(HostBridge &h): host(h) {}

void VM::execute_handler(Module* m, const string &handler_name) {
    if(!m) return;
    auto it = m->bytecode.handler_index.find(handler_name);
    if(it==m->bytecode.handler_index.end()) { dbg("handler not found: " + handler_name + " in module " + m->name); return; }
    int idx = it->second;
    execute_handler_idx(m, idx);
}

Value VM::call_bytecode_function(Module* m, int func_idx, const vector<Value> &args) {
    if(!m) return Value::make_nil();
    if(func_idx < 0 || func_idx >= (int)m->bytecode.funcs.size()) { dbg("call_bytecode_function: invalid idx"); return Value::make_nil(); }
    ByteFunc &f = m->bytecode.funcs[func_idx];
    Frame frame;
    frame.module = m;
    frame.func = &f;
    frame.locals.resize(f.locals.size());
    // copy args into first locals
    for(size_t i=0;i<args.size() && i<frame.locals.size(); ++i) frame.locals[i] = args[i];

    ActiveCallGuard guard(m);
    // simple interpreter loop for this function
    vector<Value> eval_stack;
    for(size_t ip=0; ip < f.code.size(); ++ip) {
        Op &op = f.code[ip];
        switch(op.op)
        {
            case OP_PUSH_CONST:
                if(valid_const_idx(f, op.a)) eval_stack.push_back(f.consts[op.a]);
                else eval_stack.push_back(Value::make_nil());
                break;
            case OP_PUSH_LOCAL:
                if(valid_local_idx(frame, op.a)) eval_stack.push_back(frame.locals[op.a]);
                else eval_stack.push_back(Value::make_nil());
                break;
            case OP_PUSH_GLOBAL:
                eval_stack.push_back(load_global(m, op.s));
                break;
            case OP_STORE_LOCAL:
                if(eval_stack.empty()) { errlog("STORE_LOCAL: stack underflow"); break; }
                {
                    Value v = eval_stack.back(); eval_stack.pop_back();
                    if(valid_local_idx(frame, op.a)) frame.locals[op.a] = v; else dbg("STORE_LOCAL: invalid local");
                }
                break;
            case OP_POP:
                for(int pi=0; pi<op.a && !eval_stack.empty(); ++pi) eval_stack.pop_back();
                break;
            case OP_CALL: {
                int nargs = op.a;
                if((int)eval_stack.size() < nargs + (op.b==-2 ? 1 : 0)) { errlog("CALL: stack underflow"); break; }
                vector<Value> argsv(nargs);
                // when op.b == -2, callee is on top after args were pushed last -> pop callee then pop nargs
                if(op.b == -2)
                {
                    Value callee = eval_stack.back(); eval_stack.pop_back();
                    for(int i=nargs-1;i>=0;--i){ argsv[i] = eval_stack.back(); eval_stack.pop_back(); }
                    // callee should be a number with function index
                    if(callee.tag == Tag::Number) {
                        int fidx = (int)callee.num;
                        Value ret = call_bytecode_function(m, fidx, argsv);
                        eval_stack.push_back(ret);
                    } else {
                        errlog("CALL dynamic: callee not function index");
                        eval_stack.push_back(Value::make_nil());
                    }
                } else if(op.b >= 0) {
                    for(int i=nargs-1;i>=0;--i){ argsv[i] = eval_stack.back(); eval_stack.pop_back(); }
                    Value ret = call_bytecode_function(m, op.b, argsv);
                    eval_stack.push_back(ret);
                } else if(op.b == -1) {
                    for(int i=nargs-1;i>=0;--i){ argsv[i] = eval_stack.back(); eval_stack.pop_back(); }
                    // resolve host/global function by name s
                    if(host.has_function(op.s)) {
                        Value ret = host.call_function(op.s, argsv);
                        eval_stack.push_back(ret);
                    } else {
                        // unknown -> nil
                        errlog("CALL: unknown function " + op.s);
                        eval_stack.push_back(Value::make_nil());
                    }
                } else {
                    errlog("CALL: unsupported mode");
                }
                break;
            }
            case OP_JMP:
                ip = (size_t)op.a - 1; // -1 because loop will ++
                break;
            case OP_JMP_IF_FALSE: {
                if(eval_stack.empty()) { errlog("JMP_IF_FALSE: stack underflow"); break; }
                Value cond = eval_stack.back(); eval_stack.pop_back();
                bool truth = true;
                if(cond.tag == Tag::Nil) truth = false;
                else if(cond.tag == Tag::Number && cond.num == 0.0) truth = false;
                if(!truth) ip = (size_t)op.a - 1;
                break;
            }
            case OP_RET:
                if(!eval_stack.empty()) return eval_stack.back();
                return Value::make_nil();
            default:
                dbg("call_bytecode_function: unknown opcode");
                break;
        }
    }
    return Value::make_nil();
}

void VM::execute_handler_idx(Module* m, int idx) {
    if(!m) return;
    if(idx < 0 || idx >= (int)m->bytecode.funcs.size()) { dbg("execute_handler_idx: invalid idx"); return; }
    ByteFunc &f = m->bytecode.funcs[idx];
    Frame frame;
    frame.module = m;
    frame.func = &f;
    frame.locals.resize(f.locals.size());

    ActiveCallGuard guard(m);

    vector<Value> eval_stack;
    for(size_t ip=0; ip < f.code.size(); ++ip) {
        Op &op = f.code[ip];
        switch(op.op) {
            case OP_PUSH_CONST:
                if(valid_const_idx(f, op.a)) eval_stack.push_back(f.consts[op.a]);
                else eval_stack.push_back(Value::make_nil());
                break;
            case OP_PUSH_LOCAL:
                if(valid_local_idx(frame, op.a)) eval_stack.push_back(frame.locals[op.a]);
                else eval_stack.push_back(Value::make_nil());
                break;
            case OP_PUSH_GLOBAL:
                eval_stack.push_back(load_global(m, op.s));
                break;
            case OP_STORE_LOCAL:
                if(eval_stack.empty())
                {
                    errlog("STORE_LOCAL: underflow");
                    break;
                }
                {
                    Value v = eval_stack.back(); eval_stack.pop_back();
                    if(valid_local_idx(frame, op.a)) frame.locals[op.a] = v; else dbg("STORE_LOCAL invalid");
                }
                break;
            case OP_POP:
                for(int k=0;k<op.a && !eval_stack.empty(); ++k) eval_stack.pop_back();
                break;
            case OP_CALL: {
                int nargs = op.a;
                if((int)eval_stack.size() < nargs + (op.b==-2 ? 1 : 0)) { errlog("CALL: stack underflow"); break; }
                vector<Value> argsv(nargs);
                if(op.b == -2) {
                    Value callee = eval_stack.back(); eval_stack.pop_back();
                    for(int i=nargs-1;i>=0;--i){ argsv[i] = eval_stack.back(); eval_stack.pop_back(); }
                    if(callee.tag == Tag::Number) {
                        int fidx = (int)callee.num;
                        Value ret = call_bytecode_function(m, fidx, argsv);
                        eval_stack.push_back(ret);
                    } else {
                        errlog("CALL dynamic: callee not function index");
                        eval_stack.push_back(Value::make_nil());
                    }
                } else if(op.b >= 0) {
                    for(int i=nargs-1;i>=0;--i){ argsv[i] = eval_stack.back(); eval_stack.pop_back(); }
                    Value ret = call_bytecode_function(m, op.b, argsv);
                    eval_stack.push_back(ret);
                } else if(op.b == -1) {
                    for(int i=nargs-1;i>=0;--i){ argsv[i] = eval_stack.back(); eval_stack.pop_back(); }
                    if(host.has_function(op.s)) {
                        Value ret = host.call_function(op.s, argsv);
                        eval_stack.push_back(ret);
                    } else {
                        errlog("CALL unknown host function: " + op.s);
                        eval_stack.push_back(Value::make_nil());
                    }
                } else errlog("CALL unsupported mode");
                break;
            }
            case OP_JMP:
                ip = (size_t)op.a - 1;
                break;
            case OP_JMP_IF_FALSE: {
                if(eval_stack.empty())
                {
                    errlog("JMP_IF_FALSE underflow");
                    break;
                }
                Value cond = eval_stack.back();
                eval_stack.pop_back();
                bool truth = true;
                if(cond.tag == Tag::Boolean && !cond.boolean) truth = false;
                else if(cond.tag == Tag::Nil) truth = false;
                else if(cond.tag == Tag::Number && cond.num == 0.0) truth = false;
                if(!truth) ip = (size_t)op.a - 1;
                break;
            }
            case OP_RET:
                // return top of eval stack or nil
                if(!eval_stack.empty())
                {
                    Value r = eval_stack.back();
                    // clear locals
                    for(auto &lv : frame.locals) lv = Value();
                    dbg("VM: exit handler");
                    return;
                }
                else
                {
                    for(auto &lv : frame.locals) lv = Value();
                    dbg("VM: exit handler");
                    return;
                }
            default:
                dbg("VM: unknown opcode");
                break;
        }
    }
    for(auto &lv : frame.locals) lv = Value();
}

Value VM::load_global(Module* m, const string &name) {
    // minimal globals: first try host functions/properties
    if(host.has_function(name)) {
        // return a "callable" as number with function index? Instead we return nil; actual calls by name go through OP_CALL b == -1
        return Value::make_nil();
    }
    dbg("load_global: " + name + " (not implemented) in module " + (m?m->name:string("<null>")));
    return Value::make_nil();
}
