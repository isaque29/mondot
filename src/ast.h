#ifndef MONDOT_AST_H
#define MONDOT_AST_H

#include <string>
#include <vector>
#include <memory>
#include <utility>

struct Expr;
struct Stmt;

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

struct Expr {
    enum Kind {
        KNumber,
        KString,
        KIdent,
        KCall,
        KCallExpr,
        KFuncLiteral
    } kind;

    // number
    double num{0.0};

    // string literal
    std::string str;

    // identifier name (or dotted name)
    std::string ident;

    // call
    std::string call_name;
    std::vector<ExprPtr> args;

    // function literal
    std::vector<std::string> params;
    std::vector<StmtPtr> body;

    // ctors
    Expr(); // default
    Expr(double n);
    Expr(const std::string &s, bool isString);
    Expr(const std::string &name, std::vector<ExprPtr> &&a); // call
    static ExprPtr make_ident(const std::string &id);
    static ExprPtr make_number(double n);
    static ExprPtr make_string(const std::string &s);
    static ExprPtr make_call(const std::string &name, std::vector<ExprPtr> &&args);
    static ExprPtr make_funcliteral(std::vector<std::string> &&params, std::vector<StmtPtr> &&body);
};

struct Stmt {
    enum Kind {
        KLocalDecl,
        KAssign,
        KExpr,
        KIf,
        KWhile,
        KForeach,
        KReturn
    } kind;

    // for local decl
    std::string local_name;
    ExprPtr local_init; // optional

    // for assign
    std::string lhs;
    ExprPtr rhs;

    // for expression statement
    ExprPtr expr;

    // for if
    ExprPtr cond;
    std::vector<StmtPtr> then_body;
    // elseif_parts: pair<cond, body>
    std::vector<std::pair<ExprPtr, std::vector<StmtPtr>>> elseif_parts;
    std::vector<StmtPtr> else_body;

    // for while
    // cond + then_body used

    // for foreach
    std::string iter_name;
    ExprPtr iter_expr;
    std::vector<StmtPtr> foreach_body;

    // for return
    // expr used

    // factory helpers / ctors
    Stmt();
    static StmtPtr make_local(const std::string &name, ExprPtr init); // local name = init (init may be nullptr)
    static StmtPtr make_assign(const std::string &l, ExprPtr r);
    static StmtPtr make_expr(ExprPtr e);
    static StmtPtr make_if(ExprPtr cond, std::vector<StmtPtr> &&then_body);
    static StmtPtr make_while(ExprPtr cond, std::vector<StmtPtr> &&body);
    static StmtPtr make_foreach(const std::string &itname, ExprPtr iter_expr, std::vector<StmtPtr> &&body);
    static StmtPtr make_return(ExprPtr e);
};

struct HandlerDecl {
    std::string name;
    std::vector<std::string> params;
    std::vector<StmtPtr> body;
};

struct UnitDecl {
    std::string name;
    std::vector<std::unique_ptr<HandlerDecl>> handlers;
};

struct Program {
    std::vector<std::unique_ptr<UnitDecl>> units;
};

#endif
