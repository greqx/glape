// Copyright (c) 2026 greqx and Contributors

#ifndef GLAPE_BYTECODE_H
#define GLAPE_BYTECODE_H

#include <stdint.h>

// ── instruction opcodes ────────────────────────────────────────────────────
// each opcode is 1 byte, operands follow inline

typedef enum {
    // stack
    OP_PUSH_INT,     // <int64>   push integer literal
    OP_PUSH_FLOAT,   // <double>  push float literal
    OP_PUSH_STR,     // <uint32>  push string from string table
    OP_PUSH_BOOL,    // <uint8>   push bool (0 or 1)
    OP_POP,          //           discard top of stack

    // variables
    OP_LOAD,         // <uint32>  push value of variable (name index)
    OP_STORE,        // <uint32>  pop and store into variable (name index)
    OP_STORE_IMMUT,  // <uint32>  same but marks slot as immutable

    // arithmetic
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,

    // comparison  (result pushed as bool)
    OP_EQ,
    OP_NEQ,
    OP_GT,
    OP_LT,
    OP_GTE,
    OP_LTE,

    // jumps (operand is signed int32 offset from current ip)
    OP_JUMP,         // <int32>   unconditional
    OP_JUMP_IF,      // <int32>   jump if top of stack is true  (pops)
    OP_JUMP_IFNOT,   // <int32>   jump if top of stack is false (pops)

    // functions
    OP_CALL,         // <uint32 name_idx> <uint8 argc>  call named function
    OP_CALL_LIB,     // <uint32 lib_idx>  <uint32 fn_idx> <uint8 argc>
    OP_RET,          //           return top of stack (or void)
    OP_RET_VOID,     //           return nothing

    // builtins
    OP_PRINT,        //           pop and print top of stack

    // control
    OP_HALT,         //           end of program

    OP__COUNT        // always last - total number of opcodes
} Opcode;

// ── value types used at runtime ────────────────────────────────────────────

typedef enum {
    VAL_INT,
    VAL_FLOAT,
    VAL_STR,
    VAL_BOOL,
    VAL_VOID,
} ValType;

typedef struct {
    ValType type;
    union {
        int64_t  ival;
        double   fval;
        char    *sval;
        int      bval;
    };
} Value;

// ── compiled function ──────────────────────────────────────────────────────

typedef struct {
    char     *name;
    uint32_t  param_count;
    char    **param_names; // name of each param, in order
    uint8_t  *code;        // raw bytecode
    uint32_t  code_len;
    uint32_t  code_cap;
} GlapeFunc;

// ── compiled chunk (one script or one .gdl library) ───────────────────────

typedef struct {
    // string table - all string literals and names
    char    **strings;
    uint32_t  string_count;
    uint32_t  string_cap;

    // function table
    GlapeFunc *funcs;
    uint32_t   func_count;
    uint32_t   func_cap;

    // top-level code (outside any function)
    uint8_t  *code;
    uint32_t  code_len;
    uint32_t  code_cap;
} Chunk;

// ── chunk helpers ──────────────────────────────────────────────────────────

Chunk    *chunk_new(void);
void      chunk_free(Chunk *c);

// add a string to the string table, return its index
uint32_t  chunk_add_string(Chunk *c, const char *s);

// find existing string index or add it
uint32_t  chunk_intern(Chunk *c, const char *s);

// emit one byte into a code buffer
void      chunk_emit(uint8_t **code, uint32_t *len, uint32_t *cap, uint8_t byte);

// emit into top-level code
void      chunk_emit_top(Chunk *c, uint8_t byte);

// emit into a specific function
void      chunk_emit_func(Chunk *c, uint32_t func_idx, uint8_t byte);

// add a function, return its index
uint32_t  chunk_add_func(Chunk *c, const char *name);

// opcode name for debug
const char *opcode_name(Opcode op);

#endif
