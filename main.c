// Copyright (c) 2026 greqx and Contributors

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "frontend/lexer.h"
#include "frontend/parser.h"
#include "virtual/compiler.h"
#include "virtual/vm.h"
#include "virtual/gdl.h"
#include "virtual/gffi.h"
#include "virtual/compiler.h"
#include "daemon/daemon.h"

#define GLAPE_VERSION "1.0-beta"

#define COLOR_RESET "\033[0m"
#define COLOR_LBLUE "\033[94m"

static void print_version(void) {
    printf("Glape " GLAPE_VERSION "\n");
    printf("Copyright (c) 2026 greqx and Contributors\n");
    printf("Licensed under Apache License Version 2.0\n");
    printf("\n");
    printf(COLOR_LBLUE "https://github.com/greqx/glape" COLOR_RESET "\n");
}

static void print_help(void) {
    printf("Usage: glape <command> [options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  <file.glape>              run a script\n");
    printf("  gdl --in <name>           compile a library or .so to .gdl\n");
    printf("      --out <name.gdl>\n");
    printf("      -sys                  wrap a system .so library\n");
    printf("\n");
    printf("Options:\n");
    printf("  --version                 print version info\n");
    printf("  --help                    print this message\n");
    printf("  --local                   run without daemon even if running\n");
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "\033[91merror:\033[0m cannot open file '%s'\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *src = malloc(size + 1);
    if (fread(src, 1, size, f) != (size_t)size) {
        fprintf(stderr, "\033[91merror:\033[0m failed to read file '%s'\n", path);
        exit(1);
    }
    src[size] = '\0';
    fclose(f);
    return src;
}

static void run_local(const char *path) {
    char *src = read_file(path);
    int count;
    Token *tokens = lex(src, &count);
    Node *ast = parse(tokens, count);
    Chunk *chunk = compile(ast);

    VM *vm = vm_new();

    // load imported libraries
    char script_dir[511];
    script_dir[0] = '\0';
    if (path) {
        snprintf(script_dir, sizeof(script_dir), "%s", path);
        char *sl = strrchr(script_dir, '/');
        if (sl) *(sl + 1) = '\0';
        else script_dir[0] = '\0';
    }

    for (int i = 0; i < ast->program.count; i++) {
        Node *stmt = ast->program.stmts[i];
        if (stmt->type != NODE_IMPORT) continue;

        const char *libname = stmt->import.name;
        char gdl_path[512];
        FILE *test = NULL;

        // 1. script_dir/libname.gdl
        snprintf(gdl_path, sizeof(gdl_path), "%s%s.gdl", script_dir, libname);
        test = fopen(gdl_path, "rb");

        // 2. script_dir/libs/libname.gdl
        if (!test) {
            snprintf(gdl_path, sizeof(gdl_path), "%slibss/%s.gdl", script_dir, libname);
            test = fopen(gdl_path, "rb");
        }

        // 3. script_dir/../libs/libname.gdl  (sibling libs folder)
        if (!test) {
            snprintf(gdl_path, sizeof(gdl_path), "%slibs/%s.gdl", script_dir, libname);
            test = fopen(gdl_path, "rb");
        }

        // 4. ~/.glape/libs/libname.gdl
        if (!test) {
            const char *home = getenv("HOME");
            if (home)
                snprintf(gdl_path, sizeof(gdl_path),
                    "%s/.glape/libs/%s.gdl", home, libname);
            test = fopen(gdl_path, "rb");
        }

        if (test) fclose(test);
        else {
            fprintf(stderr,
                "\033[91merror:\033[0m library '%s' not found\n", libname);
            exit(1);
        }

        GdlLib *lib = gdl_load(gdl_path);
        if (!lib) exit(1);
        vm_load_lib(vm, libname, lib);
    }

    vm_run(vm, chunk);
    vm_free(vm);

    chunk_free(chunk);
    node_free(ast);
    lex_free(tokens, count);
    free(src);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 0;
    }

    if (strcmp(argv[1], "--version") == 0) {
        print_version();
        return 0;
    }

    if (strcmp(argv[1], "--help") == 0) {
        print_help();
        return 0;
    }

    if (strcmp(argv[1], "gffi-dump") == 0) {
        if (argc < 3) {
            fprintf(stderr, "\033[91merror:\033[0m usage: glape gffi-dump <file.gffi>\n");
            return 1;
        }
        GffiFile *gf = gffi_parse(argv[2]);
        if (!gf) return 1;
        printf("functions: %u\n\n", gf->count);
        for (uint32_t i = 0; i < gf->count; i++) {
            GffiFunc *fn = &gf->funcs[i];
            printf("  %s", fn->name);
            if (strcmp(fn->name, fn->so_symbol) != 0)
                printf(" (= %s)", fn->so_symbol);
            printf("(");
            for (uint32_t j = 0; j < fn->arg_count; j++) {
                printf("%s", gffi_type_name(fn->args[j].type));
                if (j + 1 < fn->arg_count) printf(", ");
            }
            printf(") >> %s\n", gffi_type_name(fn->ret));
        }
        gffi_free(gf);
        return 0;
    }
    if (strcmp(argv[1], "gdl-dump") == 0) {
        if (argc < 3) {
            return 1;
        }
        GdlLib *lib = gdl_load(argv[2]);
        if (!lib) return 1;
        if (lib->ffi) {
            printf("type: FFI\n");
            printf("so: %s\n", lib->ffi->so_path);
            printf("symbols: %u\n\n", lib->ffi->symbol_count);
            for (uint32_t i = 0; i < lib->ffi->symbol_count; i++) {
                FfiSymbol *s = &lib->ffi->symbols[i];
                printf("  %s", s->glape_name);
                if (strcmp(s->glape_name, s->so_symbol) != 0)
                    printf(" (= %s)", s->so_symbol);
                printf("(");
                for (uint32_t j = 0; j < s->arg_count; j++) {
                    printf("%s", gffi_type_name((GffiType)s->arg_types[j]));
                    if (j + 1 < s->arg_count) printf(", ");
                }
                printf(") >> %s\n", gffi_type_name((GffiType)s->ret_type));
            }
        } else {
            printf("type: BYTECODE\n");
            printf("modules: %u\n\n", lib->module_count);
            for (uint32_t i = 0; i < lib->module_count; i++) {
                printf("=== module '%s' ===\n", lib->modules[i].name);
                chunk_disasm(lib->modules[i].chunk);
                printf("\n");
            }
        }
        gdl_lib_free(lib);
        return 0;
    }

    if (strcmp(argv[1], "gdl") == 0) {
        // glape gdl <folder> --output <file.gdl>
        // glape gdl <folder> --output <file.gdl> -sys --in <lib.so>
        const char *folder = NULL;
        const char *out_path = NULL;
        int is_ffi = 0;
        (void)is_ffi;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--output") == 0 && i+1 < argc) out_path = argv[++i];
            else if (strcmp(argv[i], "-sys") == 0) is_ffi = 1;
            else if (strcmp(argv[i], "--map") == 0 && i+1 < argc) i++; // handled below
            else if (strcmp(argv[i], "--in") == 0 && i+1 < argc) i++;
            else if (!folder) folder = argv[i];
        }

        if (!folder || !out_path) {
            fprintf(stderr, "\033[91merror:\033[0m usage: glape gdl <folder> --output <file.gdl>\n");
            return 1;
        }

        if (is_ffi) {
            // glape gdl -sys libfoo.so --map libfoo.gffi --output foo.gdl
            const char *map_path = NULL;
            for (int i = 2; i < argc; i++)
                if (strcmp(argv[i], "--map") == 0 && i+1 < argc) map_path = argv[++i];

            if (!map_path) {
                fprintf(stderr, "\033[91merror:\033[0m -sys requires --map <file.gffi>\n");
                return 1;
            }
            if (!folder) {
                fprintf(stderr, "\033[91merror:\033[0m -sys requires <library.so>\n");
                return 1;
            }

            GffiFile *gf = gffi_parse(map_path);
            if (!gf) return 1;

            printf("compiling FFI '%s' + '%s' -> '%s'\n", folder, map_path, out_path);
            if (gdl_write_ffi_from_gffi(out_path, folder, gf) < 0) {
                gffi_free(gf);
                return 1;
            }
            printf("  + %u symbols\n", gf->count);
            gffi_free(gf);
            printf("done\n");
            return 0;
        }

        printf("compiling '%s' → '%s'\n", folder, out_path);
        if (gdl_compile_folder(folder, out_path) < 0) return 1;
        printf("done\n");
        return 0;
    }

    // check for --local flag
    int force_local = 0;
    const char *script = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--local") == 0)
            force_local = 1;
        else if (!script)
            script = argv[i];
    }

    if (!script) {
        print_help();
        return 1;
    }

    if (!force_local && daemon_is_running()) {
        char *src = read_file(script);
        int ret = daemon_send_run(src, script);
        free(src);
        if (ret == 0) return 0;
        fprintf(stderr, "\033[90mwarning: daemon unreachable, running locally\033[0m\n");
    }

    run_local(script);
    return 0;
}

