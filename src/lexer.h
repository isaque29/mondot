#ifndef MONDOT_LEXER_H
#define MONDOT_LEXER_H

#include <string>

enum class TokenKind
{
    End,
    Identifier,
    Boolean,
    Number,
    Nil,
    String,
    Kw_unit,
    Kw_on,
    Kw_end,
    Kw_local,
    Kw_if,
    Kw_elseif,
    Kw_else,
    Kw_while,
    Kw_foreach,
    Kw_in,
    Kw_return,
    Arrow,
    LParen,
    RParen,
    LBrace,
    RBrace,
    Equal,
    Semicolon,
    Comma,

    Plus,
    Minus,
    Star,
    Slash,
    Exclamation,
    Ampersand,
    Pipe,
    EqualEqual,
    NotEqual,
    ShiftLeftEqual,
    ShiftLeft,
    LessEqual,
    Less,
    Greater,
    GreaterEqual,
    ShiftRight,
    ShiftRightEqual,
    LogicalAnd,
    AmpersandEqual,
    LogicalOr,
    PipeEqual,
    MinusMinus,
    PlusPlus,
    PlusEqual,
    Caret,
    CaretEqual,
    StarEqual,
    SlashEqual,
    MinusEqual,
    Percent,
    PercentEqual,
    Tilde,
    TildeEqual,
    
};

struct Token
{
    TokenKind kind;
    std::string text;
    int line;
    int col;
};

struct Lexer
{
    std::string src;
    size_t i = 0;
    int line = 1;
    int col = 1;
    Lexer() = default;
    Lexer(const std::string &s);
    char peek() const;
    char get();
    void skip_ws();
    Token next();
};

#endif
