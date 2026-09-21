// Copyright (c) 2026 greqx and Contributors

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include "daemon/daemon.h"

#define GLAPE_VERSION "1.0-beta"

static void print_help(void) {
    printf("Usage: glape-daemon <command>\n");
    printf("\n");
    printf("Commands:\n");
    printf("  start                     start the daemon in background\n");
    printf("  stop                      stop the daemon\n");
    printf("  restart                   restart the daemon\n");
    printf("  status                    show daemon status\n");
    printf("  list                      list running scripts\n");
    printf("  kill <pid>                kill a running script\n");
    printf("  flush                     unload all .gdl from memory\n");
    printf("  reload                    reload .gdl without stopping\n");
    printf("\n");
    printf("Options:\n");
    printf("  --version                 print version info\n");
    printf("  --help                    print this message\n");
}

static void send_simple(MsgType type) {
    int fd = sock_connect_pub();
    if (fd < 0) {
        fprintf(stderr, "\033[91merror:\033[0m daemon not running\n");
        exit(1);
    }
    MsgHeader hdr = { type, 0, 0, 0 };
    send(fd, &hdr, sizeof(hdr), 0);

    MsgHeader resp;
    char *payload = NULL;
    recv_msg_pub(fd, &resp, &payload);
    if (payload) { printf("%s\n", payload); free(payload); }
    close(fd);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 0;
    }

    if (strcmp(argv[1], "--version") == 0) {
        printf("Glape " GLAPE_VERSION "\n");
        return 0;
    }

    if (strcmp(argv[1], "--help") == 0) {
        print_help();
        return 0;
    }

    if (strcmp(argv[1], "start") == 0) {
        return daemon_start() == 0 ? 0 : 1;
    }

    if (strcmp(argv[1], "stop") == 0) {
        daemon_stop();
        return 0;
    }

    if (strcmp(argv[1], "restart") == 0) {
        daemon_stop();
        // wait for pid file to be removed
        for (int i = 0; i < 30; i++) {
            if (!daemon_is_running()) break;
            usleep(100000);
        }
        return daemon_start() == 0 ? 0 : 1;
    }

    if (strcmp(argv[1], "status") == 0) {
        if (!daemon_is_running()) {
            printf("glape-daemon is not running\n");
            return 1;
        }
        send_simple(MSG_STATUS);
        return 0;
    }

    if (strcmp(argv[1], "list") == 0) {
        if (!daemon_is_running()) {
            printf("glape-daemon is not running\n");
            return 1;
        }
        send_simple(MSG_LIST);
        return 0;
    }

    if (strcmp(argv[1], "kill") == 0) {
        if (argc < 3) {
            fprintf(stderr, "\033[91merror:\033[0m kill requires a pid\n");
            return 1;
        }
        int target_pid = atoi(argv[2]);
        int fd = sock_connect_pub();
        if (fd < 0) {
            fprintf(stderr, "\033[91merror:\033[0m daemon not running\n");
            return 1;
        }
        MsgHeader hdr = { MSG_KILL, 0, target_pid, 0 };
        send(fd, &hdr, sizeof(hdr), 0);
        MsgHeader resp; char *p = NULL;
        recv_msg_pub(fd, &resp, &p);
        free(p);
        close(fd);
        printf("killed %d\n", target_pid);
        return 0;
    }

    if (strcmp(argv[1], "flush") == 0) {
        send_simple(MSG_FLUSH);
        return 0;
    }

    if (strcmp(argv[1], "reload") == 0) {
        send_simple(MSG_RELOAD);
        return 0;
    }

    fprintf(stderr, "\033[91merror:\033[0m unknown command '%s'\n", argv[1]);
    print_help();
    return 1;
}
