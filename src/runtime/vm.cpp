#include "vm.h"
#include "util.h"
#include <optional>

using namespace std;

struct ActiveCallGuard
{
    Module *m;
    ActiveCallGuard(Module *mm): m(mm)
    {
        if(m) m->active_calls.fetch_add(1);
    }
    ~ActiveCallGuard()
    {
        if(m) m->active_calls.fetch_sub(1);
    }
};

static inline bool is_truthy(const Value &v)
{
    if(v.tag == Tag::Nil) return false;
    if(v.tag == Tag::Boolean) return v.boolean;
    if(v.tag == Tag::Number) return v.num != 0.0;
    return true;
}

static inline bool valid_const(const ByteFunc &f, int i)
{
    return i >= 0 && (size_t)i < f.consts.size();
}

static inline bool valid_local(const Frame &fr, int i)
{
    return i >= 0 && (size_t)i < fr.locals.size();
}

VM::VM(HostBridge &h): host(h)
{
    eval_stack.reserve(1024);
    arg_scratch.reserve(64);
}

Value VM::execute_handler(Module *m, const string &name)
{
    if(!m) return Value::make_nil();

    auto it = m->bytecode.handler_index.find(name);
    if(it == m->bytecode.handler_index.end())
    {
        dbg("handler not found: " + name);
        return Value::make_nil();
    }

    ActiveCallGuard guard(m);
    return execute_handler_idx(m, it->second);
}

Value VM::execute_handler_idx(Module *m, int idx)
{
    if(!m) return Value::make_nil();
    if(idx < 0 || idx >= (int)m->bytecode.funcs.size())
        return Value::make_nil();

    ByteFunc &f = m->bytecode.funcs[idx];

    Frame fr;
    fr.module = m;
    fr.func   = &f;
    fr.locals.assign(f.locals.size(), Value::make_nil());

    return run_frame(fr);
}

Value VM::call_bytecode_function(Module *m, int idx, const vector<Value> &args)
{
    if(!m) return Value::make_nil();
    if(idx < 0 || idx >= (int)m->bytecode.funcs.size())
        return Value::make_nil();

    ByteFunc &f = m->bytecode.funcs[idx];

    Frame fr;
    fr.module = m;
    fr.func   = &f;
    fr.locals.assign(f.locals.size(), Value::make_nil());

    for(size_t i=0;i<args.size() && i<fr.locals.size();++i)
        fr.locals[i] = args[i];

    return run_frame(fr);
}

Value VM::run_frame(Frame &fr)
{
    ActiveCallGuard guard(fr.module);
    ByteFunc &f = *fr.func;
    size_t base_sp = eval_stack.size();

    for(size_t ip = 0; ip < f.code.size(); ++ip)
    {
        Op &op = f.code[ip];

        switch(op.op)
        {
            case OP_PUSH_CONST:
                eval_stack.push_back(
                    valid_const(f, op.a) ? f.consts[op.a] : Value::make_nil()
                );
                break;

            case OP_PUSH_LOCAL:
                eval_stack.push_back(
                    valid_local(fr, op.a) ? fr.locals[op.a] : Value::make_nil()
                );
                break;

            case OP_STORE_LOCAL:
            {
                if(eval_stack.size() <= base_sp) break;
                Value v = eval_stack.back(); eval_stack.pop_back();
                if(valid_local(fr, op.a)) fr.locals[op.a] = v;
                break;
            }

            case OP_POP:
            {
                size_t n = (size_t)max(0, op.a);
                size_t sp = eval_stack.size();
                eval_stack.resize(sp > n ? max(base_sp, sp - n) : base_sp);
                break;
            }

            case OP_CALL:
            {
                int nargs = op.a;
                bool dynamic = (op.b == -2);

                if(eval_stack.size() < base_sp + nargs + (dynamic?1:0))
                    break;

                arg_scratch.clear();

                if(dynamic)
                {
                    Value callee = eval_stack.back();
                    eval_stack.pop_back();

                    size_t sp = eval_stack.size();
                    arg_scratch.insert(
                        arg_scratch.end(),
                        eval_stack.begin() + (sp - nargs),
                        eval_stack.begin() + sp
                    );
                    eval_stack.resize(sp - nargs);

                    if(callee.tag == Tag::Number)
                        eval_stack.push_back(
                            call_bytecode_function(fr.module, (int)callee.num, arg_scratch)
                        );
                    else
                        eval_stack.push_back(Value::make_nil());
                }
                else if(op.b >= 0)
                {
                    size_t sp = eval_stack.size();
                    arg_scratch.insert(
                        arg_scratch.end(),
                        eval_stack.begin() + (sp - nargs),
                        eval_stack.begin() + sp
                    );
                    eval_stack.resize(sp - nargs);

                    eval_stack.push_back(
                        call_bytecode_function(fr.module, op.b, arg_scratch)
                    );
                }
                else if(op.b == -1)
                {
                    size_t sp = eval_stack.size();
                    arg_scratch.insert(
                        arg_scratch.end(),
                        eval_stack.begin() + (sp - nargs),
                        eval_stack.begin() + sp
                    );
                    eval_stack.resize(sp - nargs);

                    auto r = host.call_function(op.s, arg_scratch);
                    eval_stack.push_back(r ? *r : Value::make_nil());
                }
                break;
            }

            case OP_JMP:
                ip = (size_t)op.a - 1;
                break;

            case OP_JMP_IF_FALSE:
            {
                if(eval_stack.size() <= base_sp) break;
                Value cond = eval_stack.back(); eval_stack.pop_back();
                if(!is_truthy(cond)) ip = (size_t)op.a - 1;
                break;
            }

            case OP_RET:
            {
                Value ret = Value::make_nil();
                if(eval_stack.size() > base_sp)
                    ret = eval_stack.back();

                eval_stack.resize(base_sp);
                return ret;
            }

            default:
                dbg("VM: unknown opcode");
                break;
        }
    }

    eval_stack.resize(base_sp);
    return Value::make_nil();
}
