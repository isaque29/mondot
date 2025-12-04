#ifndef MONDOT_UTIL_H
#define MONDOT_UTIL_H

#include <string>
#include <cstdio>

#include "lexer.h"
#include "ast.h"
#include "module.h"

#ifndef MONDOT_DEBUG
  #ifdef NDEBUG
    #define MONDOT_DEBUG 0
  #else
    #define MONDOT_DEBUG 1
  #endif
#endif

void enable_terminal_colors();

void colored_fprintf(FILE* out, const char* color, const std::string &msg);
#if MONDOT_DEBUG
void dbg(const std::string &s);
void info(const std::string &s);
#else
inline void dbg(const std::string &) {}
inline void info(const std::string &) {}
#endif
void errlog(const std::string &s);
constexpr inline const char* token_kind_to_string(TokenKind k)
{
    switch(k)
    {
        case TokenKind::End: return "End";
        case TokenKind::Identifier: return "Identifier";
        case TokenKind::Number: return "Number";
        case TokenKind::String: return "String";
        case TokenKind::Boolean: return "Boolean";
        case TokenKind::Nil: return "Nil";

        case TokenKind::Kw_unit: return "Kw_unit";
        case TokenKind::Kw_on: return "Kw_on";
        case TokenKind::Kw_end: return "Kw_end";
        case TokenKind::Kw_if: return "Kw_if";
        case TokenKind::Kw_else: return "Kw_else";
        case TokenKind::Kw_elseif: return "Kw_elseif";
        case TokenKind::Kw_while: return "Kw_while";
        case TokenKind::Kw_foreach: return "Kw_foreach";
        case TokenKind::Kw_in: return "Kw_in";
        case TokenKind::Kw_return: return "Kw_return";
        case TokenKind::Kw_local: return "Kw_local";

        case TokenKind::LParen: return "LParen";
        case TokenKind::RParen: return "RParen";
        case TokenKind::LBrace: return "LBrace";
        case TokenKind::RBrace: return "RBrace";
        case TokenKind::LBracket: return "LBracket";
        case TokenKind::RBracket: return "RBracket";
        case TokenKind::Comma: return "Comma";
        case TokenKind::Semicolon: return "Semicolon";

        case TokenKind::Plus: return "Plus";
        case TokenKind::Minus: return "Minus";
        case TokenKind::Star: return "Star";
        case TokenKind::Slash: return "Slash";
        case TokenKind::Percent: return "Percent";

        case TokenKind::PlusPlus: return "PlusPlus";
        case TokenKind::MinusMinus: return "MinusMinus";

        case TokenKind::Equal: return "Equal";
        case TokenKind::EqualEqual: return "EqualEqual";
        case TokenKind::NotEqual: return "NotEqual";

        case TokenKind::Less: return "Less";
        case TokenKind::LessEqual: return "LessEqual";
        case TokenKind::Greater: return "Greater";
        case TokenKind::GreaterEqual: return "GreaterEqual";

        case TokenKind::Ampersand: return "And";
        case TokenKind::Pipe: return "Or";
        case TokenKind::Exclamation: return "Exclamation";

        case TokenKind::Arrow: return "Arrow";

        default: return "UnknownTokenKind";
    }
}
#if MONDOT_DEBUG
void dump_program_tokens(const Program* p, FILE* out = stdout);
void dump_module_bytecode(const Module* m, FILE* out = stdout);
#else
inline void dump_program_tokens(const Program*, FILE* = stdout) {}
inline void dump_module_bytecode(const Module*, FILE* = stdout) {}
#endif

#endif
