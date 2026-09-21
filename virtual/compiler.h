// Copyright (c) 2026 greqx and Contributors

#ifndef GLAPE_COMPILER_H
#define GLAPE_COMPILER_H

#include "../frontend/parser.h"
#include "bytecode.h"

// compile an AST program node into a Chunk
// returns NULL on error
Chunk *compile(Node *program);

// disassemble a chunk to stdout for debugging
void chunk_disasm(Chunk *c);

#endif
