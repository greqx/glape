// Copyright (c) 2026 greqx and Contributors

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"

#define MAX_TOKENS 4096
#define MAX_STR    512

// color codes for error output
#define COLOR_RESET  "\033[0m"
#define COLOR_RED    "\033[91m"
#define COLOR_WHITE  "\033[97m"
#define COLOR_GREY   "\033[90m"

typedef struct {
    const char *src;
    int         pos;
    int         line;
    int         col;
    int         indent_stack[256];
    int         indent_top;
} Lexer;

static void lex_error(Lexer *l, const char *msg) {
    fprintf(stderr,
        COLOR_RED "error:" COLOR_RESET
        COLOR_WHITE " %s " COLOR_RESET
        COLOR_GREY "(%d:%d)\n" COLOR_RESET,
        msg, l->line, l->col);
    exit(1);
}

static char peek(Lexer *l) {
    return l->src[l->pos];
}

static char peek2(Lexer *l) {
    return l->src[l->pos + 1];
}

static char advance(Lexer *l) {
    char c = l->src[l->pos++];
    if (c == '\n') {
        l->line++;
        l->col = 1;
    } else {
        l->col++;
    }
    return c;
}

static Token make_token(Lexer *l, TokenType type, const char *value) {
    Token t;
    t.type  = type;
    t.value = value ? strdup(value) : NULL;
    t.line  = l->line;
    t.col   = l->col;
    return t;
}

// returns keyword token type if word is a keyword, TOK_IDENT otherwise
// words after a dot are never keywords
static TokenType keyword_or_ident(const char *word, int after_dot) {
    if (after_dot) return TOK_IDENT;
    if (strcmp(word, "get")    == 0) return TOK_GET;
    if (strcmp(word, "immut")  == 0) return TOK_IMMUT;
    if (strcmp(word, "define") == 0) return TOK_DEFINE;
    if (strcmp(word, "return") == 0) return TOK_RETURN;
    if (strcmp(word, "in")     == 0) return TOK_IN;
    if (strcmp(word, "if")     == 0) return TOK_IF;
    if (strcmp(word, "else")   == 0) return TOK_ELSE;
    if (strcmp(word, "loop")   == 0) return TOK_LOOP;
    if (strcmp(word, "true")   == 0) return TOK_TRUE;
    if (strcmp(word, "false")  == 0) return TOK_FALSE;
    if (strcmp(word, "int")    == 0) return TOK_TYPE_INT;
    if (strcmp(word, "float")  == 0) return TOK_TYPE_FLOAT;
    if (strcmp(word, "str")    == 0) return TOK_TYPE_STR;
    if (strcmp(word, "bool")   == 0) return TOK_TYPE_BOOL;
    return TOK_IDENT;
}

static int count_indent(Lexer *l) {
    int count = 0;
    int pos = l->pos;
    while (l->src[pos] == ' ') {
        count++;
        pos++;
    }
    return count;
}

Token *lex(const char *src, int *out_count) {
    Lexer l;
    l.src        = src;
    l.pos        = 0;
    l.line       = 1;
    l.col        = 1;
    l.indent_top = 0;
    l.indent_stack[0] = 0;

    Token *tokens = malloc(sizeof(Token) * MAX_TOKENS);
    int    count  = 0;

    // tracks whether the previous token was a dot, for keyword suppression
    int after_dot = 0;

    while (peek(&l) != '\0') {
        char c = peek(&l);

        // skip comments
        if (c == '#') {
            while (peek(&l) != '\n' && peek(&l) != '\0')
                advance(&l);
            continue;
        }

        // newline and indentation
        if (c == '\n') {
            advance(&l);
            after_dot = 0;

            // skip all consecutive blank lines
            while (peek(&l) == '\n')
                advance(&l);

            if (peek(&l) == '\0') break;

            // skip comment-only lines
            while (peek(&l) == '#') {
                while (peek(&l) != '\n' && peek(&l) != '\0')
                    advance(&l);
                if (peek(&l) == '\n') advance(&l);
                while (peek(&l) == '\n') advance(&l);
            }

            if (peek(&l) == '\0') break;

            int indent = count_indent(&l);
            int prev   = l.indent_stack[l.indent_top];

            // only emit NEWLINE when we're at the same indent level
            // (indent/dedent changes are structural, not newlines)
            if (indent == prev)
                tokens[count++] = make_token(&l, TOK_NEWLINE, NULL);

            if (indent > prev) {
                l.indent_stack[++l.indent_top] = indent;
                tokens[count++] = make_token(&l, TOK_INDENT, NULL);
                for (int i = 0; i < indent; i++) advance(&l);
            } else if (indent < prev) {
                while (l.indent_stack[l.indent_top] > indent) {
                    l.indent_top--;
                    tokens[count++] = make_token(&l, TOK_DEDENT, NULL);
                }
                if (l.indent_stack[l.indent_top] != indent)
                    lex_error(&l, "inconsistent indentation");
                for (int i = 0; i < indent; i++) advance(&l);
            } else {
                for (int i = 0; i < indent; i++) advance(&l);
            }
            continue;
        }

        // skip spaces (not at line start, those are handled above)
        if (c == ' ' || c == '\t') {
            advance(&l);
            continue;
        }

        // string literal
        if (c == '"') {
            advance(&l);
            char buf[MAX_STR];
            int  len = 0;
            while (peek(&l) != '"' && peek(&l) != '\0') {
                if (len >= MAX_STR - 1)
                    lex_error(&l, "string literal too long");
                buf[len++] = advance(&l);
            }
            if (peek(&l) == '\0')
                lex_error(&l, "unterminated string literal");
            advance(&l);
            buf[len] = '\0';
            tokens[count++] = make_token(&l, TOK_STRING, buf);
            after_dot = 0;
            continue;
        }

        // number literal (int or float)
        if (isdigit(c)) {
            char buf[MAX_STR];
            int  len    = 0;
            int  is_flt = 0;
            while (isdigit(peek(&l))) {
                buf[len++] = advance(&l);
            }
            // check for float: digit(s) dot digit, not range (..)
            if (peek(&l) == '.' && isdigit(peek2(&l))) {
                is_flt = 1;
                buf[len++] = advance(&l);
                while (isdigit(peek(&l)))
                    buf[len++] = advance(&l);
            }
            buf[len] = '\0';
            tokens[count++] = make_token(&l, is_flt ? TOK_FLOAT : TOK_INT, buf);
            after_dot = 0;
            continue;
        }

        // identifier or keyword
        if (isalpha(c) || c == '_') {
            char buf[MAX_STR];
            int  len = 0;
            while (isalnum(peek(&l)) || peek(&l) == '_')
                buf[len++] = advance(&l);
            buf[len] = '\0';
            TokenType type = keyword_or_ident(buf, after_dot);
            tokens[count++] = make_token(&l, type, buf);
            after_dot = 0;
            continue;
        }

        // two-char and one-char operators
        advance(&l);
        after_dot = 0;

        if (c == '.' && peek(&l) == '.' && peek2(&l) == '=') {
            advance(&l); advance(&l);
            tokens[count++] = make_token(&l, TOK_RANGE_INCL, NULL);
        } else if (c == '.' && peek(&l) == '.') {
            advance(&l);
            tokens[count++] = make_token(&l, TOK_RANGE, NULL);
        } else if (c == '.' ) {
            tokens[count++] = make_token(&l, TOK_DOT, NULL);
            after_dot = 1;
        } else if (c == '>' && peek(&l) == '>') {
            advance(&l);
            tokens[count++] = make_token(&l, TOK_ARROW, NULL);
        } else if (c == '>' && peek(&l) == '=') {
            advance(&l);
            tokens[count++] = make_token(&l, TOK_GTE, NULL);
        } else if (c == '<' && peek(&l) == '=') {
            advance(&l);
            tokens[count++] = make_token(&l, TOK_LTE, NULL);
        } else if (c == '=' && peek(&l) == '=') {
            advance(&l);
            tokens[count++] = make_token(&l, TOK_EQ, NULL);
        } else if (c == '!' && peek(&l) == '=') {
            advance(&l);
            tokens[count++] = make_token(&l, TOK_NEQ, NULL);
        } else if (c == '=') {
            tokens[count++] = make_token(&l, TOK_ASSIGN, NULL);
        } else if (c == '>') {
            tokens[count++] = make_token(&l, TOK_GT, NULL);
        } else if (c == '<') {
            tokens[count++] = make_token(&l, TOK_LT, NULL);
        } else if (c == '+') {
            tokens[count++] = make_token(&l, TOK_PLUS, NULL);
        } else if (c == '-') {
            tokens[count++] = make_token(&l, TOK_MINUS, NULL);
        } else if (c == '*') {
            tokens[count++] = make_token(&l, TOK_STAR, NULL);
        } else if (c == '/') {
            tokens[count++] = make_token(&l, TOK_SLASH, NULL);
        } else if (c == ':') {
            tokens[count++] = make_token(&l, TOK_COLON, NULL);
        } else if (c == '(') {
            tokens[count++] = make_token(&l, TOK_LPAREN, NULL);
        } else if (c == ')') {
            tokens[count++] = make_token(&l, TOK_RPAREN, NULL);
        } else if (c == ',') {
            tokens[count++] = make_token(&l, TOK_COMMA, NULL);
        } else {
            char msg[64];
            snprintf(msg, sizeof(msg), "unknown character '%c'", c);
            lex_error(&l, msg);
        }
    }

    // emit remaining dedents
    while (l.indent_top > 0) {
        l.indent_top--;
        tokens[count++] = make_token(&l, TOK_DEDENT, NULL);
    }

    tokens[count++] = make_token(&l, TOK_EOF, NULL);
    *out_count = count;
    return tokens;
}

// returns a human-readable name for a token type
static const char *tok_name(TokenType t) {
    switch (t) {
        case TOK_INT:        return "INT";
        case TOK_FLOAT:      return "FLOAT";
        case TOK_STRING:     return "STRING";
        case TOK_TRUE:       return "TRUE";
        case TOK_FALSE:      return "FALSE";
        case TOK_IDENT:      return "IDENT";
        case TOK_TYPE_INT:   return "TYPE_INT";
        case TOK_TYPE_FLOAT: return "TYPE_FLOAT";
        case TOK_TYPE_STR:   return "TYPE_STR";
        case TOK_TYPE_BOOL:  return "TYPE_BOOL";
        case TOK_GET:        return "GET";
        case TOK_IMMUT:      return "IMMUT";
        case TOK_DEFINE:     return "DEFINE";
        case TOK_RETURN:     return "RETURN";
        case TOK_IN:         return "IN";
        case TOK_IF:         return "IF";
        case TOK_ELSE:       return "ELSE";
        case TOK_LOOP:       return "LOOP";
        case TOK_PLUS:       return "PLUS";
        case TOK_MINUS:      return "MINUS";
        case TOK_STAR:       return "STAR";
        case TOK_SLASH:      return "SLASH";
        case TOK_EQ:         return "EQ";
        case TOK_NEQ:        return "NEQ";
        case TOK_GT:         return "GT";
        case TOK_LT:         return "LT";
        case TOK_GTE:        return "GTE";
        case TOK_LTE:        return "LTE";
        case TOK_ASSIGN:     return "ASSIGN";
        case TOK_COLON:      return "COLON";
        case TOK_LPAREN:     return "LPAREN";
        case TOK_RPAREN:     return "RPAREN";
        case TOK_DOT:        return "DOT";
        case TOK_RANGE:      return "RANGE";
        case TOK_RANGE_INCL: return "RANGE_INCL";
        case TOK_ARROW:      return "ARROW";
        case TOK_NEWLINE:    return "NEWLINE";
        case TOK_INDENT:     return "INDENT";
        case TOK_DEDENT:     return "DEDENT";
        case TOK_COMMA:      return "COMMA";
        case TOK_EOF:        return "EOF";
        case TOK_UNKNOWN:    return "UNKNOWN";
        default:             return "?";
    }
}

void lex_dump(Token *tokens, int count) {
    for (int i = 0; i < count; i++) {
        Token *t = &tokens[i];
        if (t->value)
            printf("%-12s %s  (%d:%d)\n", tok_name(t->type), t->value, t->line, t->col);
        else
            printf("%-12s (%d:%d)\n", tok_name(t->type), t->line, t->col);
    }
}

void lex_free(Token *tokens, int count) {
    for (int i = 0; i < count; i++)
        free(tokens[i].value);
    free(tokens);
}
