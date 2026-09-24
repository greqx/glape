// Copyright (c) 2026 greqx and Contributors

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

#define COLOR_RESET "\033[0m"
#define COLOR_RED "\033[91m"
#define COLOR_WHITE "\033[97m"
#define COLOR_GREY "\033[90m"

typedef struct {
    Token *tokens;
    int count;
    int pos;
} Parser;

static void parse_error(Parser *p, const char *msg) {
    Token *t = &p->tokens[p->pos];
    fprintf(stderr,
        COLOR_RED "error:" COLOR_RESET
        COLOR_WHITE " %s " COLOR_RESET
        COLOR_GREY "(%d:%d)\n" COLOR_RESET,
        msg, t->line, t->col);
    exit(1);
}

static Token *peek(Parser *p) {
    return &p->tokens[p->pos];
}

static Token *advance(Parser *p) {
    return &p->tokens[p->pos++];
}

static int check(Parser *p, TokenType t) {
    return peek(p)->type == t;
}

static Token *expect(Parser *p, TokenType t, const char *msg) {
    if (!check(p, t)) parse_error(p, msg);
    return advance(p);
}

// skip consecutive newlines
static void skip_newlines(Parser *p) {
    while (check(p, TOK_NEWLINE))
        advance(p);
}

static Node *make_node(NodeType type, Token *tok) {
    Node *n = calloc(1, sizeof(Node));
    n->type = type;
    n->line = tok->line;
    n->col = tok->col;
    return n;
}

static GlapeType parse_type(Parser *p) {
    Token *t = peek(p);
    switch (t->type) {
        case TOK_TYPE_INT: advance(p); return TYPE_INT;
        case TOK_TYPE_FLOAT: advance(p); return TYPE_FLOAT;
        case TOK_TYPE_STR: advance(p); return TYPE_STR;
        case TOK_TYPE_BOOL: advance(p); return TYPE_BOOL;
        default: parse_error(p, "expected a type"); return TYPE_VOID;
    }
}

static Node *parse_expr(Parser *p);
static Node *parse_stmt(Parser *p);

// parse a block: INDENT stmts DEDENT
static Node **parse_block(Parser *p, int *out_count) {
    if (check(p, TOK_NEWLINE)) advance(p);
    expect(p, TOK_INDENT, "expected indented block");

    int cap = 8;
    int count = 0;
    Node **stmts = malloc(sizeof(Node *) * cap);

    while (!check(p, TOK_DEDENT) && !check(p, TOK_EOF)) {
        skip_newlines(p);
        if (check(p, TOK_DEDENT) || check(p, TOK_EOF)) break;
        if (count >= cap) {
            cap *= 2;
            stmts = realloc(stmts, sizeof(Node *) * cap);
        }
        stmts[count++] = parse_stmt(p);
    }

    expect(p, TOK_DEDENT, "expected dedent after block");
    *out_count = count;
    return stmts;
}

// primary expression
static Node *parse_primary(Parser *p) {
    Token *t = peek(p);

    if (t->type == TOK_INT) {
        Node *n = make_node(NODE_INT_LIT, t);
        n->int_lit.val = atoll(t->value);
        advance(p);
        return n;
    }

    if (t->type == TOK_FLOAT) {
        Node *n = make_node(NODE_FLOAT_LIT, t);
        n->float_lit.val = atof(t->value);
        advance(p);
        return n;
    }

    if (t->type == TOK_STRING) {
        Node *n = make_node(NODE_STR_LIT, t);
        n->str_lit.val = strdup(t->value);
        advance(p);
        return n;
    }

    if (t->type == TOK_TRUE || t->type == TOK_FALSE) {
        Node *n = make_node(NODE_BOOL_LIT, t);
        n->bool_lit.val = (t->type == TOK_TRUE);
        advance(p);
        return n;
    }

    if (t->type == TOK_IDENT) {
        char *name = strdup(t->value);
        Token *tok = t;
        advance(p);

        // lib.func() call
        if (check(p, TOK_DOT)) {
            advance(p);
            Token *method = expect(p, TOK_IDENT, "expected method name after '.'");
            expect(p, TOK_LPAREN, "expected '(' after method name");

            int cap = 4;
            int argc = 0;
            Node **args = malloc(sizeof(Node *) * cap);

            while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
                if (argc >= cap) { cap *= 2; args = realloc(args, sizeof(Node *) * cap); }
                args[argc++] = parse_expr(p);
                if (!check(p, TOK_RPAREN))
                    expect(p, TOK_COMMA, "expected ',' between arguments");
            }
            expect(p, TOK_RPAREN, "expected ')'");

            Node *n = make_node(NODE_CALL_EXPR, tok);
            n->call.object = name;
            n->call.name = strdup(method->value);
            n->call.args = args;
            n->call.arg_count = argc;
            return n;
        }

        // plain func() call
        if (check(p, TOK_LPAREN)) {
            advance(p);
            int cap = 4;
            int argc = 0;
            Node **args = malloc(sizeof(Node *) * cap);

            while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
                if (argc >= cap) { cap *= 2; args = realloc(args, sizeof(Node *) * cap); }
                args[argc++] = parse_expr(p);
                if (!check(p, TOK_RPAREN))
                    expect(p, TOK_COMMA, "expected ',' between arguments");
            }
            expect(p, TOK_RPAREN, "expected ')'");

            Node *n = make_node(NODE_CALL_EXPR, tok);
            n->call.object = NULL;
            n->call.name = name;
            n->call.args = args;
            n->call.arg_count = argc;
            return n;
        }

        // plain identifier
        Node *n = make_node(NODE_IDENT, tok);
        n->ident.name = name;
        return n;
    }

    parse_error(p, "unexpected token in expression");
    return NULL;
}

// binary expression with precedence
static int get_precedence(TokenType t) {
    switch (t) {
        case TOK_EQ:
        case TOK_NEQ: return 1;
        case TOK_GT:
        case TOK_LT:
        case TOK_GTE:
        case TOK_LTE: return 2;
        case TOK_PLUS:
        case TOK_MINUS: return 3;
        case TOK_STAR:
        case TOK_SLASH: return 4;
        default: return 0;
    }
}

static Node *parse_binary(Parser *p, int min_prec) {
    Node *left = parse_primary(p);

    while (1) {
        int prec = get_precedence(peek(p)->type);
        if (prec < min_prec) break;

        Token *op = advance(p);
        Node *right = parse_binary(p, prec + 1);

        Node *n = make_node(NODE_BINARY, op);
        n->binary.op = op->type;
        n->binary.left = left;
        n->binary.right = right;
        left = n;
    }

    return left;
}

static Node *parse_expr(Parser *p) {
    return parse_binary(p, 1);
}

static Node *parse_stmt(Parser *p) {
    Token *t = peek(p);

    // import: get libname
    if (t->type == TOK_GET) {
        advance(p);
        Token *name = expect(p, TOK_IDENT, "expected library name after 'get'");
        Node *n = make_node(NODE_IMPORT, t);
        n->import.name = strdup(name->value);
        skip_newlines(p);
        return n;
    }

    // immut var decl
    if (t->type == TOK_IMMUT) {
        advance(p);
        Token *name = expect(p, TOK_IDENT, "expected variable name");
        expect(p, TOK_COLON, "expected ':' after variable name");
        GlapeType vtype = parse_type(p);
        expect(p, TOK_ASSIGN, "expected '=' in variable declaration");
        Node *value = parse_expr(p);
        Node *n = make_node(NODE_VAR_DECL, t);
        n->var_decl.name = strdup(name->value);
        n->var_decl.vtype = vtype;
        n->var_decl.immut = 1;
        n->var_decl.value = value;
        skip_newlines(p);
        return n;
    }

    // if
    if (t->type == TOK_IF) {
        advance(p);
        Node *cond = parse_expr(p);
        expect(p, TOK_COLON, "expected ':' after if condition");

        int body_count, else_count = 0;
        Node **body = parse_block(p, &body_count);
        Node **else_body = NULL;

        skip_newlines(p);
        if (check(p, TOK_ELSE)) {
            advance(p);
            expect(p, TOK_COLON, "expected ':' after else");
            else_body = parse_block(p, &else_count);
        }

        Node *n = make_node(NODE_IF, t);
        n->if_stmt.cond = cond;
        n->if_stmt.body = body;
        n->if_stmt.body_count = body_count;
        n->if_stmt.else_body = else_body;
        n->if_stmt.else_count = else_count;
        return n;
    }

    // loop
    if (t->type == TOK_LOOP) {
        advance(p);

        // loop: infinite
        if (check(p, TOK_COLON)) {
            advance(p);
            int count; Node **body = parse_block(p, &count);
            Node *n = make_node(NODE_LOOP_INFINITE, t);
            n->loop_inf.body = body;
            n->loop_inf.count = count;
            return n;
        }

        // loop var in range:
        if (check(p, TOK_IDENT)) {
            // peek ahead for IN
            int saved = p->pos;
            char *var = strdup(peek(p)->value);
            advance(p);
            if (check(p, TOK_IN)) {
                advance(p);
                Node *from = parse_expr(p);
                int inclusive = 0;
                if (check(p, TOK_RANGE_INCL)) { inclusive = 1; advance(p); }
                else expect(p, TOK_RANGE, "expected '..' or '..=' in range");
                Node *to = parse_expr(p);
                expect(p, TOK_COLON, "expected ':' after range");
                int count; Node **body = parse_block(p, &count);
                Node *n = make_node(NODE_LOOP_RANGE, t);
                n->loop_range.var = var;
                n->loop_range.from = from;
                n->loop_range.to = to;
                n->loop_range.inclusive = inclusive;
                n->loop_range.body = body;
                n->loop_range.count = count;
                return n;
            }
            // not a range loop, backtrack to while
            p->pos = saved;
            free(var);
        }

        // loop condition: while-style
        Node *cond = parse_expr(p);
        expect(p, TOK_COLON, "expected ':' after loop condition");
        int count; Node **body = parse_block(p, &count);
        Node *n = make_node(NODE_LOOP_WHILE, t);
        n->loop_while.cond = cond;
        n->loop_while.body = body;
        n->loop_while.count = count;
        return n;
    }

    // define
    if (t->type == TOK_DEFINE) {
        advance(p);
        Token *name = expect(p, TOK_IDENT, "expected function name");
        expect(p, TOK_LPAREN, "expected '('");

        int cap = 4;
        int param_count = 0;
        Param *params = malloc(sizeof(Param) * cap);

        while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
            if (param_count >= cap) { cap *= 2; params = realloc(params, sizeof(Param) * cap); }
            Token *pname = expect(p, TOK_IDENT, "expected parameter name");
            expect(p, TOK_COLON, "expected ':' after parameter name");
            GlapeType ptype = parse_type(p);
            params[param_count].name = strdup(pname->value);
            params[param_count].type = ptype;
            param_count++;
            if (!check(p, TOK_RPAREN))
                expect(p, TOK_COMMA, "expected ',' between parameters");
        }
        expect(p, TOK_RPAREN, "expected ')'");

        GlapeType ret = TYPE_VOID;
        if (check(p, TOK_ARROW)) {
            advance(p);
            ret = parse_type(p);
        }
        expect(p, TOK_COLON, "expected ':' after function signature");

        int body_count; Node **body = parse_block(p, &body_count);
        Node *n = make_node(NODE_FUNC_DEF, t);
        n->func_def.name = strdup(name->value);
        n->func_def.params = params;
        n->func_def.param_count = param_count;
        n->func_def.ret_type = ret;
        n->func_def.body = body;
        n->func_def.body_count = body_count;
        return n;
    }

    // return
    if (t->type == TOK_RETURN) {
        advance(p);
        Node *val = NULL;
        if (!check(p, TOK_NEWLINE) && !check(p, TOK_EOF))
            val = parse_expr(p);
        Node *n = make_node(NODE_RETURN, t);
        n->ret.value = val;
        skip_newlines(p);
        return n;
    }

    // ident: var decl, assign, or call stmt
    if (t->type == TOK_IDENT) {
        char *name = strdup(t->value);
        Token *tok = t;
        advance(p);

        // lib.func() call statement
        if (check(p, TOK_DOT)) {
            advance(p);
            Token *method = expect(p, TOK_IDENT, "expected method name after '.'");
            expect(p, TOK_LPAREN, "expected '('");

            int cap = 4, argc = 0;
            Node **args = malloc(sizeof(Node *) * cap);
            while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
                if (argc >= cap) { cap *= 2; args = realloc(args, sizeof(Node *) * cap); }
                args[argc++] = parse_expr(p);
                if (!check(p, TOK_RPAREN))
                    expect(p, TOK_COMMA, "expected ','");
            }
            expect(p, TOK_RPAREN, "expected ')'");

            Node *n = make_node(NODE_CALL_STMT, tok);
            n->call.object = name;
            n->call.name = strdup(method->value);
            n->call.args = args;
            n->call.arg_count = argc;
            skip_newlines(p);
            return n;
        }

        // plain func() call statement
        if (check(p, TOK_LPAREN)) {
            advance(p);
            int cap = 4, argc = 0;
            Node **args = malloc(sizeof(Node *) * cap);
            while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
                if (argc >= cap) { cap *= 2; args = realloc(args, sizeof(Node *) * cap); }
                args[argc++] = parse_expr(p);
                if (!check(p, TOK_RPAREN))
                    expect(p, TOK_COMMA, "expected ','");
            }
            expect(p, TOK_RPAREN, "expected ')'");

            Node *n = make_node(NODE_CALL_STMT, tok);
            n->call.object = NULL;
            n->call.name = name;
            n->call.args = args;
            n->call.arg_count = argc;
            skip_newlines(p);
            return n;
        }

        // var decl: name:type = expr
        if (check(p, TOK_COLON)) {
            advance(p);
            GlapeType vtype = parse_type(p);
            expect(p, TOK_ASSIGN, "expected '=' in variable declaration");
            Node *value = parse_expr(p);
            Node *n = make_node(NODE_VAR_DECL, tok);
            n->var_decl.name = name;
            n->var_decl.vtype = vtype;
            n->var_decl.immut = 0;
            n->var_decl.value = value;
            skip_newlines(p);
            return n;
        }

        // assign: name = expr
        if (check(p, TOK_ASSIGN)) {
            advance(p);
            Node *value = parse_expr(p);
            Node *n = make_node(NODE_ASSIGN, tok);
            n->assign.name = name;
            n->assign.value = value;
            skip_newlines(p);
            return n;
        }

        parse_error(p, "unexpected token after identifier");
    }

    parse_error(p, "unexpected token");
    return NULL;
}

Node *parse(Token *tokens, int count) {
    Parser p;
    p.tokens = tokens;
    p.count = count;
    p.pos = 0;

    skip_newlines(&p);

    int cap = 16;
    int total = 0;
    Node **stmts = malloc(sizeof(Node *) * cap);

    Token first = tokens[0];
    while (!check(&p, TOK_EOF)) {
        skip_newlines(&p);
        if (check(&p, TOK_EOF)) break;
        if (total >= cap) { cap *= 2; stmts = realloc(stmts, sizeof(Node *) * cap); }
        stmts[total++] = parse_stmt(&p);
    }

    Node *root = make_node(NODE_PROGRAM, &first);
    root->program.stmts = stmts;
    root->program.count = total;
    return root;
}

void node_free(Node *n) {
    if (!n) return;
    switch (n->type) {
        case NODE_PROGRAM:
            for (int i = 0; i < n->program.count; i++) node_free(n->program.stmts[i]);
            free(n->program.stmts);
            break;
        case NODE_IMPORT: free(n->import.name); break;
        case NODE_VAR_DECL: free(n->var_decl.name); node_free(n->var_decl.value); break;
        case NODE_ASSIGN: free(n->assign.name); node_free(n->assign.value); break;
        case NODE_IF:
            node_free(n->if_stmt.cond);
            for (int i = 0; i < n->if_stmt.body_count; i++) node_free(n->if_stmt.body[i]);
            free(n->if_stmt.body);
            for (int i = 0; i < n->if_stmt.else_count; i++) node_free(n->if_stmt.else_body[i]);
            free(n->if_stmt.else_body);
            break;
        case NODE_LOOP_INFINITE:
            for (int i = 0; i < n->loop_inf.count; i++) node_free(n->loop_inf.body[i]);
            free(n->loop_inf.body);
            break;
        case NODE_LOOP_WHILE:
            node_free(n->loop_while.cond);
            for (int i = 0; i < n->loop_while.count; i++) node_free(n->loop_while.body[i]);
            free(n->loop_while.body);
            break;
        case NODE_LOOP_RANGE:
            free(n->loop_range.var);
            node_free(n->loop_range.from);
            node_free(n->loop_range.to);
            for (int i = 0; i < n->loop_range.count; i++) node_free(n->loop_range.body[i]);
            free(n->loop_range.body);
            break;
        case NODE_FUNC_DEF:
            free(n->func_def.name);
            for (int i = 0; i < n->func_def.param_count; i++) free(n->func_def.params[i].name);
            free(n->func_def.params);
            for (int i = 0; i < n->func_def.body_count; i++) node_free(n->func_def.body[i]);
            free(n->func_def.body);
            break;
        case NODE_RETURN: node_free(n->ret.value); break;
        case NODE_CALL_STMT:
        case NODE_CALL_EXPR:
            free(n->call.object);
            free(n->call.name);
            for (int i = 0; i < n->call.arg_count; i++) node_free(n->call.args[i]);
            free(n->call.args);
            break;
        case NODE_BINARY:
            node_free(n->binary.left);
            node_free(n->binary.right);
            break;
        case NODE_STR_LIT: free(n->str_lit.val); break;
        case NODE_IDENT: free(n->ident.name); break;
        case NODE_RANGE:
            node_free(n->range.from);
            node_free(n->range.to);
            break;
        default: break;
    }
    free(n);
}

static const char *type_name(GlapeType t) {
    switch (t) {
        case TYPE_INT: return "int";
        case TYPE_FLOAT: return "float";
        case TYPE_STR: return "str";
        case TYPE_BOOL: return "bool";
        case TYPE_VOID: return "void";
        default: return "?";
    }
}

static void dump(Node *n, int depth) {
    if (!n) return;
    for (int i = 0; i < depth; i++) printf("  ");

    switch (n->type) {
        case NODE_PROGRAM:
            printf("Program (%d stmts)\n", n->program.count);
            for (int i = 0; i < n->program.count; i++) dump(n->program.stmts[i], depth + 1);
            break;
        case NODE_IMPORT:
            printf("Import \"%s\"\n", n->import.name);
            break;
        case NODE_VAR_DECL:
            printf("VarDecl %s%s:%s\n",
                n->var_decl.immut ? "immut " : "",
                n->var_decl.name,
                type_name(n->var_decl.vtype));
            dump(n->var_decl.value, depth + 1);
            break;
        case NODE_ASSIGN:
            printf("Assign %s\n", n->assign.name);
            dump(n->assign.value, depth + 1);
            break;
        case NODE_IF:
            printf("If\n");
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("cond:\n");
            dump(n->if_stmt.cond, depth + 2);
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("body:\n");
            for (int i = 0; i < n->if_stmt.body_count; i++) dump(n->if_stmt.body[i], depth + 2);
            if (n->if_stmt.else_count > 0) {
                for (int i = 0; i < depth + 1; i++) printf("  ");
                printf("else:\n");
                for (int i = 0; i < n->if_stmt.else_count; i++) dump(n->if_stmt.else_body[i], depth + 2);
            }
            break;
        case NODE_LOOP_INFINITE:
            printf("Loop infinite\n");
            for (int i = 0; i < n->loop_inf.count; i++) dump(n->loop_inf.body[i], depth + 1);
            break;
        case NODE_LOOP_WHILE:
            printf("Loop while\n");
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("cond:\n");
            dump(n->loop_while.cond, depth + 2);
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("body:\n");
            for (int i = 0; i < n->loop_while.count; i++) dump(n->loop_while.body[i], depth + 2);
            break;
        case NODE_LOOP_RANGE: {
            // extract from/to values for inline display
            char from_s[32] = "?", to_s[32] = "?";
            if (n->loop_range.from->type == NODE_INT_LIT)
                snprintf(from_s, sizeof(from_s), "%lld", n->loop_range.from->int_lit.val);
            else if (n->loop_range.from->type == NODE_FLOAT_LIT)
                snprintf(from_s, sizeof(from_s), "%g", n->loop_range.from->float_lit.val);
            if (n->loop_range.to->type == NODE_INT_LIT)
                snprintf(to_s, sizeof(to_s), "%lld", n->loop_range.to->int_lit.val);
            else if (n->loop_range.to->type == NODE_FLOAT_LIT)
                snprintf(to_s, sizeof(to_s), "%g", n->loop_range.to->float_lit.val);
            printf("Loop range %s in %s%s%s\n",
                n->loop_range.var, from_s,
                n->loop_range.inclusive ? "..=" : "..",
                to_s);
            for (int i = 0; i < depth + 1; i++) printf("  ");
            printf("body:\n");
            for (int i = 0; i < n->loop_range.count; i++) dump(n->loop_range.body[i], depth + 2);
            break;
        }
        case NODE_FUNC_DEF:
            printf("FuncDef %s(", n->func_def.name);
            for (int i = 0; i < n->func_def.param_count; i++) {
                printf("%s:%s", n->func_def.params[i].name, type_name(n->func_def.params[i].type));
                if (i + 1 < n->func_def.param_count) printf(", ");
            }
            printf(") >> %s\n", type_name(n->func_def.ret_type));
            for (int i = 0; i < n->func_def.body_count; i++) dump(n->func_def.body[i], depth + 1);
            break;
        case NODE_RETURN:
            printf("Return\n");
            dump(n->ret.value, depth + 1);
            break;
        case NODE_CALL_STMT:
        case NODE_CALL_EXPR:
            if (n->call.object)
                printf("Call %s.%s (%d args)\n", n->call.object, n->call.name, n->call.arg_count);
            else
                printf("Call %s (%d args)\n", n->call.name, n->call.arg_count);
            for (int i = 0; i < n->call.arg_count; i++) dump(n->call.args[i], depth + 1);
            break;
        case NODE_BINARY: {
            const char *op = "?";
            switch (n->binary.op) {
                case TOK_PLUS: op = "+"; break;
                case TOK_MINUS: op = "-"; break;
                case TOK_STAR: op = "*"; break;
                case TOK_SLASH: op = "/"; break;
                case TOK_EQ: op = "=="; break;
                case TOK_NEQ: op = "!="; break;
                case TOK_GT: op = ">"; break;
                case TOK_LT: op = "<"; break;
                case TOK_GTE: op = ">="; break;
                case TOK_LTE: op = "<="; break;
                default: break;
            }
            printf("Binary %s\n", op);
            dump(n->binary.left, depth + 1);
            dump(n->binary.right, depth + 1);
            break;
        }
        case NODE_INT_LIT: printf("Int %lld\n", n->int_lit.val); break;
        case NODE_FLOAT_LIT: printf("Float %g\n", n->float_lit.val); break;
        case NODE_STR_LIT: printf("Str \"%s\"\n", n->str_lit.val); break;
        case NODE_BOOL_LIT: printf("Bool %s\n", n->bool_lit.val ? "true" : "false"); break;
        case NODE_IDENT: printf("Ident %s\n", n->ident.name); break;
        case NODE_RANGE:
            printf("Range %s\n", n->range.inclusive ? "..=" : "..");
            dump(n->range.from, depth + 1);
            dump(n->range.to, depth + 1);
            break;
        default:
            printf("Unknown node %d\n", n->type);
            break;
    }
}

void ast_dump(Node *root) {
    dump(root, 0);
}
