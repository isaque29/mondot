#include "bytecode.h"
#include "util.h"
#include <stdexcept>
#include <unordered_map>
#include <string>
#include <functional>
#include "host_manifest.h"

using namespace std;

static int try_get_local(const unordered_map<string,int> &local_index, const string &name)
{
    auto it = local_index.find(name);
    return it == local_index.end() ? -1 : it->second;
}

static int push_const(ByteFunc &bf, const Value &v)
{
    bf.consts.push_back(v);
    return (int)bf.consts.size() - 1;
}

CompiledUnit compile_unit(UnitDecl *u)
{
    ByteModule mod; mod.name = u->name;
    CompiledUnit cu; cu.module = mod;

    for(auto &hptr : u->handlers)
    {
        HandlerDecl *h = hptr.get();
        ByteFunc bf;
        unordered_map<string,int> local_index;

        auto add_local = [&](const string &name)->int {
            if(local_index.count(name)) return local_index.at(name);
            int id = (int)bf.locals.size();
            bf.locals.push_back(name);
            local_index[name] = id;
            return id;
        };

        add_local("_tmp");

        auto emit = [&](const Op &op){ bf.code.push_back(op); };

        // compile expression
        function<void(Expr*)> compile_expr;
        compile_expr = [&](Expr* e)
        {
            switch(e->kind)
            {
                case Expr::KBoolean: {
                    int ci = push_const(bf, Value::make_boolean(e->num));
                    emit(Op(OP_PUSH_CONST, ci, 0));
                    break;
                }
                case Expr::KNumber: {
                    int ci = push_const(bf, Value::make_number(e->num));
                    emit(Op(OP_PUSH_CONST, ci, 0));
                    break;
                }
                case Expr::KNil: {
                    int ci = push_const(bf, Value::make_nil());
                    emit(Op(OP_PUSH_CONST, ci, 0));
                    break;
                }
                case Expr::KString: {
                    int ci = push_const(bf, Value::make_string(e->str));
                    emit(Op(OP_PUSH_CONST, ci, 0));
                    break;
                }
                case Expr::KIdent: {
                    int lid = try_get_local(local_index, e->ident);
                    if(lid >= 0) emit(Op(OP_PUSH_LOCAL, lid, 0));
                    else throw runtime_error(
                            string("unresolved identifier '") + e->ident +
                            "': globals are not allowed; declare as local or pass as parameter"
                        );
                    break;
                }
                case Expr::KCall: {
                    // compile args left-to-right
                    for(auto &a : e->args) compile_expr(a.get());

                    int lid = try_get_local(local_index, e->call_name);
                    if(lid >= 0)
                    {
                        emit(Op(OP_PUSH_LOCAL, lid, 0));
                        emit(Op(OP_CALL, (int)e->args.size(), -2));
                    }
                    else
                    {
                        if (HostManifest::has(e->call_name))
                        {
                            Op call(OP_CALL, (int)e->args.size(), -1);
                            call.s = e->call_name;
                            emit(call);
                        }
                        else
                        {
                            throw runtime_error(
                                string("unresolved function '") + e->call_name +
                                "': globals not allowed; assign function to local variable or import explicitly"
                            );
                        }
                    }
                    break;
                }
                case Expr::KCallExpr: {
                    // TODO
                    throw runtime_error("KCallExpr unsupported in this compile path");
                }
                case Expr::KFuncLiteral: {
                    // TODO.
                    throw runtime_error("function literal not supported in this compiler (closures not implemented)");
                }
                default:
                    throw runtime_error("unsupported expr kind in compile_expr");
            }
        };

        function<void(const vector<unique_ptr<Stmt>>&)> compile_block;
        compile_block = [&](const vector<unique_ptr<Stmt>> &stmts)
        {
            for(size_t si=0; si<stmts.size(); ++si) {
                Stmt *st = stmts[si].get();
                switch(st->kind) {
                    case Stmt::KLocalDecl: {
                        // local declaration: expected fields local_name, local_init (Expr*)
                        if(!st->local_name.size()) throw runtime_error("local decl requires name");
                        if(st->local_init) {
                            compile_expr(st->local_init.get());
                        } else {
                            int ci = push_const(bf, Value::make_nil());
                            emit(Op(OP_PUSH_CONST, ci, 0));
                        }
                        int lid = add_local(st->local_name);
                        emit(Op(OP_STORE_LOCAL, lid, 0));
                        break;
                    }
                    case Stmt::KAssign: {
                        // assignment: lhs (string), rhs (Expr*)
                        if(!st->lhs.size()) throw runtime_error("assign requires lhs");
                        compile_expr(st->rhs.get());
                        
                        int lid = try_get_local(local_index, st->lhs);
                        if(lid < 0) throw runtime_error(string("assign to undeclared name '") + st->lhs + "': declare as local first");
                        emit(Op(OP_STORE_LOCAL, lid, 0));
                        break;
                    }
                    case Stmt::KExpr: {
                        // expression statement: e.g., function call
                        if(!st->expr) throw runtime_error("expr stmt without expression");
                        if(st->expr->kind != Expr::KCall) throw runtime_error("expr stmt must be a call in this prototype");
                        // compile call & drop return
                        compile_expr(st->expr.get());
                        emit(Op(OP_POP, 1, 0));
                        break;
                    }
                    case Stmt::KIf: {
                        // if statement with optional elseif parts and else
                        // fields: cond (Expr*), then_body (vector<Stmt>), elseif_parts (vector<pair<Expr*, vector<Stmt>>>), else_body (vector<Stmt>)
                        compile_expr(st->cond.get());
                        // emit JMP_IF_FALSE to after then
                        Op jif(OP_JMP_IF_FALSE, 0, 0);
                        emit(jif);
                        size_t jif_pos = bf.code.size()-1;

                        // then body
                        compile_block(st->then_body);

                        // after then, jump to after all else/elseif
                        Op jmp(OP_JMP, 0, 0);
                        emit(jmp);
                        size_t jmp_pos = bf.code.size()-1;

                        // fix jif target to current pos (start of elseif/else)
                        bf.code[jif_pos].a = (int)bf.code.size();

                        // elseif parts
                        for(auto &ep : st->elseif_parts)
                        {
                            // ep.first = cond (unique_ptr<Expr>), ep.second = vector<unique_ptr<Stmt>>
                            compile_expr(ep.first.get());
                            Op jif2(OP_JMP_IF_FALSE, 0, 0);
                            emit(jif2);
                            size_t jif2_pos = bf.code.size()-1;

                            compile_block(ep.second);

                            Op jmp2(OP_JMP, 0, 0);
                            emit(jmp2);
                            size_t jmp2_pos = bf.code.size()-1;

                            // fix jif2 to current pos
                            bf.code[jif2_pos].a = (int)bf.code.size();
                            // leave jmp2 target to be fixed by outer logic later; for simplicity set to current pos for now
                            bf.code[jmp2_pos].a = (int)bf.code.size();
                        }

                        // else
                        if(!st->else_body.empty())
                        {
                            compile_block(st->else_body);
                        }

                        // fix jmp (after then) to current pos
                        bf.code[jmp_pos].a = (int)bf.code.size();
                        break;
                    }
                    case Stmt::KWhile: {
                        // fields: cond (Expr*), then_body (vector<Stmt>)
                        size_t loop_start = bf.code.size();
                        compile_expr(st->cond.get());
                        Op jif(OP_JMP_IF_FALSE, 0, 0);
                        emit(jif);
                        size_t jif_pos = bf.code.size()-1;

                        compile_block(st->then_body);

                        // jump back to loop start
                        emit(Op(OP_JMP, (int)loop_start, 0));

                        // fix jif to after loop
                        bf.code[jif_pos].a = (int)bf.code.size();
                        break;
                    }
                    case Stmt::KForeach: {
                        // fields: iter_name (string), iter_expr (Expr*), foreach_body (vector<Stmt>)
                        // only string iteration is supported via host helpers strlen, str_char_at, add, lt
                        compile_expr(st->iter_expr.get());
                        int seq_local = add_local("__foreach_seq");
                        emit(Op(OP_STORE_LOCAL, seq_local, 0));

                        int idx_local = add_local("__foreach_idx");
                        int ci0 = push_const(bf, Value::make_number(0));
                        emit(Op(OP_PUSH_CONST, ci0, 0));
                        emit(Op(OP_STORE_LOCAL, idx_local, 0));

                        size_t loop_ip = bf.code.size();

                        // call strlen(seq)
                        emit(Op(OP_PUSH_LOCAL, seq_local, 0));
                        { Op calllen(OP_CALL, 1, -1); calllen.s = "strlen"; emit(calllen); }
                        // push idx; compare idx < len via host lt(idx, len)
                        emit(Op(OP_PUSH_LOCAL, idx_local, 0));
                        { Op calllt(OP_CALL, 2, -1); calllt.s = "lt"; emit(calllt); }
                        Op jif2(OP_JMP_IF_FALSE, 0, 0); emit(jif2);
                        size_t jif2_pos = bf.code.size()-1;

                        // str_char_at(seq, idx)
                        emit(Op(OP_PUSH_LOCAL, seq_local, 0));
                        emit(Op(OP_PUSH_LOCAL, idx_local, 0));
                        { Op callchar(OP_CALL, 2, -1); callchar.s = "str_char_at"; emit(callchar); }

                        // store into foreach var
                        int itlid = add_local(st->iter_name);
                        emit(Op(OP_STORE_LOCAL, itlid, 0));

                        // body
                        compile_block(st->foreach_body);

                        // idx = add(idx, 1)
                        emit(Op(OP_PUSH_LOCAL, idx_local, 0));
                        int ci1 = push_const(bf, Value::make_number(1));
                        emit(Op(OP_PUSH_CONST, ci1, 0));
                        { Op calladd(OP_CALL, 2, -1); calladd.s = "add"; emit(calladd); }
                        emit(Op(OP_STORE_LOCAL, idx_local, 0));

                        // jump back
                        emit(Op(OP_JMP, (int)loop_ip, 0));

                        // fix jif2 -> exit
                        bf.code[jif2_pos].a = (int)bf.code.size();
                        break;
                    }
                    case Stmt::KReturn: {
                        // fields: expr
                        compile_expr(st->expr.get());
                        emit(Op(OP_RET,0,0));
                        break;
                    }
                    default:
                        throw runtime_error("unsupported stmt kind in compile_unit");
                }
            }
        };

        compile_block(h->body);
        emit(Op(OP_RET,0,0));

        // push bf into module
        int idx = (int)cu.module.funcs.size();
        cu.module.funcs.push_back(move(bf));
        cu.module.handler_index[h->name] = idx;
    }

    return cu;
}
