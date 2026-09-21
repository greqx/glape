// Copyright (c) 2026 greqx and Contributors

#ifndef GLAPE_GFFI_H
#define GLAPE_GFFI_H

#include <stdint.h>

// types supported in .gffi
typedef enum {
    GFFI_VOID,
    GFFI_INT,
    GFFI_STR,
    GFFI_PTR,
} GffiType;

// one argument
typedef struct {
    GffiType type;
} GffiArg;

// one function declaration
typedef struct {
    char     *name;       // glape name (also symbol in .so unless overridden)
    char     *so_symbol;  // actual symbol in .so (NULL = same as name)
    GffiArg  *args;
    uint32_t  arg_count;
    GffiType  ret;
} GffiFunc;

// parsed .gffi file
typedef struct {
    GffiFunc *funcs;
    uint32_t  count;
    uint32_t  cap;
} GffiFile;

// parse a .gffi file, returns NULL on error
GffiFile *gffi_parse(const char *path);

void gffi_free(GffiFile *f);

const char *gffi_type_name(GffiType t);

#endif
