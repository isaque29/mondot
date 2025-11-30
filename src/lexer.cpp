#include "lexer.h"
#include <cctype>

using namespace std;

Lexer::Lexer(const string &s): src(s) {}

char Lexer::peek() const { return i < src.size() ? src[i] : '\0'; }

char Lexer::get() {
    char c = peek();
    if(c == '\n') { line++; col = 1; }
    else col++;
    if(i < src.size()) i++;
    return c;
}

void Lexer::skip_ws()
{
    while(isspace((unsigned char)peek()))
        get();
}

Token Lexer::next()
{
    skip_ws();
    char c = peek();
    Token t; t.line = line; t.col = col;
    if(c == '\0')
    {
        t.kind = TokenKind::End;
        t.text = "";
        return t;
    }

    if(isalpha((unsigned char)c) || c == '_' )
    {
        string s;
        while(isalnum((unsigned char)peek()) || peek()=='_' || peek()=='.')
            s.push_back(get());

        if(s == "unit") t.kind = TokenKind::Kw_unit;
        else if(s == "on") t.kind = TokenKind::Kw_on;
        else if(s == "end") t.kind = TokenKind::Kw_end;
        else if(s == "local") t.kind = TokenKind::Kw_local;
        else if(s == "if") t.kind = TokenKind::Kw_if;
        else if(s == "elseif") t.kind = TokenKind::Kw_elseif;
        else if(s == "else") t.kind = TokenKind::Kw_else;
        else if(s == "while") t.kind = TokenKind::Kw_while;
        else if(s == "foreach") t.kind = TokenKind::Kw_foreach;
        else if(s == "in") t.kind = TokenKind::Kw_in;
        else if(s == "return") t.kind = TokenKind::Kw_return;
        else if(s == "true") t.kind = TokenKind::Boolean;
        else if(s == "false") t.kind = TokenKind::Boolean;
        else if(s == "nil") t.kind = TokenKind::Nil;
        else t.kind = TokenKind::Identifier;
        t.text = s;
        return t;
    }

    if(isdigit((unsigned char)c) || (c=='.' && i+1<src.size() && isdigit((unsigned char)src[i+1])))
    {
        string s; bool hasdot=false;
        while(isdigit((unsigned char)peek()) || peek()=='.')
        {
            char cc = get();
            if(cc=='.')
            {
                if(hasdot) break;
                hasdot = true;
            }
            s.push_back(cc);
        }
        t.kind = TokenKind::Number;
        t.text = s;
        return t;
    }

    if(c=='\'' || c=='\"')
    {
        char quote = get();
        string s;
        while(peek() != quote && peek() != '\0')
        {
            char cc = get();
            if(cc == '\\')
            {
                char nx = get();
                if(nx == 'n') s.push_back('\n');
                else s.push_back(nx);
            }
            else s.push_back(cc);
        }
        if(peek() == quote) get();
        t.kind = TokenKind::String;
        t.text = s;
        return t;
    }

    auto peek1 = [&]()->char { return (i+1 < src.size()) ? src[i+1] : '\0'; };

    char ch = get();
    switch(ch)
    {
        case '(': t.kind = TokenKind::LParen; break;
        case ')': t.kind = TokenKind::RParen; break;
        case '{': t.kind = TokenKind::LBrace; break;
        case '}': t.kind = TokenKind::RBrace; break;
        case ';': t.kind = TokenKind::Semicolon; break;
        case ',': t.kind = TokenKind::Comma; break;
        case '~': t.kind = TokenKind::Tilde; break;

        case '=':
            if(peek() == '=')
            {
                get();
                t.kind = TokenKind::EqualEqual;
                t.text = "==";
            }
            else t.kind = TokenKind::Equal;
            break;
        case '!':
            if(peek() == '=')
            {
                get();
                t.kind = TokenKind::NotEqual;
                t.text = "!=";
            }
            else t.kind = TokenKind::Exclamation;
            break;
        case '<':
            if(peek() == '<')
            {
                get();
                if(peek() == '=')
                {
                    get();
                    t.kind = TokenKind::ShiftLeftEqual;
                    t.text = "<<=";
                }
                else
                {
                    t.kind = TokenKind::ShiftLeft;
                    t.text = "<<";
                }
            }
            else if(peek() == '=')
            {
                get();
                t.kind = TokenKind::LessEqual;
                t.text = "<=";
            }
            else { t.kind = TokenKind::Less; }
            break;

        case '>':
            if(peek() == '>')
            {
                get();
                if(peek() == '=')
                {
                    get();
                    t.kind = TokenKind::ShiftRightEqual;
                    t.text = ">>=";
                }
                else
                {
                    t.kind = TokenKind::ShiftRight;
                    t.text = ">>";
                }
            }
            else if(peek() == '=')
            {
                get();
                t.kind = TokenKind::GreaterEqual;
                t.text = ">=";
            }
            else
                t.kind = TokenKind::Greater;
            break;

        case '&':
            if(peek() == '&')
            {
                get();
                t.kind = TokenKind::LogicalAnd;
                t.text = "&&";
            }
            else if(peek() == '=')
            {
                get();
                t.kind = TokenKind::AmpersandEqual;
                t.text = "&="; 
            }
            else t.kind = TokenKind::Ampersand;
            break;

        case '|':
            if(peek() == '|')
            {
                get();
                t.kind = TokenKind::LogicalOr;
                t.text = "||";
            }
            else if(peek() == '=')
            {
                get();
                t.kind = TokenKind::PipeEqual;
                t.text = "|=";
            }
            else t.kind = TokenKind::Pipe;
            break;

        case '^':
            if(peek() == '=')
            {
                get();
                t.kind = TokenKind::CaretEqual;
                t.text = "^=";
            }
            else t.kind = TokenKind::Caret;
            break;

        case '+':
            if(peek() == '+')
            {
                get();
                t.kind = TokenKind::PlusPlus;
                t.text = "++";
            }
            else if(peek() == '=')
            {
                get();
                t.kind = TokenKind::PlusEqual;
                t.text = "+=";
            }
            else t.kind = TokenKind::Plus;
            break;

        case '-':
            if(peek() == '-')
            {
                get();
                t.kind = TokenKind::MinusMinus;
                t.text = "--";
            }
            else if(peek() == '=')
            {
                get();
                t.kind = TokenKind::MinusEqual;
                t.text = "-=";
            }
            else if(peek() == '>')
            {
                get();
                t.kind = TokenKind::Arrow;
                t.text = "->";
            }
            else t.kind = TokenKind::Minus;
            break;

        case '*':
            if(peek() == '=')
            {
                get();
                t.kind = TokenKind::StarEqual;
                t.text = "*=";
            }
            else t.kind = TokenKind::Star;
            break;

        case '/':
            if(peek() == '=')
            {
                get();
                t.kind = TokenKind::SlashEqual;
                t.text = "/=";
            }
            else t.kind = TokenKind::Slash;
            break;

        case '%':
            if(peek() == '=')
            {
                get();
                t.kind = TokenKind::PercentEqual;
                t.text = "%=";
            }
            else t.kind = TokenKind::Percent;
            break;

        default:
            t.kind = TokenKind::End; t.text = string(1,ch); break;
    }
    return t;
}
