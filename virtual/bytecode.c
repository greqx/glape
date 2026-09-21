// Copyright (c) 2026 greqx and Contributors

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bytecode.h"

// ── chunk ──────────────────────────────────────────────────────────────────

Chunk *chunk_new(void) {
    Chunk *c = calloc(1, sizeof(Chunk));
    c->string_cap = 16;
    c->strings    = malloc(sizeof(char *) * c->string_cap);
    c->func_cap   = 8;
    c->funcs      = malloc(sizeof(GlapeFunc) * c->func_cap);
    c->code_cap   = 64;
    c->code       = malloc(c->code_cap);
    return c;
}

void chunk_free(Chunk *c) {
    if (!c) return;
    for (uint32_t i = 0; i < c->string_count; i++) free(c->strings[i]);
    free(c->strings);
    for (uint32_t i = 0; i < c->func_count; i++) {
        free(c->funcs[i].name);
        for (uint32_t j = 0; j < c->funcs[i].param_count; j++)
            free(c->funcs[i].param_names[j]);
        free(c->funcs[i].param_names);
        free(c->funcs[i].code);
    }
    free(c->funcs);
    free(c->code);
    free(c);
}

// ── string table ───────────────────────────────────────────────────────────

uint32_t chunk_add_string(Chunk *c, const char *s) {
    if (c->string_count >= c->string_cap) {
        c->string_cap *= 2;
        c->strings = realloc(c->strings, sizeof(char *) * c->string_cap);
    }
    c->strings[c->string_count] = strdup(s);
    return c->string_count++;
}

uint32_t chunk_intern(Chunk *c, const char *s) {
    for (uint32_t i = 0; i < c->string_count; i++)
        if (strcmp(c->strings[i], s) == 0) return i;
    return chunk_add_string(c, s);
}

// ── emit helpers ───────────────────────────────────────────────────────────

void chunk_emit(uint8_t **code, uint32_t *len, uint32_t *cap, uint8_t byte) {
    if (*len >= *cap) {
        *cap *= 2;
        *code = realloc(*code, *cap);
    }
    (*code)[(*len)++] = byte;
}

void chunk_emit_top(Chunk *c, uint8_t byte) {
    chunk_emit(&c->code, &c->code_len, &c->code_cap, byte);
}

void chunk_emit_func(Chunk *c, uint32_t fi, uint8_t byte) {
    GlapeFunc *f = &c->funcs[fi];
    chunk_emit(&f->code, &f->code_len, &f->code_cap, byte);
}

// ── functions ──────────────────────────────────────────────────────────────

uint32_t chunk_add_func(Chunk *c, const char *name) {
    if (c->func_count >= c->func_cap) {
        c->func_cap *= 2;
        c->funcs = realloc(c->funcs, sizeof(GlapeFunc) * c->func_cap);
    }
    GlapeFunc *f  = &c->funcs[c->func_count];
    f->name        = strdup(name);
    f->param_count = 0;
    f->param_names = NULL;
    f->code_cap    = 64;
    f->code_len    = 0;
    f->code        = malloc(f->code_cap);
    return c->func_count++;
}

// ── debug ──────────────────────────────────────────────────────────────────

const char *opcode_name(Opcode op) {
    switch (op) {
        case OP_PUSH_INT:    return "PUSH_INT";
        case OP_PUSH_FLOAT:  return "PUSH_FLOAT";
        case OP_PUSH_STR:    return "PUSH_STR";
        case OP_PUSH_BOOL:   return "PUSH_BOOL";
        case OP_POP:         return "POP";
        case OP_LOAD:        return "LOAD";
        case OP_STORE:       return "STORE";
        case OP_STORE_IMMUT: return "STORE_IMMUT";
        case OP_ADD:         return "ADD";
        case OP_SUB:         return "SUB";
        case OP_MUL:         return "MUL";
        case OP_DIV:         return "DIV";
        case OP_EQ:          return "EQ";
        case OP_NEQ:         return "NEQ";
        case OP_GT:          return "GT";
        case OP_LT:          return "LT";
        case OP_GTE:         return "GTE";
        case OP_LTE:         return "LTE";
        case OP_JUMP:        return "JUMP";
        case OP_JUMP_IF:     return "JUMP_IF";
        case OP_JUMP_IFNOT:  return "JUMP_IFNOT";
        case OP_CALL:        return "CALL";
        case OP_CALL_LIB:    return "CALL_LIB";
        case OP_RET:         return "RET";
        case OP_RET_VOID:    return "RET_VOID";
        case OP_PRINT:       return "PRINT";
        case OP_HALT:        return "HALT";
        default:             return "?";
    }
}
