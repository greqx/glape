// Copyright (c) 2026 greqx and Contributors

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler.h"

#define COLOR_RED "\033[91m"
#define COLOR_WHITE "\033[97m"
#define COLOR_GREY "\033[90m"
#define COLOR_RESET "\033[0m"

// ── compiler state ─────────────────────────────────────────────────────────

typedef struct {
    Chunk *chunk;
    int in_func; // are we inside a function body?
    uint32_t func_idx; // current function index
} Compiler;

static void comp_error(const char *msg, int line, int col) {
    fprintf(stderr,
        COLOR_RED "error:" COLOR_RESET
        COLOR_WHITE " %s " COLOR_RESET
        COLOR_GREY "(%d:%d)\n" COLOR_RESET,
        msg, line, col);
    exit(1);
}

// ── emit helpers ───────────────────────────────────────────────────────────
// all emit_* functions write into either top-level or current function

static void emit(Compiler *c, uint8_t byte) {
    if (c->in_func)
        chunk_emit_func(c->chunk, c->func_idx, byte);
    else
        chunk_emit_top(c->chunk, byte);
}

// emit a uint8
static void emit_u8(Compiler *c, uint8_t v) {
    emit(c, v);
}

// emit a uint32 little-endian
static void emit_u32(Compiler *c, uint32_t v) {
    emit(c, (v ) & 0xff);
    emit(c, (v >> 8) & 0xff);
    emit(c, (v >> 16) & 0xff);
    emit(c, (v >> 24) & 0xff);
}

// emit a int32 little-endian (for jump offsets)
static void emit_i32(Compiler *c, int32_t v) {
    emit_u32(c, (uint32_t)v);
}

// emit a double (8 bytes)
static void emit_f64(Compiler *c, double v) {
    uint8_t buf[8];
    memcpy(buf, &v, 8);
    for (int i = 0; i < 8; i++) emit(c, buf[i]);
}

// emit a int64 (8 bytes)
static void emit_i64(Compiler *c, int64_t v) {
    uint8_t buf[8];
    memcpy(buf, &v, 8);
    for (int i = 0; i < 8; i++) emit(c, buf[i]);
}

// returns current code length (for patching jumps)
static uint32_t current_offset(Compiler *c) {
    if (c->in_func)
        return c->chunk->funcs[c->func_idx].code_len;
    return c->chunk->code_len;
}

// patch a int32 at a given offset in current code buffer
static void patch_i32(Compiler *c, uint32_t offset, int32_t v) {
    uint8_t *code = c->in_func
        ? c->chunk->funcs[c->func_idx].code
        : c->chunk->code;
    code[offset ] = (v ) & 0xff;
    code[offset + 1] = (v >> 8) & 0xff;
    code[offset + 2] = (v >> 16) & 0xff;
    code[offset + 3] = (v >> 24) & 0xff;
}

// ── forward declaration ────────────────────────────────────────────────────

static void compile_expr(Compiler *c, Node *n);
static void compile_stmt(Compiler *c, Node *n);

// ── expression compiler ────────────────────────────────────────────────────

static void compile_expr(Compiler *c, Node *n) {
    switch (n->type) {
        case NODE_INT_LIT:
            emit(c, OP_PUSH_INT);
            emit_i64(c, n->int_lit.val);
            break;

        case NODE_FLOAT_LIT:
            emit(c, OP_PUSH_FLOAT);
            emit_f64(c, n->float_lit.val);
            break;

        case NODE_STR_LIT: {
            uint32_t idx = chunk_intern(c->chunk, n->str_lit.val);
            emit(c, OP_PUSH_STR);
            emit_u32(c, idx);
            break;
        }

        case NODE_BOOL_LIT:
            emit(c, OP_PUSH_BOOL);
            emit_u8(c, n->bool_lit.val ? 1 : 0);
            break;

        case NODE_IDENT: {
            uint32_t idx = chunk_intern(c->chunk, n->ident.name);
            emit(c, OP_LOAD);
            emit_u32(c, idx);
            break;
        }

        case NODE_BINARY: {
            compile_expr(c, n->binary.left);
            compile_expr(c, n->binary.right);
            switch (n->binary.op) {
                case TOK_PLUS: emit(c, OP_ADD); break;
                case TOK_MINUS: emit(c, OP_SUB); break;
                case TOK_STAR: emit(c, OP_MUL); break;
                case TOK_SLASH: emit(c, OP_DIV); break;
                case TOK_EQ: emit(c, OP_EQ); break;
                case TOK_NEQ: emit(c, OP_NEQ); break;
                case TOK_GT: emit(c, OP_GT); break;
                case TOK_LT: emit(c, OP_LT); break;
                case TOK_GTE: emit(c, OP_GTE); break;
                case TOK_LTE: emit(c, OP_LTE); break;
                default:
                    comp_error("unknown binary operator", n->line, n->col);
            }
            break;
        }

        case NODE_CALL_EXPR: {
            // push args left to right
            for (int i = 0; i < n->call.arg_count; i++)
                compile_expr(c, n->call.args[i]);

            if (n->call.object) {
                // lib.func() call
                uint32_t lib_idx = chunk_intern(c->chunk, n->call.object);
                uint32_t fn_idx = chunk_intern(c->chunk, n->call.name);
                emit(c, OP_CALL_LIB);
                emit_u32(c, lib_idx);
                emit_u32(c, fn_idx);
                emit_u8(c, (uint8_t)n->call.arg_count);
            } else if (strcmp(n->call.name, "print") == 0) {
                emit(c, OP_PRINT);
            } else {
                uint32_t fn_idx = chunk_intern(c->chunk, n->call.name);
                emit(c, OP_CALL);
                emit_u32(c, fn_idx);
                emit_u8(c, (uint8_t)n->call.arg_count);
            }
            break;
        }

        default:
            comp_error("unsupported expression node", n->line, n->col);
    }
}

// ── statement compiler ─────────────────────────────────────────────────────

static void compile_stmt(Compiler *c, Node *n) {
    switch (n->type) {
        case NODE_IMPORT:
            // handled at runtime by VM
            break;

        case NODE_VAR_DECL: {
            compile_expr(c, n->var_decl.value);
            uint32_t idx = chunk_intern(c->chunk, n->var_decl.name);
            emit(c, n->var_decl.immut ? OP_STORE_IMMUT : OP_STORE);
            emit_u32(c, idx);
            break;
        }

        case NODE_ASSIGN: {
            compile_expr(c, n->assign.value);
            uint32_t idx = chunk_intern(c->chunk, n->assign.name);
            emit(c, OP_STORE);
            emit_u32(c, idx);
            break;
        }

        case NODE_IF: {
            // compile condition
            compile_expr(c, n->if_stmt.cond);

            // emit JUMP_IFNOT with placeholder offset
            emit(c, OP_JUMP_IFNOT);
            uint32_t patch_else = current_offset(c);
            emit_i32(c, 0);

            // compile body
            for (int i = 0; i < n->if_stmt.body_count; i++)
                compile_stmt(c, n->if_stmt.body[i]);

            if (n->if_stmt.else_count > 0) {
                // jump over else
                emit(c, OP_JUMP);
                uint32_t patch_end = current_offset(c);
                emit_i32(c, 0);

                // patch the JUMP_IFNOT to here
                int32_t else_offset = (int32_t)current_offset(c) - (int32_t)(patch_else + 4);
                patch_i32(c, patch_else, else_offset);

                // compile else body
                for (int i = 0; i < n->if_stmt.else_count; i++)
                    compile_stmt(c, n->if_stmt.else_body[i]);

                // patch the JUMP to here
                int32_t end_offset = (int32_t)current_offset(c) - (int32_t)(patch_end + 4);
                patch_i32(c, patch_end, end_offset);
            } else {
                // patch the JUMP_IFNOT to here
                int32_t end_offset = (int32_t)current_offset(c) - (int32_t)(patch_else + 4);
                patch_i32(c, patch_else, end_offset);
            }
            break;
        }

        case NODE_LOOP_INFINITE: {
            uint32_t loop_start = current_offset(c);
            for (int i = 0; i < n->loop_inf.count; i++)
                compile_stmt(c, n->loop_inf.body[i]);
            // jump back to start
            emit(c, OP_JUMP);
            int32_t back = (int32_t)loop_start - (int32_t)(current_offset(c) + 4);
            emit_i32(c, back);
            break;
        }

        case NODE_LOOP_WHILE: {
            uint32_t loop_start = current_offset(c);
            compile_expr(c, n->loop_while.cond);

            emit(c, OP_JUMP_IFNOT);
            uint32_t patch_end = current_offset(c);
            emit_i32(c, 0);

            for (int i = 0; i < n->loop_while.count; i++)
                compile_stmt(c, n->loop_while.body[i]);

            // jump back
            emit(c, OP_JUMP);
            int32_t back = (int32_t)loop_start - (int32_t)(current_offset(c) + 4);
            emit_i32(c, back);

            // patch exit
            int32_t end_offset = (int32_t)current_offset(c) - (int32_t)(patch_end + 4);
            patch_i32(c, patch_end, end_offset);
            break;
        }

        case NODE_LOOP_RANGE: {
            // init loop var
            compile_expr(c, n->loop_range.from);
            uint32_t var_idx = chunk_intern(c->chunk, n->loop_range.var);
            emit(c, OP_STORE);
            emit_u32(c, var_idx);

            uint32_t loop_start = current_offset(c);

            // condition: var < to (exclusive) or var <= to (inclusive)
            emit(c, OP_LOAD);
            emit_u32(c, var_idx);
            compile_expr(c, n->loop_range.to);
            emit(c, n->loop_range.inclusive ? OP_LTE : OP_LT);

            emit(c, OP_JUMP_IFNOT);
            uint32_t patch_end = current_offset(c);
            emit_i32(c, 0);

            // body
            for (int i = 0; i < n->loop_range.count; i++)
                compile_stmt(c, n->loop_range.body[i]);

            // increment: var = var + 1
            emit(c, OP_LOAD);
            emit_u32(c, var_idx);
            emit(c, OP_PUSH_INT);
            emit_i64(c, 1);
            emit(c, OP_ADD);
            emit(c, OP_STORE);
            emit_u32(c, var_idx);

            // jump back
            emit(c, OP_JUMP);
            int32_t back = (int32_t)loop_start - (int32_t)(current_offset(c) + 4);
            emit_i32(c, back);

            // patch exit
            int32_t end_offset = (int32_t)current_offset(c) - (int32_t)(patch_end + 4);
            patch_i32(c, patch_end, end_offset);
            break;
        }

        case NODE_FUNC_DEF: {
            uint32_t fi = chunk_add_func(c->chunk, n->func_def.name);
            GlapeFunc *f = &c->chunk->funcs[fi];

            // store param names
            f->param_count = n->func_def.param_count;
            f->param_names = malloc(sizeof(char *) * f->param_count);
            for (uint32_t i = 0; i < f->param_count; i++)
                f->param_names[i] = strdup(n->func_def.params[i].name);

            // compile body inside function context
            int saved_in_func = c->in_func;
            uint32_t saved_fi = c->func_idx;
            c->in_func = 1;
            c->func_idx = fi;

            for (int i = 0; i < n->func_def.body_count; i++)
                compile_stmt(c, n->func_def.body[i]);

            // ensure function ends with RET_VOID
            emit(c, OP_RET_VOID);

            c->in_func = saved_in_func;
            c->func_idx = saved_fi;
            break;
        }

        case NODE_RETURN:
            if (n->ret.value) {
                compile_expr(c, n->ret.value);
                emit(c, OP_RET);
            } else {
                emit(c, OP_RET_VOID);
            }
            break;

        case NODE_CALL_STMT: {
            // push args
            for (int i = 0; i < n->call.arg_count; i++)
                compile_expr(c, n->call.args[i]);

            if (n->call.object) {
                uint32_t lib_idx = chunk_intern(c->chunk, n->call.object);
                uint32_t fn_idx = chunk_intern(c->chunk, n->call.name);
                emit(c, OP_CALL_LIB);
                emit_u32(c, lib_idx);
                emit_u32(c, fn_idx);
                emit_u8(c, (uint8_t)n->call.arg_count);
            } else if (strcmp(n->call.name, "print") == 0) {
                emit(c, OP_PRINT);
            } else {
                uint32_t fn_idx = chunk_intern(c->chunk, n->call.name);
                emit(c, OP_CALL);
                emit_u32(c, fn_idx);
                emit_u8(c, (uint8_t)n->call.arg_count);
            }
            // discard return value from call statement
            emit(c, OP_POP);
            break;
        }

        default:
            comp_error("unsupported statement node", n->line, n->col);
    }
}

// ── public api ─────────────────────────────────────────────────────────────

Chunk *compile(Node *program) {
    if (program->type != NODE_PROGRAM) return NULL;

    Compiler c;
    c.chunk = chunk_new();
    c.in_func = 0;
    c.func_idx = 0;

    for (int i = 0; i < program->program.count; i++)
        compile_stmt(&c, program->program.stmts[i]);

    emit(&c, OP_HALT);
    return c.chunk;
}

// ── disassembler ───────────────────────────────────────────────────────────

static void disasm_code(Chunk *chunk, uint8_t *code, uint32_t len, const char *label) {
    printf("=== %s ===\n", label);
    uint32_t ip = 0;
    while (ip < len) {
        Opcode op = (Opcode)code[ip];
        printf("  %04u  %s", ip, opcode_name(op));
        ip++;
        switch (op) {
            case OP_PUSH_INT: {
                int64_t v; memcpy(&v, &code[ip], 8); ip += 8;
                printf("  %lld", (long long)v);
                break;
            }
            case OP_PUSH_FLOAT: {
                double v; memcpy(&v, &code[ip], 8); ip += 8;
                printf("  %g", v);
                break;
            }
            case OP_PUSH_STR:
            case OP_LOAD:
            case OP_STORE:
            case OP_STORE_IMMUT: {
                uint32_t idx; memcpy(&idx, &code[ip], 4); ip += 4;
                printf("  %u (%s)", idx,
                    idx < chunk->string_count ? chunk->strings[idx] : "?");
                break;
            }
            case OP_PUSH_BOOL: {
                uint8_t v = code[ip++];
                printf("  %s", v ? "true" : "false");
                break;
            }
            case OP_JUMP:
            case OP_JUMP_IF:
            case OP_JUMP_IFNOT: {
                int32_t off; memcpy(&off, &code[ip], 4); ip += 4;
                printf("  %+d -> %u", off, ip + off);
                break;
            }
            case OP_CALL: {
                uint32_t fi; memcpy(&fi, &code[ip], 4); ip += 4;
                uint8_t ac = code[ip++];
                printf("  %u (%s)  argc=%u", fi,
                    fi < chunk->string_count ? chunk->strings[fi] : "?", ac);
                break;
            }
            case OP_CALL_LIB: {
                uint32_t li; memcpy(&li, &code[ip], 4); ip += 4;
                uint32_t fi; memcpy(&fi, &code[ip], 4); ip += 4;
                uint8_t ac = code[ip++];
                printf("  %s.%s  argc=%u",
                    li < chunk->string_count ? chunk->strings[li] : "?",
                    fi < chunk->string_count ? chunk->strings[fi] : "?", ac);
                break;
            }
            default: break;
        }
        printf("\n");
    }
}

void chunk_disasm(Chunk *c) {
    printf("strings: %u\n", c->string_count);
    for (uint32_t i = 0; i < c->string_count; i++)
        printf("  [%u] \"%s\"\n", i, c->strings[i]);
    printf("\n");

    disasm_code(c, c->code, c->code_len, "top-level");

    for (uint32_t i = 0; i < c->func_count; i++) {
        GlapeFunc *f = &c->funcs[i];
        char label[128];
        snprintf(label, sizeof(label), "fn %s", f->name);
        printf("\n");
        disasm_code(c, f->code, f->code_len, label);
    }
}
