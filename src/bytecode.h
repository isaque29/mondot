#ifndef MONDOT_BYTECODE_H
#define MONDOT_BYTECODE_H

#include "value.h"
#include <string>
#include <vector>
#include <unordered_map>

/*
 OpCodes estendidos para suportar:
  - PUSH/STORE local/const/global
  - CALL (host or dynamic)
  - POP (descartar retorno)
  - JMP / JMP_IF_FALSE
  - PUSH_GLOBAL / STORE_GLOBAL
  - basic helpers
*/

enum OpCode : uint8_t
{
    OP_NOP = 0,

    // pushing/loading
    OP_PUSH_CONST,   // a = const idx
    OP_PUSH_LOCAL,   // a = local idx
    OP_STORE_LOCAL,  // a = local idx (store top)
    OP_PUSH_GLOBAL,  // s = name
    OP_STORE_GLOBAL, // s = name

    // calls / fn
    OP_CALL,  // a = arg count, b: special (-1 host/global by name in .s, -2 dynamic callee on stack)
    OP_POP,   // a = count to pop

    // flow control
    OP_JMP,            // a = target ip or relative (we use absolute addresses)
    OP_JMP_IF_FALSE,   // a = target ip

    // original simpler ops (kept for compatibility)
    OP_PRINT,
    OP_SPAWN,
    OP_RET,
    OP_DROP,
    OP_LOAD_NUM, // legacy: keep for compatibility with older compiler paths
    OP_LOAD_STR,
    OP_LOAD_GLOBAL,
    OP_STORE_GLOBAL_LEGACY
};

struct Op
{
    OpCode op;
    int a;
    int b;
    std::string s;
    Op(OpCode o=OP_NOP,int A=0,int B=0): op(o), a(A), b(B){}
};

struct ByteFunc
{
    std::vector<Op> code;
    std::vector<Value> consts;
    std::vector<std::string> locals;
};

struct ByteModule
{
    std::string name;
    std::unordered_map<std::string,int> handler_index;
    std::vector<ByteFunc> funcs;
};

struct CompiledUnit
{
    ByteModule module;
};

#include "ast.h"

CompiledUnit compile_unit(UnitDecl *u);

#endif // MONDOT_BYTECODE_H
