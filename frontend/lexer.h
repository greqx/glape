// Copyright (c) 2026 greqx and Contributors

#ifndef GLAPE_LEXER_H
#define GLAPE_LEXER_H

typedef enum {
    // literals
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_TRUE,
    TOK_FALSE,

    // identifier and types
    TOK_IDENT,
    TOK_TYPE_INT,
    TOK_TYPE_FLOAT,
    TOK_TYPE_STR,
    TOK_TYPE_BOOL,

    // keywords
    TOK_GET,
    TOK_IMMUT,
    TOK_DEFINE,
    TOK_RETURN,
    TOK_IN,
    TOK_IF,
    TOK_ELSE,
    TOK_LOOP,

    // operators
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_EQ,
    TOK_NEQ,
    TOK_GT,
    TOK_LT,
    TOK_GTE,
    TOK_LTE,
    TOK_ASSIGN,

    // punctuation
    TOK_COLON,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_DOT,
    TOK_RANGE,
    TOK_RANGE_INCL,
    TOK_ARROW,

    // structure
    TOK_NEWLINE,
    TOK_INDENT,
    TOK_DEDENT,

    TOK_COMMA,

    // service
    TOK_EOF,
    TOK_UNKNOWN,
} TokenType;

typedef struct {
    TokenType type;
    char *value;
    int line;
    int col;
} Token;

Token *lex(const char *src, int *out_count);
void lex_dump(Token *tokens, int count);
void lex_free(Token *tokens, int count);

#endif
