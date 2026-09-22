# Copyright (c) 2026 greqx and Contributors

CC     = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -ldl

BUILD  = build
COMMON = frontend/lexer.c frontend/parser.c virtual/bytecode.c virtual/compiler.c virtual/vm.c virtual/gdl.c virtual/gffi.c

all: $(BUILD)/glape $(BUILD)/glape-daemon

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/glape: main.c $(COMMON) daemon/daemon.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ main.c $(COMMON) daemon/daemon.c $(LDFLAGS)

$(BUILD)/glape-daemon: main_daemon.c daemon/daemon.c $(COMMON) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ main_daemon.c daemon/daemon.c $(COMMON) $(LDFLAGS)

clean:
	rm -rf $(BUILD)
