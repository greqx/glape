// Copyright (c) 2026 greqx and Contributors

#ifndef GLAPE_PARSER_H
#define GLAPE_PARSER_H

#include "../frontend/lexer.h"

// node types
typedef enum {
    // statements
    NODE_PROGRAM,
    NODE_IMPORT,
    NODE_VAR_DECL,
    NODE_ASSIGN,
    NODE_IF,
    NODE_LOOP_INFINITE,
    NODE_LOOP_WHILE,
    NODE_LOOP_RANGE,
    NODE_FUNC_DEF,
    NODE_RETURN,
    NODE_CALL_STMT,

    // expressions
    NODE_BINARY,
    NODE_INT_LIT,
    NODE_FLOAT_LIT,
    NODE_STR_LIT,
    NODE_BOOL_LIT,
    NODE_IDENT,
    NODE_CALL_EXPR,
    NODE_RANGE,
} NodeType;

// value types
typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STR,
    TYPE_BOOL,
    TYPE_VOID,
} GlapeType;

typedef struct Node Node;

// function argument: name + type
typedef struct {
    char      *name;
    GlapeType  type;
} Param;

struct Node {
    NodeType type;
    int      line;
    int      col;

    union {
        // NODE_PROGRAM
        struct { Node **stmts; int count; } program;

        // NODE_IMPORT
        struct { char *name; } import;

        // NODE_VAR_DECL: x:int = expr  /  immut x:int = expr
        struct {
            char      *name;
            GlapeType  vtype;
            int        immut;
            Node      *value;
        } var_decl;

        // NODE_ASSIGN: x = expr
        struct { char *name; Node *value; } assign;

        // NODE_IF
        struct {
            Node  *cond;
            Node **body;
            int    body_count;
            Node **else_body;
            int    else_count;
        } if_stmt;

        // NODE_LOOP_INFINITE
        struct { Node **body; int count; } loop_inf;

        // NODE_LOOP_WHILE
        struct {
            Node  *cond;
            Node **body;
            int    count;
        } loop_while;

        // NODE_LOOP_RANGE
        struct {
            char  *var;
            Node  *from;
            Node  *to;
            int    inclusive;
            Node **body;
            int    count;
        } loop_range;

        // NODE_FUNC_DEF
        struct {
            char      *name;
            Param     *params;
            int        param_count;
            GlapeType  ret_type;
            Node     **body;
            int        body_count;
        } func_def;

        // NODE_RETURN
        struct { Node *value; } ret;

        // NODE_CALL_STMT / NODE_CALL_EXPR
        // object is NULL for plain calls, set for lib.func()
        struct {
            char  *object;
            char  *name;
            Node **args;
            int    arg_count;
        } call;

        // NODE_BINARY
        struct {
            TokenType  op;
            Node      *left;
            Node      *right;
        } binary;

        // NODE_INT_LIT
        struct { long long val; } int_lit;

        // NODE_FLOAT_LIT
        struct { double val; } float_lit;

        // NODE_STR_LIT
        struct { char *val; } str_lit;

        // NODE_BOOL_LIT
        struct { int val; } bool_lit;

        // NODE_IDENT
        struct { char *name; } ident;

        // NODE_RANGE
        struct { Node *from; Node *to; int inclusive; } range;
    };
};

Node *parse(Token *tokens, int count);
void  ast_dump(Node *root);
void  node_free(Node *node);

#endif
