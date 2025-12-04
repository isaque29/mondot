#include "util.h"

#include <cstdio>
#include <iostream>
#include <set>
#include <string>
#include <algorithm>

#ifdef _WIN32
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
#else
 #include <unistd.h>
#endif

#include "lexer.h"
#include "ast.h"
#include "bytecode.h"
#include "module.h"

static bool TERM_SUPPORTS_COLOR = false;

void enable_terminal_colors()
{
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) { TERM_SUPPORTS_COLOR = false; return; }
    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) { TERM_SUPPORTS_COLOR = false; return; }
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
    if (!SetConsoleMode(hOut, dwMode)) { TERM_SUPPORTS_COLOR = false; return; }
    TERM_SUPPORTS_COLOR = true;
#else
    TERM_SUPPORTS_COLOR = isatty(fileno(stdout));
#endif
}

static constexpr const char* COL_RESET      = "\x1b[0m";
static constexpr const char* COL_DARKGRAY   = "\x1b[90m";
static constexpr const char* COL_YELLOW     = "\x1b[93m";
static constexpr const char* COL_RED        = "\x1b[31m";
static constexpr const char* COL_DARKGREEN  = "\x1b[32m";
static constexpr const char* COL_GREEN      = "\x1b[92m";

void colored_fprintf(FILE* out, const char* color, const std::string &msg)
{
    if (TERM_SUPPORTS_COLOR && color)
        fprintf(out, "%s%s%s\n", color, msg.c_str(), COL_RESET);
    else
        fprintf(out, "%s\n", msg.c_str());
}

void errlog(const std::string &s)
{
    colored_fprintf(stderr, COL_RED, std::string("[err] ") + s);
}

#if MONDOT_DEBUG

void dbg(const std::string &s)
{
    colored_fprintf(stderr, COL_DARKGRAY, std::string("[dbg] ") + s);
}
void info(const std::string &s)
{
    colored_fprintf(stdout, COL_YELLOW, std::string("[info] ") + s);
}

static void add_tok_str(std::set<std::string> &out, const std::string &s) { out.insert(s); }
static void collect_tokens_from_expr(const Expr* e, std::set<std::string> &out);
static void collect_tokens_from_stmt(const Stmt* s, std::set<std::string> &out);

static void collect_tokens_from_expr(const Expr* e, std::set<std::string> &out)
{
    if (!e) return;
    switch (e->kind)
    {
        case Expr::KNil:        add_tok_str(out, "Nil");     break;
        case Expr::KBoolean:    add_tok_str(out, "Boolean"); break;
        case Expr::KNumber:     add_tok_str(out, "Number");  break;
        case Expr::KString:     add_tok_str(out, "String");  break;
        case Expr::KIdent:
            add_tok_str(out, std::string("id:") + e->ident);
            add_tok_str(out, "Identifier");
            break;
        case Expr::KCall:
        case Expr::KCallExpr:
            if (!e->call_name.empty()) add_tok_str(out, std::string("call:") + e->call_name);
            else add_tok_str(out, "Call");
            for (const auto &a : e->args) collect_tokens_from_expr(a.get(), out);
            break;
        case Expr::KFuncLiteral:
            add_tok_str(out, "func-literal");
            for (const auto &p : e->params) add_tok_str(out, std::string("param:") + p);
            for (const auto &st : e->body) collect_tokens_from_stmt(st.get(), out);
            break;
        default:
            add_tok_str(out, "expr-unknown");
            break;
    }
}

static void collect_tokens_from_stmt(const Stmt* s, std::set<std::string> &out)
{
    if (!s) return;
    switch (s->kind)
    {
        case Stmt::KLocalDecl:
            add_tok_str(out, "local");
            add_tok_str(out, std::string("id:") + s->local_name);
            if (s->local_init)
            {
                add_tok_str(out, "=");
                collect_tokens_from_expr(s->local_init.get(), out);
            }
            break;
        case Stmt::KAssign:
            add_tok_str(out, std::string("id:") + s->lhs);
            add_tok_str(out, "=");
            collect_tokens_from_expr(s->rhs.get(), out);
            break;
        case Stmt::KExpr:
            collect_tokens_from_expr(s->expr.get(), out);
            break;
        case Stmt::KIf:
            add_tok_str(out, "if");
            collect_tokens_from_expr(s->cond.get(), out);
            for (const auto &b : s->then_body) collect_tokens_from_stmt(b.get(), out);
            for (const auto &ep : s->elseif_parts)
            {
                add_tok_str(out, "elseif");
                collect_tokens_from_expr(ep.first.get(), out);
                for (const auto &st : ep.second) collect_tokens_from_stmt(st.get(), out);
            }
            if (!s->else_body.empty())
            {
                add_tok_str(out, "else");
                for (const auto &st : s->else_body) collect_tokens_from_stmt(st.get(), out);
            }
            break;
        case Stmt::KWhile:
            add_tok_str(out, "while");
            collect_tokens_from_expr(s->cond.get(), out);
            for (const auto &b : s->then_body) collect_tokens_from_stmt(b.get(), out);
            break;
        case Stmt::KForeach:
            add_tok_str(out, "foreach");
            add_tok_str(out, std::string("it:") + s->iter_name);
            collect_tokens_from_expr(s->iter_expr.get(), out);
            for (const auto &b : s->foreach_body) collect_tokens_from_stmt(b.get(), out);
            break;
        case Stmt::KReturn:
            add_tok_str(out, "return");
            collect_tokens_from_expr(s->expr.get(), out);
            break;
        default:
            add_tok_str(out, "stmt-unknown");
            break;
    }
}

void dump_program_tokens(const Program* p, FILE* out)
{
    if (!p) { fprintf(out, "dump_program_tokens: program is null\n"); return; }

    for (const auto &u : p->units)
    {
        std::set<std::string> tokens;
        if (u && !u->name.empty()) tokens.insert(std::string("unit:") + u->name);
        for (const auto &h : u->handlers)
        {
            if (h && !h->name.empty()) tokens.insert(std::string("handler:") + h->name);
            for (const auto &pn : h->params) tokens.insert(std::string("param:") + pn);
            for (const auto &st : h->body) collect_tokens_from_stmt(st.get(), tokens);
        }

        std::string joined;
        bool first = true;
        for (const auto &t : tokens)
        {
            if (!first) joined += ", ";
            joined += t;
            first = false;
        }
        if (joined.empty()) joined = "(no tokens)";

        std::string header = std::string("Unit ") + (u->name.empty() ? "<anon>" : ("'" + u->name + "'")) + " : ";
        if (TERM_SUPPORTS_COLOR)
            fprintf(out, "%s%s%s %s%s%s\n", COL_DARKGRAY, header.c_str(), COL_RESET, COL_DARKGREEN, joined.c_str(), COL_RESET);
        else
            fprintf(out, "%s %s\n", header.c_str(), joined.c_str());
    }
}

void dump_module_bytecode(const Module* m, FILE* out)
{
    if (!m) { fprintf(out, "dump_module_bytecode: module is null\n"); return; }

    const auto &bm = m->bytecode;
    std::string handlers_joined;
    bool first = true;
    try
    {
        for (const auto &kv : bm.handler_index)
        {
            if (!first) handlers_joined += ", ";
            handlers_joined += kv.first + "->" + std::to_string(kv.second);
            first = false;
        }
    }
    catch (...)
    {
        handlers_joined = "(handler_index unavailable)";
    }
    if (handlers_joined.empty()) handlers_joined = "(no handlers)";

    std::string header = std::string("Bytecode for ") + (m->name.empty() ? "<anon>" : ("'" + m->name + "'")) + " : ";
    if (TERM_SUPPORTS_COLOR)
        fprintf(out, "%s%s%s %s%s%s\n", COL_DARKGRAY, header.c_str(), COL_RESET, COL_GREEN, handlers_joined.c_str(), COL_RESET);
    else
        fprintf(out, "%s %s\n", header.c_str(), handlers_joined.c_str());
}

#endif
