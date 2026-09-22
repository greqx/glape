// Copyright (c) 2026 greqx and Contributors

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "vm.h"
#include "gdl.h"
#include "gffi.h"

#define COLOR_RED   "\033[91m"
#define COLOR_WHITE "\033[97m"
#define COLOR_GREY  "\033[90m"
#define COLOR_RESET "\033[0m"

static void vm_error(const char *msg) {
    fprintf(stderr,
        COLOR_RED "error:" COLOR_RESET
        COLOR_WHITE " %s\n" COLOR_RESET, msg);
    exit(1);
}

// ── scope ──────────────────────────────────────────────────────────────────

static Scope *scope_new(Scope *parent) {
    Scope *s  = calloc(1, sizeof(Scope));
    s->cap    = 8;
    s->slots  = malloc(sizeof(Slot) * s->cap);
    s->parent = parent;
    return s;
}

static void scope_free(Scope *s) {
    free(s->slots);
    free(s);
}

static Slot *scope_lookup(Scope *s, const char *name) {
    for (Scope *cur = s; cur; cur = cur->parent)
        for (int i = 0; i < cur->count; i++)
            if (strcmp(cur->slots[i].name, name) == 0)
                return &cur->slots[i];
    return NULL;
}

static void scope_set(Scope *s, const char *name, Value val, int immut) {
    // update existing in any scope
    for (Scope *cur = s; cur; cur = cur->parent)
        for (int i = 0; i < cur->count; i++)
            if (strcmp(cur->slots[i].name, name) == 0) {
                if (cur->slots[i].immut) {
                    char msg[128];
                    snprintf(msg, sizeof(msg),
                        "cannot assign to immutable variable '%s'", name);
                    vm_error(msg);
                }
                cur->slots[i].val = val;
                return;
            }
    // new slot in current scope
    if (s->count >= s->cap) {
        s->cap *= 2;
        s->slots = realloc(s->slots, sizeof(Slot) * s->cap);
    }
    s->slots[s->count].name  = (char *)name; // string from chunk, stable
    s->slots[s->count].val   = val;
    s->slots[s->count].immut = immut;
    s->count++;
}

// ── stack ──────────────────────────────────────────────────────────────────

static void push(VM *vm, Value v) {
    if (vm->stack_top >= VM_STACK_MAX)
        vm_error("stack overflow");
    vm->stack[vm->stack_top++] = v;
}

static Value pop(VM *vm) {
    if (vm->stack_top <= 0)
        vm_error("stack underflow");
    return vm->stack[--vm->stack_top];
}

// ── value helpers ──────────────────────────────────────────────────────────

static Value val_int(int64_t v)    { Value r; r.type=VAL_INT;   r.ival=v; return r; }
static Value val_float(double v)   { Value r; r.type=VAL_FLOAT; r.fval=v; return r; }
static Value val_bool(int v)       { Value r; r.type=VAL_BOOL;  r.bval=v; return r; }
static Value val_void(void)        { Value r; r.type=VAL_VOID;            return r; }
static Value val_str(char *s)      { Value r; r.type=VAL_STR;   r.sval=s; return r; }

static int is_truthy(Value v) {
    switch (v.type) {
        case VAL_BOOL:  return v.bval;
        case VAL_INT:   return v.ival != 0;
        case VAL_FLOAT: return v.fval != 0.0;
        case VAL_STR:   return v.sval && v.sval[0];
        default:        return 0;
    }
}

static void print_value(Value v) {
    switch (v.type) {
        case VAL_INT:   printf("%lld\n", (long long)v.ival); break;
        case VAL_FLOAT: printf("%g\n",   v.fval);            break;
        case VAL_STR:   printf("%s\n",   v.sval);            break;
        case VAL_BOOL:  printf("%s\n",   v.bval?"true":"false"); break;
        case VAL_VOID:  break;
    }
    fflush(stdout);
}

// ── read helpers (advance ip) ──────────────────────────────────────────────

static uint8_t read_u8(uint8_t *code, uint32_t *ip) {
    return code[(*ip)++];
}

static uint32_t read_u32(uint8_t *code, uint32_t *ip) {
    uint32_t v;
    memcpy(&v, &code[*ip], 4);
    *ip += 4;
    return v;
}

static int32_t read_i32(uint8_t *code, uint32_t *ip) {
    int32_t v;
    memcpy(&v, &code[*ip], 4);
    *ip += 4;
    return v;
}

static int64_t read_i64(uint8_t *code, uint32_t *ip) {
    int64_t v;
    memcpy(&v, &code[*ip], 8);
    *ip += 8;
    return v;
}

static double read_f64(uint8_t *code, uint32_t *ip) {
    double v;
    memcpy(&v, &code[*ip], 8);
    *ip += 8;
    return v;
}

// ── execution engine ───────────────────────────────────────────────────────

static Value exec(VM *vm, uint8_t *code, uint32_t len, Scope *scope) {
    uint32_t ip = 0;

    while (ip < len) {
        Opcode op = (Opcode)read_u8(code, &ip);

        switch (op) {
            case OP_PUSH_INT:
                push(vm, val_int(read_i64(code, &ip)));
                break;

            case OP_PUSH_FLOAT:
                push(vm, val_float(read_f64(code, &ip)));
                break;

            case OP_PUSH_STR: {
                uint32_t idx = read_u32(code, &ip);
                push(vm, val_str(vm->chunk->strings[idx]));
                break;
            }

            case OP_PUSH_BOOL:
                push(vm, val_bool(read_u8(code, &ip)));
                break;

            case OP_POP:
                if (vm->stack_top > 0) pop(vm);
                break;

            case OP_LOAD: {
                uint32_t idx = read_u32(code, &ip);
                const char *name = vm->chunk->strings[idx];
                Slot *slot = scope_lookup(scope, name);
                if (!slot) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "undefined variable '%s'", name);
                    vm_error(msg);
                }
                push(vm, slot->val);
                break;
            }

            case OP_STORE:
            case OP_STORE_IMMUT: {
                uint32_t idx   = read_u32(code, &ip);
                const char *name = vm->chunk->strings[idx];
                Value v        = pop(vm);
                scope_set(scope, name, v, op == OP_STORE_IMMUT);
                break;
            }

            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
            case OP_EQ:  case OP_NEQ: case OP_GT:  case OP_LT:
            case OP_GTE: case OP_LTE: {
                Value r = pop(vm);
                Value l = pop(vm);
                // promote int to float if needed
                if (l.type == VAL_INT && r.type == VAL_FLOAT)
                    l = val_float((double)l.ival);
                if (l.type == VAL_FLOAT && r.type == VAL_INT)
                    r = val_float((double)r.ival);

                switch (op) {
                    case OP_ADD:
                        if (l.type==VAL_INT)   push(vm, val_int(l.ival + r.ival));
                        else                   push(vm, val_float(l.fval + r.fval));
                        break;
                    case OP_SUB:
                        if (l.type==VAL_INT)   push(vm, val_int(l.ival - r.ival));
                        else                   push(vm, val_float(l.fval - r.fval));
                        break;
                    case OP_MUL:
                        if (l.type==VAL_INT)   push(vm, val_int(l.ival * r.ival));
                        else                   push(vm, val_float(l.fval * r.fval));
                        break;
                    case OP_DIV:
                        if (l.type==VAL_INT) {
                            if (r.ival==0) vm_error("division by zero");
                            push(vm, val_int(l.ival / r.ival));
                        } else {
                            if (r.fval==0.0) vm_error("division by zero");
                            push(vm, val_float(l.fval / r.fval));
                        }
                        break;
                    case OP_EQ:
                        if (l.type==VAL_INT)   push(vm, val_bool(l.ival == r.ival));
                        else if (l.type==VAL_FLOAT) push(vm, val_bool(l.fval == r.fval));
                        else if (l.type==VAL_BOOL)  push(vm, val_bool(l.bval == r.bval));
                        else push(vm, val_bool(strcmp(l.sval, r.sval)==0));
                        break;
                    case OP_NEQ:
                        if (l.type==VAL_INT)   push(vm, val_bool(l.ival != r.ival));
                        else if (l.type==VAL_FLOAT) push(vm, val_bool(l.fval != r.fval));
                        else if (l.type==VAL_BOOL)  push(vm, val_bool(l.bval != r.bval));
                        else push(vm, val_bool(strcmp(l.sval, r.sval)!=0));
                        break;
                    case OP_GT:
                        if (l.type==VAL_INT) push(vm, val_bool(l.ival > r.ival));
                        else push(vm, val_bool(l.fval > r.fval));
                        break;
                    case OP_LT:
                        if (l.type==VAL_INT) push(vm, val_bool(l.ival < r.ival));
                        else push(vm, val_bool(l.fval < r.fval));
                        break;
                    case OP_GTE:
                        if (l.type==VAL_INT) push(vm, val_bool(l.ival >= r.ival));
                        else push(vm, val_bool(l.fval >= r.fval));
                        break;
                    case OP_LTE:
                        if (l.type==VAL_INT) push(vm, val_bool(l.ival <= r.ival));
                        else push(vm, val_bool(l.fval <= r.fval));
                        break;
                    default: break;
                }
                break;
            }

            case OP_JUMP: {
                int32_t off = read_i32(code, &ip);
                ip = (uint32_t)((int32_t)ip + off);
                break;
            }

            case OP_JUMP_IF: {
                int32_t off = read_i32(code, &ip);
                if (is_truthy(pop(vm)))
                    ip = (uint32_t)((int32_t)ip + off);
                break;
            }

            case OP_JUMP_IFNOT: {
                int32_t off = read_i32(code, &ip);
                if (!is_truthy(pop(vm)))
                    ip = (uint32_t)((int32_t)ip + off);
                break;
            }

            case OP_CALL: {
                uint32_t fn_idx = read_u32(code, &ip);
                uint8_t  argc   = read_u8(code,  &ip);
                const char *name = vm->chunk->strings[fn_idx];

                // find function in chunk
                GlapeFunc *fn = NULL;
                for (uint32_t i = 0; i < vm->chunk->func_count; i++)
                    if (strcmp(vm->chunk->funcs[i].name, name) == 0) {
                        fn = &vm->chunk->funcs[i];
                        break;
                    }
                if (!fn) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "undefined function '%s'", name);
                    vm_error(msg);
                }
                if (argc != fn->param_count) {
                    char msg[128];
                    snprintf(msg, sizeof(msg),
                        "function '%s' expects %u argument(s)", name, fn->param_count);
                    vm_error(msg);
                }

                // build function scope with args
                Scope *fn_scope = scope_new(vm->global);
                // args are on stack in order: first arg pushed first
                // so stack has [arg0, arg1, ..., argN-1] top=argN-1
                // we need to pop in reverse
                for (int i = (int)argc - 1; i >= 0; i--) {
                    Value arg = pop(vm);
                    scope_set(fn_scope, fn->param_names[i], arg, 0);
                }

                Value ret = exec(vm, fn->code, fn->code_len, fn_scope);
                scope_free(fn_scope);
                push(vm, ret); // always push, OP_POP handles call stmts
                break;
            }

            case OP_CALL_LIB: {
                uint32_t lib_idx = read_u32(code, &ip);
                uint32_t fn_idx  = read_u32(code, &ip);
                uint8_t  argc    = read_u8(code,  &ip);

                const char *lib_name = vm->chunk->strings[lib_idx];
                const char *fn_name  = vm->chunk->strings[fn_idx];

                // find library
                GdlLib *lib = NULL;
                for (int i = 0; i < vm->lib_count; i++)
                    if (strcmp(vm->lib_names[i], lib_name) == 0) {
                        lib = vm->libs[i]; break;
                    }
                if (!lib) {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "library '%s' not loaded", lib_name);
                    vm_error(msg);
                }

                // FFI library - call via dlsym pointer
                if (lib->ffi) {
                    FfiSymbol *sym = NULL;
                    for (uint32_t i = 0; i < lib->ffi->symbol_count; i++)
                        if (strcmp(lib->ffi->symbols[i].glape_name, fn_name) == 0) {
                            sym = &lib->ffi->symbols[i]; break;
                        }
                    if (!sym) {
                        char msg[128];
                        snprintf(msg, sizeof(msg),
                            "function '%s' not found in FFI library '%s'", fn_name, lib_name);
                        vm_error(msg);
                    }
                    if (argc != (uint8_t)sym->arg_count) {
                        char msg[128];
                        snprintf(msg, sizeof(msg),
                            "%s.%s expects %u argument(s)", lib_name, fn_name, sym->arg_count);
                        vm_error(msg);
                    }

                    // pop args
                    Value args[64];
                    for (int i = (int)argc - 1; i >= 0; i--)
                        args[i] = pop(vm);

                    // type-safe dispatch via function pointer cast
                    // covers the most common C signatures
                    GffiType rt = (GffiType)sym->ret_type;
                    Value ret   = val_void();

                    #define A0t ((GffiType)sym->arg_types[0])
                    #define A1t ((GffiType)sym->arg_types[1])
                    #define A2t ((GffiType)sym->arg_types[2])
                    #define Ai(n) (args[n].ival)
                    #define As(n) (args[n].type == VAL_STR ? args[n].sval : "")
                    #define Ap(n) ((void*)(intptr_t)args[n].ival)

                    if (argc == 0) {
                        if (rt == GFFI_INT) ret = val_int(((int(*)(void))sym->fn_ptr)());
                        else if (rt == GFFI_PTR) ret = val_int((int64_t)(intptr_t)((void*(*)(void))sym->fn_ptr)());
                        else ((void(*)(void))sym->fn_ptr)();
                    } else if (argc == 1) {
                        if (A0t == GFFI_STR) {
                            if (rt == GFFI_INT) ret = val_int(((int(*)(const char*))sym->fn_ptr)(As(0)));
                            else if (rt == GFFI_PTR) ret = val_int((int64_t)(intptr_t)((void*(*)(const char*))sym->fn_ptr)(As(0)));
                            else ((void(*)(const char*))sym->fn_ptr)(As(0));
                        } else if (A0t == GFFI_INT) {
                            if (rt == GFFI_INT) ret = val_int(((int(*)(int64_t))sym->fn_ptr)(Ai(0)));
                            else if (rt == GFFI_PTR) ret = val_int((int64_t)(intptr_t)((void*(*)(int64_t))sym->fn_ptr)(Ai(0)));
                            else ((void(*)(int64_t))sym->fn_ptr)(Ai(0));
                        } else if (A0t == GFFI_PTR) {
                            if (rt == GFFI_INT) ret = val_int(((int(*)(void*))sym->fn_ptr)(Ap(0)));
                            else if (rt == GFFI_PTR) ret = val_int((int64_t)(intptr_t)((void*(*)(void*))sym->fn_ptr)(Ap(0)));
                            else ((void(*)(void*))sym->fn_ptr)(Ap(0));
                        }
                    } else if (argc == 2) {
                        if (A0t == GFFI_STR && A1t == GFFI_STR) {
                            if (rt == GFFI_INT) ret = val_int(((int(*)(const char*,const char*))sym->fn_ptr)(As(0),As(1)));
                            else if (rt == GFFI_PTR) ret = val_int((int64_t)(intptr_t)((void*(*)(const char*,const char*))sym->fn_ptr)(As(0),As(1)));
                            else ((void(*)(const char*,const char*))sym->fn_ptr)(As(0),As(1));
                        } else if (A0t == GFFI_PTR && A1t == GFFI_INT) {
                            if (rt == GFFI_INT) ret = val_int(((int(*)(void*,int64_t))sym->fn_ptr)(Ap(0),Ai(1)));
                            else ((void(*)(void*,int64_t))sym->fn_ptr)(Ap(0),Ai(1));
                        } else if (A0t == GFFI_INT && A1t == GFFI_INT) {
                            if (rt == GFFI_INT) ret = val_int(((int(*)(int64_t,int64_t))sym->fn_ptr)(Ai(0),Ai(1)));
                            else ((void(*)(int64_t,int64_t))sym->fn_ptr)(Ai(0),Ai(1));
                        } else if (A0t == GFFI_STR && A1t == GFFI_INT) {
                            if (rt == GFFI_INT) ret = val_int(((int(*)(const char*,int64_t))sym->fn_ptr)(As(0),Ai(1)));
                            else ((void(*)(const char*,int64_t))sym->fn_ptr)(As(0),Ai(1));
                        }
                    } else if (argc == 3) {
                        if (A0t == GFFI_PTR && A1t == GFFI_INT && A2t == GFFI_STR) {
                            if (rt == GFFI_INT) ret = val_int(((int(*)(void*,int64_t,const char*))sym->fn_ptr)(Ap(0),Ai(1),As(2)));
                            else ((void(*)(void*,int64_t,const char*))sym->fn_ptr)(Ap(0),Ai(1),As(2));
                        } else if (A0t == GFFI_PTR && A1t == GFFI_INT && A2t == GFFI_INT) {
                            if (rt == GFFI_INT) ret = val_int(((int(*)(void*,int64_t,int64_t))sym->fn_ptr)(Ap(0),Ai(1),Ai(2)));
                            else ((void(*)(void*,int64_t,int64_t))sym->fn_ptr)(Ap(0),Ai(1),Ai(2));
                        } else if (A0t == GFFI_INT && A1t == GFFI_PTR && A2t == GFFI_INT) {
                            if (rt == GFFI_INT) ret = val_int(((int(*)(int64_t,void*,int64_t))sym->fn_ptr)(Ai(0),Ap(1),Ai(2)));
                            else ((void(*)(int64_t,void*,int64_t))sym->fn_ptr)(Ai(0),Ap(1),Ai(2));
                        }
                    }

                    #undef A0t
                    #undef A1t
                    #undef A2t
                    #undef Ai
                    #undef As
                    #undef Ap

                    push(vm, ret);
                    break;
                }

                // bytecode library - search all modules
                GlapeFunc *fn     = NULL;
                Chunk     *fn_chunk = NULL;
                for (uint32_t m = 0; m < lib->module_count; m++) {
                    Chunk *mc = lib->modules[m].chunk;
                    for (uint32_t f = 0; f < mc->func_count; f++) {
                        if (strcmp(mc->funcs[f].name, fn_name) == 0) {
                            fn       = &mc->funcs[f];
                            fn_chunk = mc;
                            break;
                        }
                    }
                    if (fn) break;
                }
                if (!fn) {
                    char msg[128];
                    snprintf(msg, sizeof(msg),
                        "function '%s' not found in library '%s'", fn_name, lib_name);
                    vm_error(msg);
                }
                if (argc != fn->param_count) {
                    char msg[128];
                    snprintf(msg, sizeof(msg),
                        "%s.%s expects %u argument(s)", lib_name, fn_name, fn->param_count);
                    vm_error(msg);
                }

                // save current chunk, switch to library chunk
                Chunk *saved_chunk = vm->chunk;
                vm->chunk = fn_chunk;

                Scope *fn_scope = scope_new(vm->global);
                for (int i = (int)argc - 1; i >= 0; i--) {
                    Value arg = pop(vm);
                    scope_set(fn_scope, fn->param_names[i], arg, 0);
                }
                Value ret = exec(vm, fn->code, fn->code_len, fn_scope);
                scope_free(fn_scope);

                vm->chunk = saved_chunk;
                push(vm, ret);
                break;
            }

            case OP_RET: {
                Value v = pop(vm);
                return v;
            }

            case OP_RET_VOID:
                return val_void();

            case OP_PRINT:
                print_value(pop(vm));
                push(vm, val_void());
                break;

            case OP_HALT:
                return val_void();

            default: {
                char msg[64];
                snprintf(msg, sizeof(msg), "unknown opcode %d", op);
                vm_error(msg);
            }
        }
    }
    return val_void();
}

// ── public api ─────────────────────────────────────────────────────────────

VM *vm_new(void) {
    VM *vm     = calloc(1, sizeof(VM));
    vm->global = scope_new(NULL);
    return vm;
}

void vm_load_lib(VM *vm, const char *name, GdlLib *lib) {
    if (vm->lib_count >= VM_LIBS_MAX) {
        vm_error("too many libraries loaded");
        return;
    }
    vm->lib_names[vm->lib_count] = strdup(name);
    vm->libs[vm->lib_count]      = lib;
    vm->lib_count++;
}

void vm_run(VM *vm, Chunk *chunk) {
    vm->chunk     = chunk;
    vm->stack_top = 0;
    exec(vm, chunk->code, chunk->code_len, vm->global);
}

void vm_free(VM *vm) {
    for (int i = 0; i < vm->lib_count; i++) {
        free(vm->lib_names[i]);
        gdl_lib_free(vm->libs[i]);
    }
    scope_free(vm->global);
    free(vm);
}
