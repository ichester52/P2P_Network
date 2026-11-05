/*
 * tracker.h - shared declarations for the tracker server
 *
 *   tracker.c       : startup, sconfig, worker threads, LIST, GET, helpers
 *   tracker_file.c  : createtracker, updatetracker, .track file handling
 */
#ifndef TRACKER_H
#define TRACKER_H

#include <pthread.h>
#include <stddef.h>

#define MAX_MSG       2048   /* longest protocol message we accept */
#define MAX_NAME      256    /* longest file name */
#define MAX_PATH_LEN  1024
#define TRACK_EXT     ".track"

/* Values read from sconfig */
struct tracker_config {
    int  port;                     /* line 1: port the tracker listens on */
    char shared_dir[MAX_NAME];     /* line 2: folder holding .track files (torrents) */
    int  dead_timeout;             /* line 3: seconds without an update before a peer is dead */
};

/* Header fields of a .track file */
struct tracker_header {
    char      filename[MAX_NAME];
    long long filesize;
    char      md5[64];
};

extern struct tracker_config g_cfg;

/* One global lock for every read-modify-write of a .track file.
 * Works because all workers are threads in the same process. */
extern pthread_mutex_t tracker_lock;

/* ---- helpers (tracker.c) ---- */
int  send_all(int sock, const char *buf, size_t len);
int  send_str(int sock, const char *s);
void log_event(const char *fmt, ...);
int  split_tokens(char *s, char *tok[], int max);
int  valid_filename(const char *name);
int  parse_nonneg(const char *s, long long *out);

/* Parsed createtracker message. These used to be global t_* variables;
 * with threads each request needs its own copy or they overwrite each other. */
struct createtracker_msg {
    char t_filename[MAX_NAME];
    char t_filesize[32];
    char t_description[MAX_MSG];
    char t_md5[64];
    char t_ip[64];
    char t_port[16];
};

/* Parsed updatetracker message */
struct updatetracker_msg {
    char      t_filename[MAX_NAME];
    long long start;
    long long end;
    char      t_ip[64];
    int       port;
};

/* ---- tracker file handling (tracker_file.c) ---- */
int  tokenize_createmsg(char *msg, struct createtracker_msg *t);
void handle_createtracker_req(int child_socket, struct createtracker_msg *t);
int  tokenize_updatemsg(char *msg, struct updatetracker_msg *t);
void handle_updatetracker_req(int sock, struct updatetracker_msg *t);
int  read_tracker_header(const char *path, struct tracker_header *h);
void build_track_path(char *out, size_t n, const char *filename);

#endif
