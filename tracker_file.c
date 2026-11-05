/*
 * tracker_file.c - CS 4390 P2P project: .track file handling
 *
 * createtracker : make a new <filename>.track in the shared folder
 * updatetracker : add/refresh a peer's line and drop dead peers
 *
 * .track format (from the spec):
 *   Filename: <name>
 *   Filesize: <bytes>
 *   Description: <text>
 *   MD5: <md5>
 *   #list of peers follows next
 *   <ip>:<port>:<start byte>:<end byte>:<timestamp>
 *   ...
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "tracker.h"

pthread_mutex_t tracker_lock = PTHREAD_MUTEX_INITIALIZER;

/* <shared_dir>/<filename>.track */
void build_track_path(char *out, size_t n, const char *filename)
{
    snprintf(out, n, "%s/%s%s", g_cfg.shared_dir, filename, TRACK_EXT);
}

static int valid_md5(const char *s)
{
    int i;
    for (i = 0; i < 32; i++)
        if (!isxdigit((unsigned char)s[i]))
            return 0;
    return s[32] == '\0';
}

static int valid_ipv4(const char *s)
{
    struct in_addr a;
    return inet_pton(AF_INET, s, &a) == 1;
}

static int parse_port(const char *s, int *port)
{
    long long v;
    if (parse_nonneg(s, &v) < 0 || v < 1 || v > 65535)
        return -1;
    *port = (int)v;
    return 0;
}

/*
 * Read Filename / Filesize / MD5 from a .track file (used by LIST).
 * Returns 0 if all three were found, -1 otherwise.
 */
int read_tracker_header(const char *path, struct tracker_header *h)
{
    FILE *f = fopen(path, "r");
    char line[1024];
    int got = 0;

    if (!f)
        return -1;
    memset(h, 0, sizeof(*h));
    h->filesize = -1;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#')
            continue;                                  /* comments are ignored */
        if (sscanf(line, "Filename: %255s", h->filename) == 1)
            got |= 1;
        else if (sscanf(line, "Filesize: %lld", &h->filesize) == 1)
            got |= 2;
        else if (sscanf(line, "MD5: %63s", h->md5) == 1)
            got |= 4;
    }
    fclose(f);
    return (got == 7) ? 0 : -1;
}

/* A peer line is "<valid IPv4>:<port>:<start>:<end>:<timestamp>" */
static int parse_peer_line(const char *line, char ip[64], int *port,
                           long long *start, long long *end, long long *ts)
{
    if (line[0] == '#')
        return 0;
    if (sscanf(line, "%63[^:]:%d:%lld:%lld:%lld", ip, port, start, end, ts) != 5)
        return 0;
    return valid_ipv4(ip);   /* stops "Description: 1:2:3:4" being read as a peer */
}

/* ===================================================================== */
/*                             createtracker                             */
/* ===================================================================== */

/* Copy one token into a fixed-size field. Returns -1 if it doesn't fit. */
static int copy_field(char *dst, size_t size, const char *src)
{
    if (strlen(src) >= size)
        return -1;
    strcpy(dst, src);
    return 0;
}

// function handling tokenization of the message "<createtracker movie1.avi 47 Ghost_and_the_Darkness,_DVD_Ri..."
// basically takes raw string sent by peer and breaks it apart so the server can extract individual info from it
//
// msg has already had the "<", ">" and "\n" stripped by peer_handler.
// Fills the struct t (one per request, so threads don't overwrite each other).
// Returns 0 if all fields were found, -1 if the message is malformed.
int tokenize_createmsg(char *msg, struct createtracker_msg *t)
{
    char *tok[8];
    int n = split_tokens(msg, tok, 7);   // tok[0] is "createtracker"

    memset(t, 0, sizeof(*t));
    if (n == 7) {
        // createtracker filename filesize description md5 ip port
        if (copy_field(t->t_filename, sizeof(t->t_filename), tok[1]) < 0 ||    // grabs the filename
            copy_field(t->t_filesize, sizeof(t->t_filesize), tok[2]) < 0 ||    // grabs the file size
            copy_field(t->t_description, sizeof(t->t_description), tok[3]) < 0 || // grabs the file description
            copy_field(t->t_md5, sizeof(t->t_md5), tok[4]) < 0 ||              // grabs the MD5 hash
            copy_field(t->t_ip, sizeof(t->t_ip), tok[5]) < 0 ||                // grabs the ip address
            copy_field(t->t_port, sizeof(t->t_port), tok[6]) < 0)              // grabs the port
            return -1;
        return 0;
    }
    if (n == 6) {
        // description is optional in the spec: createtracker filename filesize md5 ip port
        if (copy_field(t->t_filename, sizeof(t->t_filename), tok[1]) < 0 ||
            copy_field(t->t_filesize, sizeof(t->t_filesize), tok[2]) < 0 ||
            copy_field(t->t_md5, sizeof(t->t_md5), tok[3]) < 0 ||
            copy_field(t->t_ip, sizeof(t->t_ip), tok[4]) < 0 ||
            copy_field(t->t_port, sizeof(t->t_port), tok[5]) < 0)
            return -1;
        return 0;
    }
    return -1;                           // too few or too many values
}

// handles the tracker server side of createtracker request
// Out: <createtracker succ>\n | <createtracker fail>\n | <createtracker ferr>\n
void handle_createtracker_req(int child_socket, struct createtracker_msg *t)
{
    char track_filepath[MAX_PATH_LEN];
    long long filesize;
    int port, fd, i, ok;
    FILE *f;

    // check every field before touching the disk
    // (a message like "createtracker onlyname" must get fail, not an empty tracker file)
    if (!valid_filename(t->t_filename) || parse_nonneg(t->t_filesize, &filesize) < 0 ||
        !valid_md5(t->t_md5) || !valid_ipv4(t->t_ip) || parse_port(t->t_port, &port) < 0) {
        send_str(child_socket, "<createtracker fail>\n");
        log_event("createtracker %s -> fail (invalid field)", t->t_filename);
        return;
    }

    // description is sent with '_' for spaces; store it with spaces like the spec example
    for (i = 0; t->t_description[i]; i++)
        if (t->t_description[i] == '_')
            t->t_description[i] = ' ';

    // makes track filename: <shared folder from sconfig>/<filename>.track
    // (the folder itself is created at startup in main)
    build_track_path(track_filepath, sizeof(track_filepath), t->t_filename);

    pthread_mutex_lock(&tracker_lock);

    // checks for duplicates and creates the file in ONE step (O_EXCL).
    // A separate access() check followed by fopen() lets two threads both
    // see "no file" and both create it.
    fd = open(track_filepath, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        int err = errno;
        pthread_mutex_unlock(&tracker_lock);
        if (err == EEXIST) {
            send_str(child_socket, "<createtracker ferr>\n");
            log_event("Tracker file already exists: %s -> ferr", track_filepath);
        } else {
            send_str(child_socket, "<createtracker fail>\n");
            log_event("createtracker %s -> fail (%s)", t->t_filename, strerror(err));
        }
        return;
    }

    // open the file for writing
    f = fdopen(fd, "w");
    if (f == NULL) {
        close(fd);
        unlink(track_filepath);
        pthread_mutex_unlock(&tracker_lock);
        send_str(child_socket, "<createtracker fail>\n");
        log_event("createtracker %s -> fail (cannot write file)", t->t_filename);
        return;
    }

    // create / write the tracker file in the specified format from project:
    // Filename: <file-name>
    // Filesize: <size>
    // Description: <description>
    // MD5: <md5>
    // #list of peers follows next
    // ip:port:start_byte:end_byte:timestamp
    fprintf(f, "Filename: %s\n", t->t_filename);
    fprintf(f, "Filesize: %lld\n", filesize);
    fprintf(f, "Description: %s\n", t->t_description);
    fprintf(f, "MD5: %s\n", t->t_md5);
    fprintf(f, "#list of peers follows next\n");

    // the seed has the whole file: bytes 0..filesize, stamped with the current time
    fprintf(f, "%s:%d:0:%lld:%lld\n", t->t_ip, port, filesize, (long long)time(NULL));

    ok = !ferror(f);
    if (fclose(f) != 0)
        ok = 0;
    if (!ok) {
        unlink(track_filepath);          // never leave a half-written file
        pthread_mutex_unlock(&tracker_lock);
        send_str(child_socket, "<createtracker fail>\n");
        log_event("createtracker %s -> fail (write error)", t->t_filename);
        return;
    }
    pthread_mutex_unlock(&tracker_lock);

    // send success response: <createtracker succ>\n
    send_str(child_socket, "<createtracker succ>\n");
    log_event("Tracker file created successfully: %s (%s %lld %s %s %d)", track_filepath,
              t->t_filename, filesize, t->t_md5, t->t_ip, port);
}

/* ===================================================================== */
/*                             updatetracker                             */
/* ===================================================================== */

static void reply_update(int sock, const char *fname, const char *result)
{
    char line[MAX_NAME + 64];
    snprintf(line, sizeof(line), "<updatetracker %s %s>\n", fname, result);
    send_str(sock, line);
}

/*
 * In : updatetracker filename start_byte end_byte ip-address port-number
 * Out: <updatetracker filename succ|ferr|fail>\n
 *
 * Rewrites the .track file:
 *   - sender already listed -> new start/end, timestamp = now
 *   - sender not listed     -> new line added, timestamp = now
 *   - any other peer whose timestamp is older than dead_timeout -> removed
 * The new content goes to <file>.tmp and is then rename()d over the original,
 * so the file is never left half-written.
 */
// breaks "updatetracker filename start_byte end_byte ip port" into the struct t.
// Returns 0 if well formed, -1 otherwise (t->t_filename is still filled in if present,
// so the fail reply can name the file).
int tokenize_updatemsg(char *msg, struct updatetracker_msg *t)
{
    char *tok[7];
    int n = split_tokens(msg, tok, 6);   // tok[0] is "updatetracker"

    memset(t, 0, sizeof(*t));
    strcpy(t->t_filename, "unknown");
    if (n >= 2 && strlen(tok[1]) < sizeof(t->t_filename))
        strcpy(t->t_filename, tok[1]);
    if (n != 6 || !valid_filename(tok[1]) ||
        parse_nonneg(tok[2], &t->start) < 0 || parse_nonneg(tok[3], &t->end) < 0 ||
        t->start > t->end || !valid_ipv4(tok[4]) || strlen(tok[4]) >= sizeof(t->t_ip) ||
        parse_port(tok[5], &t->port) < 0)
        return -1;
    strcpy(t->t_ip, tok[4]);
    return 0;
}

// handles the tracker server side of updatetracker request (see comment above)
void handle_updatetracker_req(int sock, struct updatetracker_msg *t)
{
    char path[MAX_PATH_LEN], tmp[MAX_PATH_LEN + 8];
    const char *fname = t->t_filename, *ip = t->t_ip;
    long long start = t->start, end = t->end, filesize = -1;
    int port = t->port, found = 0, removed = 0, ok = 1;
    time_t now;
    FILE *in, *out;
    char *line = NULL;
    size_t cap = 0;

    build_track_path(path, sizeof(path), fname);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    pthread_mutex_lock(&tracker_lock);

    in = fopen(path, "r");
    if (!in) {
        int err = errno;
        pthread_mutex_unlock(&tracker_lock);
        reply_update(sock, fname, err == ENOENT ? "ferr" : "fail");
        log_event("updatetracker %s -> %s", fname,
                  err == ENOENT ? "ferr (no such tracker file)" : "fail");
        return;
    }
    out = fopen(tmp, "w");
    if (!out) {
        fclose(in);
        pthread_mutex_unlock(&tracker_lock);
        reply_update(sock, fname, "fail");
        log_event("updatetracker %s -> fail (cannot write temp file)", fname);
        return;
    }

    now = time(NULL);
    while (getline(&line, &cap, in) != -1) {
        char pip[64];
        int pport;
        long long pstart, pend, pts;
        size_t len = strlen(line);

        if (parse_peer_line(line, pip, &pport, &pstart, &pend, &pts)) {
            if (strcmp(pip, ip) == 0 && pport == port) {
                /* existing peer: refresh its range and timestamp */
                fprintf(out, "%s:%d:%lld:%lld:%lld\n", ip, port, start, end, (long long)now);
                found = 1;
            } else if ((long long)now - pts > g_cfg.dead_timeout) {
                /* no update within the timeout: peer is dead, drop the line */
                log_event("removed dead peer %s:%d from %s%s (last update %llds ago)",
                          pip, pport, fname, TRACK_EXT, (long long)now - pts);
                removed++;
            } else {
                fputs(line, out);
            }
        } else {
            /* header and comment lines are copied unchanged */
            sscanf(line, "Filesize: %lld", &filesize);
            fputs(line, out);
            if (len > 0 && line[len - 1] != '\n')
                fputc('\n', out);
        }
    }
    free(line);
    fclose(in);

    /* a peer can't claim bytes past the end of the file */
    if (filesize >= 0 && end > filesize)
        ok = 0;

    if (ok && !found)
        fprintf(out, "%s:%d:%lld:%lld:%lld\n", ip, port, start, end, (long long)now);

    if (ferror(out))
        ok = 0;
    if (fclose(out) != 0)
        ok = 0;
    if (ok && rename(tmp, path) != 0)
        ok = 0;
    if (!ok)
        unlink(tmp);

    pthread_mutex_unlock(&tracker_lock);

    if (ok) {
        reply_update(sock, fname, "succ");
        log_event("updatetracker %s %lld-%lld %s:%d -> succ (%s%s)", fname, start, end, ip, port,
                  found ? "updated" : "new peer", removed ? ", dead peers removed" : "");
    } else {
        reply_update(sock, fname, "fail");
        log_event("updatetracker %s -> fail%s", fname,
                  (filesize >= 0 && end > filesize) ? " (end byte past file size)" : "");
    }
}
