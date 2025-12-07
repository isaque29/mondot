#ifndef MONDOT_BYTECODE_H
#define MONDOT_BYTECODE_H

#include "value.h"
#include <string>
#include <vector>
#include <unordered_map>

enum OpCode : uint8_t
{
    OP_NOP = 0,

    // pushing/loading
    OP_PUSH_CONST,   // a = const idx
    OP_PUSH_LOCAL,   // a = local idx
    OP_STORE_LOCAL,  // a = local idx (store top)

    //
    OP_ADD,
    OP_SUB,
    OP_LT,

    // calls / fn
    OP_CALL,  // a = arg count, b: special (-1 host/global by name in .s, -2 dynamic callee on stack)
    OP_POP,   // a = count to pop
    OP_RET,

    // flow control
    OP_JMP,            // a = target ip or relative (we use absolute addresses)
    OP_JMP_IF_FALSE,   // a = target ip
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

#endif