// Copyright (c) 2026 greqx and Contributors

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include "daemon.h"
#include "../frontend/lexer.h"
#include "../frontend/parser.h"
#include "../virtual/compiler.h"
#include "../virtual/vm.h"

#define COLOR_RESET "\033[0m"
#define COLOR_RED   "\033[91m"
#define COLOR_WHITE "\033[97m"
#define COLOR_GREY  "\033[90m"

static Daemon g_daemon;

// ── per-user runtime paths ─────────────────────────────────────────────────

static char g_sock_path[108];
static char g_pid_path[256];
static char g_log_path[256];

static void init_paths(void) {
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    // sun_path is limited to 108 bytes on Linux, keep path short
    snprintf(g_sock_path, sizeof(g_sock_path), "%s/.glape/d.sock", home);
    snprintf(g_pid_path,  sizeof(g_pid_path),  "%s/.glape/d.pid",  home);
    snprintf(g_log_path,  sizeof(g_log_path),  "%s/.glape/d.log",  home);

    // ensure ~/.glape/ exists
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.glape", home);
    mkdir(dir, 0700);
}

// ── logging ────────────────────────────────────────────────────────────────

static FILE *g_log = NULL;

static void log_open(void) {
    g_log = fopen(g_log_path, "a");
}

static void log_msg(const char *msg) {
    if (!g_log) return;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
    fprintf(g_log, "[%s] %s\n", ts, msg);
    fflush(g_log);
}

// ── pid file ───────────────────────────────────────────────────────────────

static void pid_write(void) {
    FILE *f = fopen(g_pid_path, "w");
    if (f) { fprintf(f, "%d\n", getpid()); fclose(f); }
}

static int pid_read(void) {
    FILE *f = fopen(g_pid_path, "r");
    if (!f) return -1;
    int pid = -1;
    if (fscanf(f, "%d", &pid) != 1) pid = -1;
    fclose(f);
    return pid;
}

static void pid_remove(void) {
    unlink(g_pid_path);
}

// ── signal handling ────────────────────────────────────────────────────────

static void on_sigchld(int sig) {
    (void)sig;
    pid_t pid;
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        for (int i = 0; i < g_daemon.script_count; i++) {
            if (g_daemon.scripts[i].pid == pid) {
                g_daemon.scripts[i] = g_daemon.scripts[--g_daemon.script_count];
                break;
            }
        }
    }
}

static void on_sigterm(int sig) {
    (void)sig;
    g_daemon.running = 0;
}

// ── socket helpers ─────────────────────────────────────────────────────────

static int sock_create(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    // remove stale socket
    unlink(g_sock_path);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", g_sock_path);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd); return -1;
    }
    if (listen(fd, DAEMON_BACKLOG) < 0) {
        close(fd); return -1;
    }
    return fd;
}

static int sock_connect(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", g_sock_path);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd); return -1;
    }
    return fd;
}

static int send_msg(int fd, MsgType type, int payload_len, int pid, int exit_code) {
    MsgHeader hdr = { type, payload_len, pid, exit_code };
    return send(fd, &hdr, sizeof(hdr), 0);
}

static int send_msg_with_payload(int fd, MsgType type, const char *payload, int len) {
    MsgHeader hdr = { type, len, 0, 0 };
    if (send(fd, &hdr, sizeof(hdr), 0) < 0) return -1;
    if (len > 0 && send(fd, payload, len, 0) < 0) return -1;
    return 0;
}

static int recv_msg(int fd, MsgHeader *hdr, char **payload) {
    if (recv(fd, hdr, sizeof(*hdr), 0) <= 0) return -1;
    *payload = NULL;
    if (hdr->payload_len > 0) {
        *payload = malloc(hdr->payload_len + 1);
        int got = recv(fd, *payload, hdr->payload_len, MSG_WAITALL);
        if (got <= 0) { free(*payload); return -1; }
        (*payload)[got] = '\0';
    }
    return 0;
}

// ── script execution inside daemon ─────────────────────────────────────────

static void run_script_for_client(const char *src, int fd) {
    (void)fd;
    int    count;
    Token *tokens = lex(src, &count);
    Node  *ast    = parse(tokens, count);
    Chunk *chunk  = compile(ast);

    VM *vm = vm_new();
    vm_run(vm, chunk);
    vm_free(vm);

    chunk_free(chunk);
    node_free(ast);
    lex_free(tokens, count);
}

// ── main daemon loop ───────────────────────────────────────────────────────

static void daemon_loop(void) {
    signal(SIGCHLD, on_sigchld);
    signal(SIGTERM, on_sigterm);
    signal(SIGINT,  on_sigterm);
    signal(SIGPIPE, SIG_IGN);  // ignore broken pipe from clients

    g_daemon.sock_fd      = sock_create();
    g_daemon.running      = 1;
    g_daemon.script_count = 0;

    if (g_daemon.sock_fd < 0) {
        fprintf(stderr, COLOR_RED "error:" COLOR_RESET " cannot create socket\n");
        exit(1);
    }

    log_open();
    log_msg("start");

    while (g_daemon.running) {
        int client_fd = accept(g_daemon.sock_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // peek at message type before forking
        MsgHeader hdr;
        char *payload = NULL;
        if (recv_msg(client_fd, &hdr, &payload) < 0) {
            close(client_fd);
            continue;
        }

        if (hdr.type == MSG_RUN) {
            // pipe to send script_pid back to parent
            int notify[2];
            if (pipe(notify) < 0) { close(client_fd); free(payload); continue; }

            // fork so daemon stays responsive
            pid_t pid = fork();
            if (pid == 0) {
                // child handles the script
                close(notify[0]);
                close(g_daemon.sock_fd);
                if (!payload) { exit(1); }

                int pipefd[2];
                if (pipe(pipefd) < 0) { exit(1); }
                pid_t script_pid = fork();
                if (script_pid == 0) {
                    close(pipefd[0]);
                    close(notify[1]);
                    dup2(pipefd[1], STDOUT_FILENO);
                    close(pipefd[1]);
                    run_script_for_client(payload, STDOUT_FILENO);
                    exit(0);
                }
                // send script_pid to parent
                if (write(notify[1], &script_pid, sizeof(script_pid)) < 0) {}
                close(notify[1]);
                close(pipefd[1]);

                char buf[4096];
                ssize_t n;
                while ((n = read(pipefd[0], buf, sizeof(buf) - 1)) > 0) {
                    buf[n] = '\0';
                    send_msg_with_payload(client_fd, MSG_OUTPUT, buf, n);
                }
                close(pipefd[0]);

                int status;
                waitpid(script_pid, &status, 0);
                send_msg(client_fd, MSG_DONE, 0, script_pid, 0);
                close(client_fd);
                free(payload);
                exit(0);
            } else if (pid > 0) {
                // parent: read script_pid from notify pipe
                close(notify[1]);
                pid_t script_pid = -1;
                if (read(notify[0], &script_pid, sizeof(script_pid)) < 0) {}
                close(notify[0]);

                if (g_daemon.script_count < DAEMON_MAX_SCRIPTS) {
                    g_daemon.scripts[g_daemon.script_count].pid        = pid;
                    g_daemon.scripts[g_daemon.script_count].script_pid = script_pid;
                    g_daemon.scripts[g_daemon.script_count].client_fd  = client_fd;
                    g_daemon.script_count++;
                }
                close(client_fd);
            } else {
                close(notify[0]);
                close(notify[1]);
            }
            free(payload);
        } else {
            // status/list/etc - handle inline, fast
            // re-use handle_client but skip the recv (already done)
            switch (hdr.type) {
                case MSG_STATUS: {
                    // count active workers
                    int active = 0;
                    for (int i = 0; i < g_daemon.script_count; i++)
                        if (kill(g_daemon.scripts[i].pid, 0) == 0) active++;
                    char buf[256];
                    snprintf(buf, sizeof(buf),
                        "glape-daemon running  pid=%d  scripts=%d",
                        getpid(), active);
                    send_msg_with_payload(client_fd, MSG_OK, buf, strlen(buf));
                    close(client_fd);
                    break;
                }
                case MSG_LIST: {
                    char buf[4096];
                    int  off = 0;
                    for (int i = 0; i < g_daemon.script_count; i++) {
                        if (kill(g_daemon.scripts[i].script_pid, 0) == 0) {
                            off += snprintf(buf + off, sizeof(buf) - off,
                                "pid=%-6d\n", g_daemon.scripts[i].script_pid);
                        }
                    }
                    if (off == 0) snprintf(buf, sizeof(buf), "(no scripts running)");
                    send_msg_with_payload(client_fd, MSG_OK, buf, strlen(buf));
                    close(client_fd);
                    break;
                }
                case MSG_KILL: {
                    int found = 0;
                    for (int i = 0; i < g_daemon.script_count; i++) {
                        if (g_daemon.scripts[i].script_pid == hdr.pid) {
                            kill(g_daemon.scripts[i].script_pid, SIGKILL);
                            kill(g_daemon.scripts[i].pid, SIGKILL);
                            g_daemon.scripts[i] = g_daemon.scripts[--g_daemon.script_count];
                            found = 1;
                            break;
                        }
                    }
                    if (!found) kill(hdr.pid, SIGKILL);
                    send_msg(client_fd, MSG_OK, 0, 0, 0);
                    char killmsg[64];
                    snprintf(killmsg, sizeof(killmsg), "kill  pid=%d", hdr.pid);
                    log_msg(killmsg);
                    close(client_fd);
                    break;
                }
                case MSG_FLUSH:
                case MSG_RELOAD:
                    send_msg(client_fd, MSG_OK, 0, 0, 0);
                    close(client_fd);
                    break;
                case MSG_SHUTDOWN:
                    send_msg(client_fd, MSG_OK, 0, 0, 0);
                    close(client_fd);
                    g_daemon.running = 0;
                    break;
                default:
                    send_msg(client_fd, MSG_ERR, 0, 0, 0);
                    close(client_fd);
                    break;
            }
            free(payload);
        }
    }

    close(g_daemon.sock_fd);
    unlink(g_sock_path);
    pid_remove();
    if (g_log) fclose(g_log);
    log_msg("stop");
}

// ── public api ─────────────────────────────────────────────────────────────

int daemon_is_running(void) {
    init_paths();
    int pid = pid_read();
    if (pid < 0) return 0;
    return kill(pid, 0) == 0;
}

int daemon_start(void) {
    init_paths();
    if (daemon_is_running()) {
        fprintf(stderr, COLOR_RED "error:" COLOR_RESET " daemon already running\n");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid > 0) {
        // parent exits - daemon runs in background
        printf("glape-daemon started  pid=%d\n", pid);
        return 0;
    }

    // child becomes daemon
    setsid();

    // redirect stdin/stdout/stderr to /dev/null
    int devnull = open("/dev/null", O_RDWR);
    dup2(devnull, STDIN_FILENO);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    close(devnull);

    pid_write();
    daemon_loop();
    exit(0);
}

void daemon_stop(void) {
    init_paths();
    int pid = pid_read();
    if (pid > 0 && kill(pid, 0) == 0) {
        kill(pid, SIGTERM);
        // wait up to 3 seconds for daemon to exit
        for (int i = 0; i < 30; i++) {
            usleep(100000);
            if (kill(pid, 0) != 0) break;
        }
        // force kill if still running
        if (kill(pid, 0) == 0)
            kill(pid, SIGKILL);
        printf("glape-daemon stopped\n");
    } else {
        fprintf(stderr, "\033[90mwarning: daemon was not running\033[0m\n");
    }
    // always clean up files
    pid_remove();
    unlink(g_sock_path);
}

int daemon_send_run(const char *src, const char *path) {
    init_paths();
    (void)path; // used later for list/tracking
    int fd = sock_connect();
    if (fd < 0) return -1;

    send_msg_with_payload(fd, MSG_RUN, src, strlen(src));

    // read output until MSG_DONE
    while (1) {
        MsgHeader hdr;
        char     *payload = NULL;
        if (recv_msg(fd, &hdr, &payload) < 0) break;
        if (hdr.type == MSG_OUTPUT && payload)
            printf("%s", payload);
        if (hdr.type == MSG_DONE) {
            free(payload);
            break;
        }
        free(payload);
    }

    close(fd);
    return 0;
}

int sock_connect_pub(void) {
    init_paths();
    return sock_connect();
}

int recv_msg_pub(int fd, MsgHeader *hdr, char **payload) {
    return recv_msg(fd, hdr, payload);
}
