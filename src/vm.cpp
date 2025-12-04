#include "vm.h"
#include "util.h"
#include <iostream>
#include <stdexcept>

using namespace std;

struct ActiveCallGuard
{
    Module *m;
    ActiveCallGuard(Module *mm): m(mm) { if(m) m->active_calls.fetch_add(1); }
    ~ActiveCallGuard() { if(m) m->active_calls.fetch_sub(1); }
};

static inline bool valid_const_idx(const ByteFunc &f, int idx)
{
    return idx >= 0 && static_cast<size_t>(idx) < f.consts.size();
}
static inline bool valid_local_idx(const Frame &frame, int idx)
{
    return idx >= 0 && static_cast<size_t>(idx) < frame.locals.size();
}

VM::VM(HostBridge &h): host(h)
{
    // reserve some sensible defaults to avoid repeated allocations
    eval_stack.reserve(1024);
    arg_scratch.reserve(64);
}

void VM::execute_handler(Module* m, const string &handler_name)
{
    if(!m) return;
    ActiveCallGuard guard(m);

    auto it = m->bytecode.handler_index.find(handler_name);
    if(it==m->bytecode.handler_index.end())
    {
        dbg("handler not found: " + handler_name + " in module " + m->name);
        return;
    }
    int idx = it->second;
    execute_handler_idx(m, idx);
}

Value VM::call_bytecode_function(Module* m, int func_idx, const vector<Value> &args)
{
    if(!m) return Value::make_nil();
    if(func_idx < 0 || func_idx >= (int)m->bytecode.funcs.size())
    {
        dbg("call_bytecode_function: invalid idx");
        return Value::make_nil();
    }
    ByteFunc &f = m->bytecode.funcs[func_idx];

    Frame frame;
    frame.module = m;
    frame.func = &f;
    frame.locals.resize(f.locals.size());

    for(size_t i=0;i<args.size() && i<frame.locals.size(); ++i)
        frame.locals[i] = args[i];

    ActiveCallGuard guard(m);
    size_t base_sp = eval_stack.size();

    for(size_t ip=0; ip < f.code.size(); ++ip)
    {
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
                if(eval_stack.size() <= base_sp) { errlog("STORE_LOCAL: stack underflow"); break; }
                {
                    Value v = std::move(eval_stack.back()); eval_stack.pop_back();
                    if(valid_local_idx(frame, op.a)) frame.locals[op.a] = std::move(v);
                    else dbg("STORE_LOCAL: invalid local");
                }
                break;

            case OP_POP:
                if(op.a <= 0) break;
                {
                    size_t sp = eval_stack.size();
                    size_t newsp = (sp > (size_t)op.a) ? sp - (size_t)op.a : base_sp;
                    if(newsp < base_sp) newsp = base_sp;
                    eval_stack.resize(newsp);
                }
                break;

            case OP_CALL: {
                int nargs = op.a;
                bool need_callee = (op.b == -2);
                if((int)eval_stack.size() < (int)base_sp + nargs + (need_callee ? 1 : 0)) { errlog("CALL: stack underflow"); break; }

                // build args into arg_scratch (reuse vector to avoid allocations)
                arg_scratch.clear();
                if(need_callee)
                {
                    // layout before call: [..., arg0, arg1, ..., argN-1, callee]
                    Value callee = eval_stack.back();
                    eval_stack.pop_back(); // remove callee
                    size_t sp = eval_stack.size();
                    const Value* args_ptr = (nargs>0 && sp >= (size_t)nargs) ? &eval_stack[sp - nargs] : nullptr;
                    if(args_ptr && nargs>0) arg_scratch.insert(arg_scratch.end(), args_ptr, args_ptr + nargs);
                    // remove args from eval_stack
                    eval_stack.resize(sp - (size_t)nargs);

                    // dynamic callee expected to be a number (function idx)
                    if(callee.tag == Tag::Number)
                    {
                        int fidx = (int)callee.num;
                        Value ret = call_bytecode_function(m, fidx, arg_scratch);
                        eval_stack.push_back(ret);
                    }
                    else
                    {
                        errlog("CALL dynamic: callee not function index");
                        eval_stack.push_back(Value::make_nil());
                    }
                }
                else if(op.b >= 0)
                {
                    // static bytecode function index in op.b
                    size_t sp = eval_stack.size();
                    const Value* args_ptr = (nargs>0 && sp >= (size_t)nargs) ? &eval_stack[sp - nargs] : nullptr;
                    if(args_ptr && nargs>0) arg_scratch.insert(arg_scratch.end(), args_ptr, args_ptr + nargs);
                    eval_stack.resize(sp - (size_t)nargs);
                    Value ret = call_bytecode_function(m, op.b, arg_scratch);
                    eval_stack.push_back(ret);
                }
                else if(op.b == -1)
                {
                    // host/global call by name in op.s
                    size_t sp = eval_stack.size();
                    const Value* args_ptr = (nargs>0 && sp >= (size_t)nargs) ? &eval_stack[sp - nargs] : nullptr;
                    if(args_ptr && nargs>0) arg_scratch.insert(arg_scratch.end(), args_ptr, args_ptr + nargs);
                    eval_stack.resize(sp - (size_t)nargs);

                    if(host.has_function(op.s))
                    {
                        Value ret = host.call_function(op.s, arg_scratch);
                        eval_stack.push_back(ret);
                    }
                    else
                    {
                        errlog("CALL: unknown function " + op.s);
                        eval_stack.push_back(Value::make_nil());
                    }
                }
                else
                {
                    errlog("CALL: unsupported mode");
                }
                break;
            }

            case OP_JMP:
                ip = (size_t)op.a - 1; // -1 because loop will ++
                break;

            case OP_JMP_IF_FALSE: {
                if(eval_stack.size() <= base_sp) { errlog("JMP_IF_FALSE: stack underflow"); break; }
                Value cond = eval_stack.back(); eval_stack.pop_back();
                bool truth = true;
                if(cond.tag == Tag::Nil) truth = false;
                else if(cond.tag == Tag::Number && cond.num == 0.0) truth = false;
                if(!truth) ip = (size_t)op.a - 1;
                break;
            }

            case OP_RET: {
                Value ret = Value::make_nil();
                if(eval_stack.size() > base_sp) ret = eval_stack.back();

                eval_stack.resize(base_sp);
                return ret;
            }

            default:
                dbg("call_bytecode_function: unknown opcode");
                break;
        }
    }

    eval_stack.resize(base_sp);
    return Value::make_nil();
}

void VM::execute_handler_idx(Module* m, int idx)
{
    if(!m) return;
    if(idx < 0 || idx >= (int)m->bytecode.funcs.size()) { dbg("execute_handler_idx: invalid idx"); return; }
    ByteFunc &f = m->bytecode.funcs[idx];

    Frame frame;
    frame.module = m;
    frame.func = &f;
    frame.locals.resize(f.locals.size());

    ActiveCallGuard guard(m);

    size_t base_sp = eval_stack.size();

    for(size_t ip=0; ip < f.code.size(); ++ip)
    {
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
                if(eval_stack.size() <= base_sp)
                {
                    errlog("STORE_LOCAL: underflow");
                    break;
                }
                {
                    Value v = std::move(eval_stack.back()); eval_stack.pop_back();
                    if(valid_local_idx(frame, op.a)) frame.locals[op.a] = std::move(v);
                    else dbg("STORE_LOCAL invalid");
                }
                break;

            case OP_POP:
                if(op.a > 0)
                {
                    size_t sp = eval_stack.size();
                    size_t newsp = (sp > (size_t)op.a) ? sp - (size_t)op.a : base_sp;
                    if(newsp < base_sp) newsp = base_sp;
                    eval_stack.resize(newsp);
                }
                break;

            case OP_CALL: {
                int nargs = op.a;
                bool need_callee = (op.b == -2);
                if((int)eval_stack.size() < (int)base_sp + nargs + (need_callee ? 1 : 0)) { errlog("CALL: stack underflow"); break; }

                arg_scratch.clear();
                if(need_callee)
                {
                    Value callee = eval_stack.back();
                    eval_stack.pop_back();
                    size_t sp = eval_stack.size();
                    const Value* args_ptr = (nargs>0 && sp >= (size_t)nargs) ? &eval_stack[sp - nargs] : nullptr;
                    if(args_ptr && nargs>0) arg_scratch.insert(arg_scratch.end(), args_ptr, args_ptr + nargs);
                    eval_stack.resize(sp - (size_t)nargs);

                    if(callee.tag == Tag::Number)
                    {
                        int fidx = (int)callee.num;
                        Value ret = call_bytecode_function(m, fidx, arg_scratch);
                        eval_stack.push_back(ret);
                    }
                    else
                    {
                        errlog("CALL dynamic: callee not function index");
                        eval_stack.push_back(Value::make_nil());
                    }
                }
                else if(op.b >= 0)
                {
                    size_t sp = eval_stack.size();
                    const Value* args_ptr = (nargs>0 && sp >= (size_t)nargs) ? &eval_stack[sp - nargs] : nullptr;
                    if(args_ptr && nargs>0) arg_scratch.insert(arg_scratch.end(), args_ptr, args_ptr + nargs);
                    eval_stack.resize(sp - (size_t)nargs);

                    Value ret = call_bytecode_function(m, op.b, arg_scratch);
                    eval_stack.push_back(ret);
                }
                else if(op.b == -1)
                {
                    size_t sp = eval_stack.size();
                    const Value* args_ptr = (nargs>0 && sp >= (size_t)nargs) ? &eval_stack[sp - nargs] : nullptr;
                    if(args_ptr && nargs>0) arg_scratch.insert(arg_scratch.end(), args_ptr, args_ptr + nargs);
                    eval_stack.resize(sp - (size_t)nargs);

                    if(host.has_function(op.s))
                    {
                        Value ret = host.call_function(op.s, arg_scratch);
                        eval_stack.push_back(ret);
                    }
                    else
                    {
                        errlog("CALL unknown host function: " + op.s);
                        eval_stack.push_back(Value::make_nil());
                    }
                }
                else errlog("CALL unsupported mode");
                break;
            }

            case OP_JMP:
                ip = (size_t)op.a - 1;
                break;

            case OP_JMP_IF_FALSE: {
                if(eval_stack.size() <= base_sp)
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
                for(auto &lv : frame.locals) lv = Value();
                if(eval_stack.size() > base_sp)
                {
                    eval_stack.resize(base_sp);
                    dbg("VM: exit handler");
                    return;
                }
                else
                {
                    eval_stack.resize(base_sp);
                    dbg("VM: exit handler");
                    return;
                }

            default:
                dbg("VM: unknown opcode");
                break;
        }
    }

    eval_stack.resize(base_sp);
    for(auto &lv : frame.locals) lv = Value();
}
 
Value VM::load_global(Module* m, const string &name)
{
    if(host.has_function(name))
        return Value::make_nil();
    
    dbg("load_global: " + name + " (not implemented) in module " + (m?m->name:string("<null>")));
    return Value::make_nil();
}
