// Copyright (c) 2026 greqx and Contributors

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include "gdl.h"
#include "gffi.h"
#include "compiler.h"
#include "../frontend/lexer.h"
#include "../frontend/parser.h"

#define COLOR_RED "\033[91m"
#define COLOR_WHITE "\033[97m"
#define COLOR_RESET "\033[0m"

static void gdl_error(const char *msg) {
    fprintf(stderr,
        COLOR_RED "error:" COLOR_RESET
        COLOR_WHITE " %s\n" COLOR_RESET, msg);
}

// ── low-level write ────────────────────────────────────────────────────────

static int w_u8(FILE *f, uint8_t v) {
    return fwrite(&v, 1, 1, f) == 1 ? 0 : -1;
}
static int w_u32(FILE *f, uint32_t v) {
    uint8_t b[4] = { v&0xff, (v>>8)&0xff, (v>>16)&0xff, (v>>24)&0xff };
    return fwrite(b, 1, 4, f) == 4 ? 0 : -1;
}
static int w_bytes(FILE *f, const void *d, uint32_t n) {
    return fwrite(d, 1, n, f) == n ? 0 : -1;
}
static int w_str(FILE *f, const char *s) {
    uint32_t n = (uint32_t)strlen(s);
    if (w_u32(f, n) < 0) return -1;
    return n > 0 ? w_bytes(f, s, n) : 0;
}

// ── low-level read ─────────────────────────────────────────────────────────

static int r_u8(FILE *f, uint8_t *v) {
    return fread(v, 1, 1, f) == 1 ? 0 : -1;
}
static int r_u32(FILE *f, uint32_t *v) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return -1;
    *v = (uint32_t)b[0] | ((uint32_t)b[1]<<8)
       | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);
    return 0;
}
static int r_bytes(FILE *f, void *d, uint32_t n) {
    return fread(d, 1, n, f) == n ? 0 : -1;
}
static char *r_str(FILE *f) {
    uint32_t n;
    if (r_u32(f, &n) < 0) return NULL;
    char *s = malloc(n + 1);
    if (n > 0 && r_bytes(f, s, n) < 0) { free(s); return NULL; }
    s[n] = '\0';
    return s;
}

// ── chunk write/read ───────────────────────────────────────────────────────

static int write_chunk(FILE *f, Chunk *c) {
    // string table
    w_u32(f, c->string_count);
    for (uint32_t i = 0; i < c->string_count; i++)
        w_str(f, c->strings[i]);

    // functions
    w_u32(f, c->func_count);
    for (uint32_t i = 0; i < c->func_count; i++) {
        GlapeFunc *fn = &c->funcs[i];
        w_str(f, fn->name);
        w_u32(f, fn->param_count);
        for (uint32_t j = 0; j < fn->param_count; j++)
            w_str(f, fn->param_names[j]);
        w_u32(f, fn->code_len);
        w_bytes(f, fn->code, fn->code_len);
    }

    // top-level code
    w_u32(f, c->code_len);
    return w_bytes(f, c->code, c->code_len);
}

static Chunk *read_chunk(FILE *f) {
    Chunk *c = chunk_new();

    uint32_t sc;
    if (r_u32(f, &sc) < 0) goto err;
    for (uint32_t i = 0; i < sc; i++) {
        char *s = r_str(f);
        if (!s) goto err;
        chunk_add_string(c, s);
        free(s);
    }

    uint32_t fc;
    if (r_u32(f, &fc) < 0) goto err;
    for (uint32_t i = 0; i < fc; i++) {
        char *name = r_str(f);
        if (!name) goto err;
        uint32_t fi = chunk_add_func(c, name);
        free(name);

        GlapeFunc *fn = &c->funcs[fi];
        if (r_u32(f, &fn->param_count) < 0) goto err;
        fn->param_names = malloc(sizeof(char *) * (fn->param_count + 1));
        for (uint32_t j = 0; j < fn->param_count; j++) {
            fn->param_names[j] = r_str(f);
            if (!fn->param_names[j]) goto err;
        }
        if (r_u32(f, &fn->code_len) < 0) goto err;
        fn->code_cap = fn->code_len;
        fn->code = malloc(fn->code_len);
        if (r_bytes(f, fn->code, fn->code_len) < 0) goto err;
    }

    uint32_t top_len;
    if (r_u32(f, &top_len) < 0) goto err;
    c->code_cap = top_len;
    c->code_len = top_len;
    free(c->code);
    c->code = malloc(top_len ? top_len : 1);
    if (top_len && r_bytes(f, c->code, top_len) < 0) goto err;

    return c;
err:
    chunk_free(c);
    return NULL;
}

// ── compile one .glape file → Chunk ───────────────────────────────────────

static Chunk *compile_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *src = malloc(sz + 1);
    if (fread(src, 1, sz, f) != (size_t)sz) { free(src); fclose(f); return NULL; }
    src[sz] = '\0';
    fclose(f);

    int count;
    Token *tokens = lex(src, &count);
    Node *ast = parse(tokens, count);
    Chunk *chunk = compile(ast);

    node_free(ast);
    lex_free(tokens, count);
    free(src);
    return chunk;
}

// strip .glape extension from filename
static char *module_name(const char *filename) {
    char *n = strdup(filename);
    char *dot = strstr(n, ".glape");
    if (dot) *dot = '\0';
    return n;
}

// ── folder compiler ────────────────────────────────────────────────────────

int gdl_compile_folder(const char *folder, const char *out_path) {
    DIR *dir = opendir(folder);
    if (!dir) {
        gdl_error("cannot open library folder");
        return -1;
    }

    // collect .glape files
    char **glape_files = NULL;
    int glape_count = 0;
    int glape_cap = 8;
    glape_files = malloc(sizeof(char *) * glape_cap);

    struct dirent *entry;
    while ((entry = readdir(dir))) {
        const char *name = entry->d_name;
        size_t nlen = strlen(name);
        if (nlen > 6 && strcmp(name + nlen - 6, ".glape") == 0) {
            if (glape_count >= glape_cap) {
                glape_cap *= 2;
                glape_files = realloc(glape_files, sizeof(char *) * glape_cap);
            }
            glape_files[glape_count++] = strdup(name);
        }
    }
    closedir(dir);

    if (glape_count == 0) {
        gdl_error("no .glape files found in folder");
        free(glape_files);
        return -1;
    }

    // collect embedded .gdl dependencies from libs/
    char libs_path[512];
    snprintf(libs_path, sizeof(libs_path), "%s/libs", folder);
    DIR *libs_dir = opendir(libs_path);

    char **dep_paths = NULL;
    char **dep_names = NULL;
    int dep_count = 0;
    int dep_cap = 8;
    dep_paths = malloc(sizeof(char *) * dep_cap);
    dep_names = malloc(sizeof(char *) * dep_cap);

    if (libs_dir) {
        while ((entry = readdir(libs_dir))) {
            const char *name = entry->d_name;
            size_t nlen = strlen(name);
            if (nlen > 4 && strcmp(name + nlen - 4, ".gdl") == 0) {
                if (dep_count >= dep_cap) {
                    dep_cap *= 2;
                    dep_paths = realloc(dep_paths, sizeof(char *) * dep_cap);
                    dep_names = realloc(dep_names, sizeof(char *) * dep_cap);
                }
                char full[512];
                snprintf(full, sizeof(full), "%s/libs/%s", folder, name);
                dep_paths[dep_count] = strdup(full);
                // name without .gdl
                char *n = strdup(name);
                char *dot = strstr(n, ".gdl");
                if (dot) *dot = '\0';
                dep_names[dep_count] = n;
                dep_count++;
            }
        }
        closedir(libs_dir);
    }

    // ensure output directory exists
    char out_dir[512];
    snprintf(out_dir, sizeof(out_dir), "%s", out_path);
    char *last_slash = strrchr(out_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        // mkdir -p equivalent: try creating each component
        for (char *p = out_dir + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                mkdir(out_dir, 0755);
                *p = '/';
            }
        }
        mkdir(out_dir, 0755);
    }

    // write .gdl
    FILE *out = fopen(out_path, "wb");
    if (!out) {
        gdl_error("cannot create output .gdl file");
        return -1;
    }

    // header
    fwrite(GDL_MAGIC, 1, 4, out);
    w_u8(out, GDL_VERSION);
    w_u8(out, GDL_BYTECODE);
    w_u8(out, 0);
    w_u8(out, 0);

    // module count
    w_u32(out, (uint32_t)glape_count);
    for (int i = 0; i < glape_count; i++) {
        char path_buf[512];
        snprintf(path_buf, sizeof(path_buf), "%s/%s", folder, glape_files[i]);

        char *modname = module_name(glape_files[i]);
        Chunk *chunk = compile_file(path_buf);
        if (!chunk) {
            fprintf(stderr, COLOR_RED "error:" COLOR_RESET
                " failed to compile '%s'\n", path_buf);
            fclose(out);
            return -1;
        }

        w_str(out, modname);
        write_chunk(out, chunk);

        printf("  + module '%s'\n", modname);
        chunk_free(chunk);
        free(modname);
        free(glape_files[i]);
    }
    free(glape_files);

    // embedded dependencies
    w_u32(out, (uint32_t)dep_count);
    for (int i = 0; i < dep_count; i++) {
        // read raw bytes of the dep .gdl and embed
        FILE *dep = fopen(dep_paths[i], "rb");
        if (!dep) {
            fprintf(stderr, COLOR_RED "error:" COLOR_RESET
                " cannot open dependency '%s'\n", dep_paths[i]);
            fclose(out);
            return -1;
        }
        fseek(dep, 0, SEEK_END);
        long dep_sz = ftell(dep);
        rewind(dep);
        uint8_t *dep_data = malloc(dep_sz);
        if (fread(dep_data, 1, dep_sz, dep) != (size_t)dep_sz) {
            free(dep_data); fclose(dep); fclose(out);
            return -1;
        }
        fclose(dep);

        w_str(out, dep_names[i]);
        w_u32(out, (uint32_t)dep_sz);
        w_bytes(out, dep_data, (uint32_t)dep_sz);

        printf("  + dep '%s'\n", dep_names[i]);
        free(dep_data);
        free(dep_paths[i]);
        free(dep_names[i]);
    }
    free(dep_paths);
    free(dep_names);

    fclose(out);
    return 0;
}

// ── load ───────────────────────────────────────────────────────────────────

GdlLib *gdl_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { gdl_error("cannot open .gdl file"); return NULL; }

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, GDL_MAGIC, 4) != 0) {
        gdl_error("invalid .gdl file"); fclose(f); return NULL;
    }

    uint8_t version, type, r0, r1;
    r_u8(f, &version); r_u8(f, &type);
    r_u8(f, &r0); r_u8(f, &r1);

    if (version != GDL_VERSION) {
        gdl_error("unsupported .gdl version"); fclose(f); return NULL;
    }

    GdlLib *lib = calloc(1, sizeof(GdlLib));

    if (type == GDL_BYTECODE) {
        uint32_t mc;
        r_u32(f, &mc);
        lib->module_count = mc;
        lib->modules = malloc(sizeof(GdlModule) * mc);

        for (uint32_t i = 0; i < mc; i++) {
            lib->modules[i].name = r_str(f);
            lib->modules[i].chunk = read_chunk(f);
            if (!lib->modules[i].name || !lib->modules[i].chunk) {
                gdl_error("corrupt module in .gdl");
                gdl_lib_free(lib);
                fclose(f);
                return NULL;
            }
        }

        // skip embedded deps (loaded separately if needed)
        uint32_t dep_count;
        r_u32(f, &dep_count);
        for (uint32_t i = 0; i < dep_count; i++) {
            char *dep_name = r_str(f);
            free(dep_name);
            uint32_t dep_sz;
            r_u32(f, &dep_sz);
            fseek(f, dep_sz, SEEK_CUR);
        }

    } else if (type == GDL_FFI) {
        FfiLib *ffi = calloc(1, sizeof(FfiLib));
        ffi->so_path = r_str(f);
        r_u32(f, &ffi->symbol_count);
        ffi->symbols = calloc(ffi->symbol_count, sizeof(FfiSymbol));
        for (uint32_t i = 0; i < ffi->symbol_count; i++) {
            ffi->symbols[i].glape_name = r_str(f);
            ffi->symbols[i].so_symbol = r_str(f);
            uint8_t ret;
            r_u8(f, &ret);
            ffi->symbols[i].ret_type = ret;
            uint32_t ac;
            r_u32(f, &ac);
            ffi->symbols[i].arg_count = ac;
            ffi->symbols[i].arg_types = ac > 0 ? malloc(ac) : NULL;
            for (uint32_t j = 0; j < ac; j++) {
                uint8_t t;
                r_u8(f, &t);
                ffi->symbols[i].arg_types[j] = t;
            }
            ffi->symbols[i].fn_ptr = NULL; // filled by dlopen at runtime
        }

        // open the .so and resolve all symbols
        ffi->dl_handle = dlopen(ffi->so_path, RTLD_LAZY | RTLD_GLOBAL);
        if (!ffi->dl_handle) {
            // try just the filename (let linker find it)
            const char *basename = strrchr(ffi->so_path, '/');
            if (basename)
                ffi->dl_handle = dlopen(basename + 1, RTLD_LAZY | RTLD_GLOBAL);
        }
        if (!ffi->dl_handle) {
            fprintf(stderr,
                "\033[91merror:\033[0m cannot load '%s': %s\n",
                ffi->so_path, dlerror());
            gdl_lib_free(lib);
            fclose(f);
            return NULL;
        }
        for (uint32_t i = 0; i < ffi->symbol_count; i++) {
            ffi->symbols[i].fn_ptr = dlsym(ffi->dl_handle, ffi->symbols[i].so_symbol);
            if (!ffi->symbols[i].fn_ptr) {
                fprintf(stderr,
                    "\033[91merror:\033[0m symbol '%s' not found in '%s'\n",
                    ffi->symbols[i].so_symbol, ffi->so_path);
                gdl_lib_free(lib);
                fclose(f);
                return NULL;
            }
        }
        lib->ffi = ffi;
    }

    fclose(f);
    return lib;
}

// ── cleanup ────────────────────────────────────────────────────────────────

void gdl_lib_free(GdlLib *lib) {
    if (!lib) return;
    free(lib->name);
    for (uint32_t i = 0; i < lib->module_count; i++) {
        free(lib->modules[i].name);
        chunk_free(lib->modules[i].chunk);
    }
    free(lib->modules);
    if (lib->ffi) {
        if (lib->ffi->dl_handle) dlclose(lib->ffi->dl_handle);
        free(lib->ffi->so_path);
        for (uint32_t i = 0; i < lib->ffi->symbol_count; i++) {
            free(lib->ffi->symbols[i].glape_name);
            free(lib->ffi->symbols[i].so_symbol);
            free(lib->ffi->symbols[i].arg_types);
        }
        free(lib->ffi->symbols);
        free(lib->ffi);
    }
    free(lib);
}

int gdl_write_ffi_from_gffi(const char *path, const char *so_path, GffiFile *gf) {
    FILE *f = fopen(path, "wb");
    if (!f) { gdl_error("cannot open output file"); return -1; }

    fwrite(GDL_MAGIC, 1, 4, f);
    w_u8(f, GDL_VERSION);
    w_u8(f, GDL_FFI);
    w_u8(f, 0); w_u8(f, 0);

    w_str(f, so_path);
    w_u32(f, gf->count);

    for (uint32_t i = 0; i < gf->count; i++) {
        GffiFunc *fn = &gf->funcs[i];
        w_str(f, fn->name);
        w_str(f, fn->so_symbol);
        w_u8(f, (uint8_t)fn->ret);
        w_u32(f, fn->arg_count);
        for (uint32_t j = 0; j < fn->arg_count; j++)
            w_u8(f, (uint8_t)fn->args[j].type);
    }

    fclose(f);
    return 0;
}

int gdl_write_ffi(const char *path, const char *so_path,
                  FfiSymbol *symbols, uint32_t count) {
    FILE *f = fopen(path, "wb");
    if (!f) { gdl_error("cannot open output file"); return -1; }
    fwrite(GDL_MAGIC, 1, 4, f);
    w_u8(f, GDL_VERSION);
    w_u8(f, GDL_FFI);
    w_u8(f, 0); w_u8(f, 0);
    w_str(f, so_path);
    w_u32(f, count);
    for (uint32_t i = 0; i < count; i++) {
        w_str(f, symbols[i].glape_name);
        w_str(f, symbols[i].so_symbol);
        w_u8(f, 0); // ret = void
        w_u32(f, symbols[i].arg_count);
        for (uint32_t j = 0; j < symbols[i].arg_count; j++)
            w_u8(f, symbols[i].arg_types[j]);
    }
    fclose(f);
    return 0;
}
