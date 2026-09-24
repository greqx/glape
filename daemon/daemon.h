// Copyright (c) 2026 greqx and Contributors

#ifndef GLAPE_DAEMON_H
#define GLAPE_DAEMON_H

#ifdef __APPLE__
  #define DAEMON_SOCK_DIR "/tmp/glape"
#else
  #define DAEMON_SOCK_DIR "/tmp/glape"
#endif

// actual runtime paths are resolved per-user at runtime via get_runtime_dir()
// these are fallbacks only
#define DAEMON_SOCK_PATH DAEMON_SOCK_DIR "/daemon.sock"
#define DAEMON_PID_PATH DAEMON_SOCK_DIR "/daemon.pid"
#define DAEMON_LOG_PATH DAEMON_SOCK_DIR "/daemon.log"

#define DAEMON_MAX_CLIENTS 256
#define DAEMON_MAX_SCRIPTS 256
#define DAEMON_BACKLOG 16

// message types between glape client and daemon
typedef enum {
    MSG_RUN, // client sends script source, daemon runs it
    MSG_STATUS, // client asks for status
    MSG_LIST, // client asks for running scripts
    MSG_KILL, // client asks to kill a script by pid
    MSG_FLUSH, // client asks to unload all .gdl
    MSG_RELOAD, // client asks to reload .gdl
    MSG_SHUTDOWN, // client asks daemon to stop
    MSG_OK, // daemon ack
    MSG_ERR, // daemon error
    MSG_OUTPUT, // daemon sends script output back to client
    MSG_DONE, // daemon signals script finished
} MsgType;

// fixed-size message header sent over the socket
// payload follows immediately after if payload_len > 0
typedef struct {
    MsgType type;
    int payload_len;
    int pid; // script pid (for kill/list)
    int exit_code; // for MSG_DONE
} MsgHeader;

// one running script tracked by the daemon
typedef struct {
    int pid; // worker process pid
    int script_pid; // actual script process pid (what to kill)
    char path[512];
    int client_fd;
} ScriptEntry;

// daemon state
typedef struct {
    int sock_fd;
    int running;
    ScriptEntry scripts[DAEMON_MAX_SCRIPTS];
    int script_count;
} Daemon;

// daemon lifecycle
int daemon_start(void);
void daemon_stop(void);
int daemon_is_running(void);

// client side - send a script to the daemon
int daemon_send_run(const char *src, const char *path);

// low-level socket helpers for main_daemon.c
int sock_connect_pub(void);
int recv_msg_pub(int fd, MsgHeader *hdr, char **payload);

#endif
