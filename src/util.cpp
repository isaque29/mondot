#include "util.h"

#include <cstdio>
#include <iostream>
#ifdef _WIN32
 #define WIN32_LEAN_AND_MEAN
 #include <windows.h>
#else
 #include <unistd.h>
#endif

bool DEBUG = false;
static bool TERM_SUPPORTS_COLOR = false;

void enable_terminal_colors()
{
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) { TERM_SUPPORTS_COLOR = false; return; }
    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode)) { TERM_SUPPORTS_COLOR = false; return; }
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
    if (!SetConsoleMode(hOut, dwMode)) {
        TERM_SUPPORTS_COLOR = false;
        return;
    }
    TERM_SUPPORTS_COLOR = true;
#else
    TERM_SUPPORTS_COLOR = isatty(fileno(stdout));
#endif
}

const char* COL_RESET() { return "\x1b[0m"; }
const char* COL_DARKGRAY() { return "\x1b[90m"; }
const char* COL_YELLOW() { return "\e[93m"; }
const char* COL_RED() { return "\x1b[31m"; }

void colored_fprintf(FILE* out, const char* color, const std::string &msg)
{
    if (TERM_SUPPORTS_COLOR && color)
    {
        fprintf(out, "%s%s%s\n", color, msg.c_str(), COL_RESET());
    }
    else
    {
        fprintf(out, "%s\n", msg.c_str());
    }
}

void dbg(const std::string &s)
{
    if(DEBUG) colored_fprintf(stderr, COL_DARKGRAY(), std::string("[dbg] ")+s);
}
void info(const std::string &s)
{
    if(DEBUG) colored_fprintf(stdout, COL_YELLOW(), std::string("[info] ")+s);
}
void errlog(const std::string &s)
{
    colored_fprintf(stderr, COL_RED(), std::string("[err] ")+s);
}
