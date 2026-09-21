// Copyright (c) 2026 greqx and Contributors

#ifndef GLAPE_GDL_H
#define GLAPE_GDL_H

#include <stdint.h>
#include "bytecode.h"
#include "gffi.h"

#define GDL_MAGIC   "GDLX"
#define GDL_VERSION 1

typedef enum {
    GDL_BYTECODE = 0,
    GDL_FFI      = 1,
} GdlType;

// one module inside a .gdl (one .glape file from the library folder)
typedef struct {
    char  *name;   // module name (filename without .glape)
    Chunk *chunk;
} GdlModule;

// FFI symbol: glape name -> symbol in .so + type signature
typedef struct {
    char     *glape_name;
    char     *so_symbol;
    uint8_t  *arg_types;   // GffiType values for each arg
    uint32_t  arg_count;
    uint8_t   ret_type;    // GffiType return value
    void     *fn_ptr;      // filled at load time by dlsym
} FfiSymbol;

// loaded FFI library
typedef struct {
    char      *so_path;
    FfiSymbol *symbols;
    uint32_t   symbol_count;
    void      *dl_handle;
} FfiLib;

// a full loaded .gdl library
typedef struct {
    char      *name;          // library name (e.g. "mylib")
    GdlModule *modules;       // one per .glape file
    uint32_t   module_count;
    FfiLib    *ffi;           // non-NULL if GDL_FFI
} GdlLib;

// compile a library folder into a .gdl file
// folder/   ← all .glape files become modules
//   libs/   ← .gdl dependencies get embedded
// returns 0 on success
int gdl_compile_folder(const char *folder, const char *out_path);

// read a .gdl file into a GdlLib
// returns NULL on error
GdlLib *gdl_load(const char *path);

void gdl_lib_free(GdlLib *lib);

// write FFI .gdl from a parsed .gffi file
int gdl_write_ffi_from_gffi(const char *path, const char *so_path, GffiFile *gf);

// write FFI .gdl (legacy, simple)
int gdl_write_ffi(const char *path, const char *so_path,
                  FfiSymbol *symbols, uint32_t count);

#endif
