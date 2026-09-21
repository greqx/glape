// Copyright (c) 2026 greqx and Contributors

#ifndef GLAPE_VM_H
#define GLAPE_VM_H

#include "bytecode.h"
#include "gdl.h"

#define VM_STACK_MAX  1024
#define VM_LIBS_MAX   64

typedef struct {
    char  *name;
    Value  val;
    int    immut;
} Slot;

typedef struct Scope Scope;
struct Scope {
    Slot   *slots;
    int     count;
    int     cap;
    Scope  *parent;
};

typedef struct {
    Chunk  *chunk;
    Value   stack[VM_STACK_MAX];
    int     stack_top;
    Scope  *global;

    // loaded libraries
    GdlLib *libs[VM_LIBS_MAX];
    char   *lib_names[VM_LIBS_MAX];
    int     lib_count;
} VM;

VM   *vm_new(void);
void  vm_load_lib(VM *vm, const char *name, GdlLib *lib);
void  vm_run(VM *vm, Chunk *chunk);
void  vm_free(VM *vm);

#endif
