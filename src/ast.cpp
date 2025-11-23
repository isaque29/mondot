#include "ast.h"
#include <utility>

using namespace std;

// ---------------- Expr implementations ----------------

Expr::Expr(): kind(KIdent), num(0.0) { }

// number ctor
Expr::Expr(double n): kind(KNumber), num(n) { }

// string or ident ctor
Expr::Expr(const std::string &s, bool isString)
    : kind(isString ? KString : KIdent),
      num(0.0)
{
    if(isString) str = s;
    else ident = s;
}

// call ctor
Expr::Expr(const std::string &name, vector<ExprPtr> &&a)
    : kind(KCall), call_name(name), args(move(a)), num(0.0)
{ }

// factories
ExprPtr Expr::make_ident(const std::string &id) {
    auto e = make_unique<Expr>();
    e->kind = KIdent;
    e->ident = id;
    return e;
}

ExprPtr Expr::make_number(double n) {
    return make_unique<Expr>(n);
}

ExprPtr Expr::make_string(const std::string &s) {
    return make_unique<Expr>(s, true);
}

ExprPtr Expr::make_call(const std::string &name, vector<ExprPtr> &&args) {
    return make_unique<Expr>(name, move(args));
}

ExprPtr Expr::make_funcliteral(vector<string> &&params, vector<StmtPtr> &&body) {
    auto e = make_unique<Expr>();
    e->kind = KFuncLiteral;
    e->params = move(params);
    e->body = move(body);
    return e;
}

// ---------------- Stmt implementations ----------------

Stmt::Stmt(): kind(KExpr) { }

// local declaration
StmtPtr Stmt::make_local(const std::string &name, ExprPtr init) {
    auto s = make_unique<Stmt>();
    s->kind = KLocalDecl;
    s->local_name = name;
    s->local_init = move(init);
    return s;
}

// assignment
StmtPtr Stmt::make_assign(const std::string &l, ExprPtr r) {
    auto s = make_unique<Stmt>();
    s->kind = KAssign;
    s->lhs = l;
    s->rhs = move(r);
    return s;
}

// expr stmt
StmtPtr Stmt::make_expr(ExprPtr e) {
    auto s = make_unique<Stmt>();
    s->kind = KExpr;
    s->expr = move(e);
    return s;
}

// if
StmtPtr Stmt::make_if(ExprPtr cond, vector<StmtPtr> &&then_body) {
    auto s = make_unique<Stmt>();
    s->kind = KIf;
    s->cond = move(cond);
    s->then_body = move(then_body);
    return s;
}

// while
StmtPtr Stmt::make_while(ExprPtr cond, vector<StmtPtr> &&body) {
    auto s = make_unique<Stmt>();
    s->kind = KWhile;
    s->cond = move(cond);
    s->then_body = move(body);
    return s;
}

// foreach
StmtPtr Stmt::make_foreach(const std::string &itname, ExprPtr iter_expr, vector<StmtPtr> &&body) {
    auto s = make_unique<Stmt>();
    s->kind = KForeach;
    s->iter_name = itname;
    s->iter_expr = move(iter_expr);
    s->foreach_body = move(body);
    return s;
}

// return
StmtPtr Stmt::make_return(ExprPtr e) {
    auto s = make_unique<Stmt>();
    s->kind = KReturn;
    s->expr = move(e);
    return s;
}
