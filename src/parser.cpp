#include "parser.h"
#include <stdexcept>
#include <sstream>

using namespace std;

Parser::Parser(const string &s): lex(s) { cur = lex.next(); }

void Parser::eat() { cur = lex.next(); }
bool Parser::accept(TokenKind k) { if(cur.kind==k){ eat(); return true; } return false; }
void Parser::expect(TokenKind k, const string &msg) { if(cur.kind!=k) throw runtime_error(string("parse error: expected ")+msg+" got '"+cur.text+"' at line "+to_string(cur.line)); eat(); }

unique_ptr<Program> Parser::parse_program() {
    auto p = make_unique<Program>();
    while(cur.kind != TokenKind::End) {
        if(cur.kind == TokenKind::Kw_unit) p->units.push_back(parse_unit());
        else throw runtime_error("expected 'unit' at top-level");
    }
    return p;
}

unique_ptr<UnitDecl> Parser::parse_unit() {
    expect(TokenKind::Kw_unit, "unit");
    if(cur.kind != TokenKind::Identifier) throw runtime_error("expected unit name");
    string uname = cur.text; eat();
    expect(TokenKind::LBrace, "{");
    auto u = make_unique<UnitDecl>();
    u->name = uname;
    while(cur.kind != TokenKind::RBrace) {
        if(cur.kind == TokenKind::Kw_on) u->handlers.push_back(parse_handler());
        else throw runtime_error("expected 'on' in unit");
    }
    expect(TokenKind::RBrace, "}");
    return u;
}

unique_ptr<HandlerDecl> Parser::parse_handler() {
    expect(TokenKind::Kw_on, "on");
    if(cur.kind != TokenKind::Identifier) throw runtime_error("expected event name");
    string hname = cur.text; eat();
    expect(TokenKind::Arrow, "->");
    expect(TokenKind::LParen, "(");
    // parse params (optional)
    vector<string> params;
    if(cur.kind != TokenKind::RParen) {
        if(cur.kind == TokenKind::Identifier) {
            params.push_back(cur.text); eat();
            while(cur.kind == TokenKind::Comma) { eat(); if(cur.kind==TokenKind::Identifier){ params.push_back(cur.text); eat(); } else throw runtime_error("expected param name"); }
        }
    }
    expect(TokenKind::RParen, ")");
    auto h = make_unique<HandlerDecl>();
    h->name = hname;
    h->params = params;

    // parse body until 'end'
    while(cur.kind != TokenKind::Kw_end) {
        if(cur.kind == TokenKind::Semicolon) { eat(); continue; }
        h->body.push_back(parse_statement());
    }
    expect(TokenKind::Kw_end, "end");
    return h;
}

// parse a top-level statement inside handler
unique_ptr<Stmt> Parser::parse_statement() {
    if(cur.kind == TokenKind::Kw_local) {
        // local declaration: local id = expr ;
        eat();
        if(cur.kind != TokenKind::Identifier) throw runtime_error("expected identifier after local");
        string name = cur.text; eat();
        if(cur.kind == TokenKind::Equal) {
            eat();
            auto init = parse_expression();
            expect(TokenKind::Semicolon, ";");
            return Stmt::make_local(name, move(init));
        } else {
            expect(TokenKind::Semicolon, ";");
            return Stmt::make_local(name, nullptr);
        }
    }
    if(cur.kind == TokenKind::Kw_if) {
        // if (expr) stmts [elseif (expr) stmts]* [else stmts] end
        eat();
        expect(TokenKind::LParen, "(");
        auto cond = parse_expression();
        expect(TokenKind::RParen, ")");
        vector<unique_ptr<Stmt>> then_body;
        while(!(cur.kind==TokenKind::Kw_elseif||cur.kind==TokenKind::Kw_else||cur.kind==TokenKind::Kw_end)) {
            then_body.push_back(parse_statement());
        }
        auto s = Stmt::make_if(move(cond), move(then_body));
        // elseif parts
        while(cur.kind == TokenKind::Kw_elseif) {
            eat();
            expect(TokenKind::LParen, "(");
            auto econd = parse_expression();
            expect(TokenKind::RParen, ")");
            vector<unique_ptr<Stmt>> eb;
            while(!(cur.kind==TokenKind::Kw_elseif||cur.kind==TokenKind::Kw_else||cur.kind==TokenKind::Kw_end)) eb.push_back(parse_statement());
            s->elseif_parts.emplace_back(move(econd), move(eb));
        }
        if(cur.kind == TokenKind::Kw_else) {
            eat();
            vector<unique_ptr<Stmt>> elseb;
            while(!(cur.kind==TokenKind::Kw_end)) elseb.push_back(parse_statement());
            s->else_body = move(elseb);
        }
        expect(TokenKind::Kw_end, "end");
        return s;
    }
    if(cur.kind == TokenKind::Kw_while) {
        eat();
        expect(TokenKind::LParen, "(");
        auto cond = parse_expression();
        expect(TokenKind::RParen, ")");
        vector<unique_ptr<Stmt>> body;
        while(cur.kind != TokenKind::Kw_end) body.push_back(parse_statement());
        expect(TokenKind::Kw_end, "end");
        return Stmt::make_while(move(cond), move(body));
    }
    if(cur.kind == TokenKind::Kw_foreach) {
        eat();
        if(cur.kind != TokenKind::Identifier) throw runtime_error("expected identifier after foreach");
        string itname = cur.text; eat();
        expect(TokenKind::Kw_in, "in");
        auto iter_expr = parse_expression();
        vector<unique_ptr<Stmt>> body;
        while(cur.kind != TokenKind::Kw_end) body.push_back(parse_statement());
        expect(TokenKind::Kw_end, "end");
        return Stmt::make_foreach(itname, move(iter_expr), move(body));
    }
    if(cur.kind == TokenKind::Kw_return) {
        eat();
        auto e = parse_expression();
        expect(TokenKind::Semicolon, ";");
        return Stmt::make_return(move(e));
    }

    // local assign / identifier start (could be assignment or call)
    if(cur.kind == TokenKind::Identifier) {
        string id = cur.text; eat();
        if(cur.kind == TokenKind::Equal) {
            // assignment
            eat();
            auto rhs = parse_expression();
            expect(TokenKind::Semicolon, ";");
            return Stmt::make_assign(id, move(rhs));
        } else if(cur.kind == TokenKind::LParen) {
            // call expression
            auto call = parse_call_expr(id);
            expect(TokenKind::Semicolon, ";");
            return Stmt::make_expr(move(call));
        } else {
            throw runtime_error("unexpected token after identifier: " + cur.text);
        }
    }

    // literal expr stmt
    if(cur.kind==TokenKind::String || cur.kind==TokenKind::Number) {
        auto e = parse_expression();
        expect(TokenKind::Semicolon, ";");
        return Stmt::make_expr(move(e));
    }

    throw runtime_error("unsupported or unexpected token in statement");
}

unique_ptr<Expr> Parser::parse_expression() {
    return parse_primary();
}

unique_ptr<Expr> Parser::parse_primary() {
    if(cur.kind == TokenKind::Number) {
        double n = stod(cur.text); eat();
        return make_unique<Expr>(n);
    }
    if(cur.kind == TokenKind::String) {
        string s = cur.text; eat();
        return make_unique<Expr>(s, true);
    }
    if(cur.kind == TokenKind::Identifier) {
        string id = cur.text; eat();
        if(cur.kind == TokenKind::LParen) return parse_call_expr(id);
        // identifier could be dotted (Console.Write) — already lexed as one id
        auto e = make_unique<Expr>();
        e->kind = Expr::KIdent;
        e->ident = id;
        return e;
    }
    if(cur.kind == TokenKind::LParen) {
        // possibly func literal: (params) ... end
        // If next token after LParen is identifier or ), treat as func literal only if followed by body tokens (heuristic)
        // We'll check: if we see ')' then next token LBrace or identifier? For simplicity we attempt to parse function literal here.
        return parse_func_literal();
    }
    throw runtime_error(string("parse expr error at token '") + cur.text + "'");
}

unique_ptr<Expr> Parser::parse_call_expr(const string &name) {
    expect(TokenKind::LParen, "(");
    vector<unique_ptr<Expr>> args;
    if(cur.kind != TokenKind::RParen) {
        args.push_back(parse_expression());
        while(cur.kind == TokenKind::Comma) { eat(); args.push_back(parse_expression()); }
    }
    expect(TokenKind::RParen, ")");
    auto e = make_unique<Expr>(name, move(args));
    return e;
}

// function literal syntax: (p1, p2) stmts... end
unique_ptr<Expr> Parser::parse_func_literal() {
    expect(TokenKind::LParen, "(");
    vector<string> params;
    if(cur.kind != TokenKind::RParen) {
        if(cur.kind == TokenKind::Identifier) {
            params.push_back(cur.text); eat();
            while(cur.kind == TokenKind::Comma) { eat(); if(cur.kind==TokenKind::Identifier){ params.push_back(cur.text); eat(); } else throw runtime_error("expected param name"); }
        }
    }
    expect(TokenKind::RParen, ")");
    // parse body until 'end'
    vector<unique_ptr<Stmt>> body;
    while(cur.kind != TokenKind::Kw_end) {
        body.push_back(parse_statement());
    }
    expect(TokenKind::Kw_end, "end");
    auto e = make_unique<Expr>();
    e->kind = Expr::KFuncLiteral;
    e->params = params;
    e->body = move(body);
    return e;
}
