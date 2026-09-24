// Copyright (c) 2026 greqx and Contributors

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "gffi.h"

#define COLOR_RED "\033[91m"
#define COLOR_WHITE "\033[97m"
#define COLOR_GREY "\033[90m"
#define COLOR_RESET "\033[0m"

static void gffi_error(const char *path, int line, const char *msg) {
    fprintf(stderr,
        COLOR_RED "error:" COLOR_RESET
        COLOR_WHITE " %s " COLOR_RESET
        COLOR_GREY "(%s:%d)\n" COLOR_RESET,
        msg, path, line);
}

const char *gffi_type_name(GffiType t) {
    switch (t) {
        case GFFI_VOID: return "void";
        case GFFI_INT: return "int";
        case GFFI_STR: return "str";
        case GFFI_PTR: return "ptr";
        default: return "?";
    }
}

static int parse_type(const char *s, GffiType *out) {
    if (strcmp(s, "void") == 0) { *out = GFFI_VOID; return 0; }
    if (strcmp(s, "int") == 0) { *out = GFFI_INT; return 0; }
    if (strcmp(s, "str") == 0) { *out = GFFI_STR; return 0; }
    if (strcmp(s, "ptr") == 0) { *out = GFFI_PTR; return 0; }
    return -1;
}

// skip whitespace but not newline
static void skip_spaces(const char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

// read identifier into buf, return length
static int read_ident(const char **p, char *buf, int cap) {
    int n = 0;
    while (isalnum(**p) || **p == '_') {
        if (n < cap - 1) buf[n++] = **p;
        (*p)++;
    }
    buf[n] = '\0';
    return n;
}

GffiFile *gffi_parse(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, COLOR_RED "error:" COLOR_RESET
            COLOR_WHITE " cannot open '%s'\n" COLOR_RESET, path);
        return NULL;
    }

    GffiFile *gf = calloc(1, sizeof(GffiFile));
    gf->cap = 8;
    gf->funcs = malloc(sizeof(GffiFunc) * gf->cap);

    char line_buf[1024];
    int lineno = 0;
    int ok = 1;

    while (fgets(line_buf, sizeof(line_buf), f)) {
        lineno++;
        const char *p = line_buf;
        skip_spaces(&p);

        // skip empty lines and comments
        if (*p == '\0' || *p == '\n' || *p == '#') continue;

        // read function name
        char name[256];
        if (read_ident(&p, name, sizeof(name)) == 0) {
            gffi_error(path, lineno, "expected function name");
            ok = 0; break;
        }

        // optional alias: name=so_symbol
        char *so_symbol = NULL;
        if (*p == '=') {
            p++;
            char alias[256];
            if (read_ident(&p, alias, sizeof(alias)) == 0) {
                gffi_error(path, lineno, "expected symbol name after '='");
                ok = 0; break;
            }
            so_symbol = strdup(alias);
        }

        skip_spaces(&p);
        if (*p != '(') {
            gffi_error(path, lineno, "expected '('");
            ok = 0; break;
        }
        p++;

        // parse argument types
        GffiArg args[64];
        uint32_t argc = 0;

        skip_spaces(&p);
        while (*p && *p != ')') {
            char type_str[32];
            if (read_ident(&p, type_str, sizeof(type_str)) == 0) {
                gffi_error(path, lineno, "expected type in argument list");
                ok = 0; break;
            }
            GffiType t;
            if (parse_type(type_str, &t) < 0) {
                char msg[128];
                snprintf(msg, sizeof(msg), "unknown type '%s'", type_str);
                gffi_error(path, lineno, msg);
                ok = 0; break;
            }
            if (argc < 64) args[argc++].type = t;
            skip_spaces(&p);
            if (*p == ',') { p++; skip_spaces(&p); }
        }
        if (!ok) break;

        if (*p != ')') {
            gffi_error(path, lineno, "expected ')'");
            ok = 0; break;
        }
        p++;
        skip_spaces(&p);

        // optional return type: >> type
        GffiType ret = GFFI_VOID;
        if (*p == '>' && *(p+1) == '>') {
            p += 2;
            skip_spaces(&p);
            char type_str[32];
            if (read_ident(&p, type_str, sizeof(type_str)) == 0) {
                gffi_error(path, lineno, "expected return type after '>>'");
                ok = 0; break;
            }
            if (parse_type(type_str, &ret) < 0) {
                char msg[128];
                snprintf(msg, sizeof(msg), "unknown return type '%s'", type_str);
                gffi_error(path, lineno, msg);
                ok = 0; break;
            }
        }

        // add function
        if (gf->count >= gf->cap) {
            gf->cap *= 2;
            gf->funcs = realloc(gf->funcs, sizeof(GffiFunc) * gf->cap);
        }
        GffiFunc *fn = &gf->funcs[gf->count++];
        fn->name = strdup(name);
        fn->so_symbol = so_symbol ? so_symbol : strdup(name);
        fn->ret = ret;
        fn->arg_count = argc;
        fn->args = argc > 0 ? malloc(sizeof(GffiArg) * argc) : NULL;
        for (uint32_t i = 0; i < argc; i++)
            fn->args[i] = args[i];
    }

    fclose(f);

    if (!ok) {
        gffi_free(gf);
        return NULL;
    }

    return gf;
}

void gffi_free(GffiFile *gf) {
    if (!gf) return;
    for (uint32_t i = 0; i < gf->count; i++) {
        free(gf->funcs[i].name);
        free(gf->funcs[i].so_symbol);
        free(gf->funcs[i].args);
    }
    free(gf->funcs);
    free(gf);
}
