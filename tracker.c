/*
 * tracker.c - CS 4390 P2P project: tracker server core
 *
 * - Reads port, shared folder and dead-peer timeout from sconfig
 * - Accepts connections and hands each one to its own worker thread
 * - Worker reads ONE command, sends the reply, closes the connection
 * - Handles LIST and GET here; createtracker / updatetracker live in tracker_file.c
 *
 * Build: make        Run: cd TrackerFolder && ./tracker [config-file]
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "md5.h"
#include "tracker.h"

#define BACKLOG          20
#define FIRST_BYTE_MS    10000  /* how long to wait for a client to start talking */
#define IDLE_MS          300    /* after some data, wait this long for more if no '\n' yet */

struct tracker_config g_cfg;

/* Passed from the accept loop to each worker thread */
struct worker_arg {
    int  sock;
    char ip[INET_ADDRSTRLEN];
};

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

/* ===================================================================== */
/*                               helpers                                 */
/* ===================================================================== */

/* Print one event line. Locked so lines from different threads don't mix. */
void log_event(const char *fmt, ...)
{
    va_list ap;
    pthread_mutex_lock(&log_lock);
    printf("Tracker: ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
    pthread_mutex_unlock(&log_lock);
}

/* write() can send less than asked; keep going until everything is out. */
int send_all(int sock, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = send(sock, buf, len, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

int send_str(int sock, const char *s)
{
    return send_all(sock, s, strlen(s));
}

/* Split s on spaces/tabs (modifies s). Returns token count, or -1 if more than max. */
int split_tokens(char *s, char *tok[], int max)
{
    int n = 0;
    char *save = NULL;
    char *p = strtok_r(s, " \t", &save);
    while (p) {
        if (n == max)
            return -1;
        tok[n++] = p;
        p = strtok_r(NULL, " \t", &save);
    }
    return n;
}

/* Reject names that could escape the shared folder (e.g. "../x", "a/b"). */
int valid_filename(const char *name)
{
    size_t len = strlen(name);
    if (len == 0 || len >= MAX_NAME - strlen(TRACK_EXT))
        return 0;
    if (strchr(name, '/') || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;
    return 1;
}

/* Parse a non-negative whole number. Returns 0 on success, -1 if not a number. */
int parse_nonneg(const char *s, long long *out)
{
    char *end;
    long long v;
    if (!s || !isdigit((unsigned char)s[0]))
        return -1;
    errno = 0;
    v = strtoll(s, &end, 10);
    if (errno || *end != '\0')
        return -1;
    *out = v;
    return 0;
}

/*
 * Read one command from the socket. Stops at '\n', when the client closes,
 * or when the client goes quiet. The quiet-timeout means a client that forgets
 * the trailing '\n' (like the current peer.c) still gets an answer.
 * Returns bytes read (0 = nothing arrived), -1 on error.
 */
static ssize_t read_message(int sock, char *buf, size_t size)
{
    size_t used = 0;
    int timeout = FIRST_BYTE_MS;

    while (used < size - 1) {
        struct pollfd p;
        ssize_t n;
        int r;

        p.fd = sock;
        p.events = POLLIN;
        p.revents = 0;
        r = poll(&p, 1, timeout);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            break;                         /* timed out: use what we have */

        n = recv(sock, buf + used, size - 1 - used, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            break;                         /* client closed its side */
        used += (size_t)n;
        if (memchr(buf + used - n, '\n', (size_t)n))
            break;                         /* got a full line */
        timeout = IDLE_MS;
    }
    buf[used] = '\0';
    return (ssize_t)used;
}

/*
 * Turn "<createtracker a b c>\n" or "createtracker a b c" into "createtracker a b c",
 * so the spec format and the manual-typing format are handled the same way.
 */
static char *normalize_msg(char *s)
{
    char *nl = strchr(s, '\n');
    size_t len;

    if (nl)
        *nl = '\0';                        /* one command per connection */
    while (*s == '<' || isspace((unsigned char)*s))
        s++;
    len = strlen(s);
    while (len > 0 && (s[len - 1] == '>' || isspace((unsigned char)s[len - 1])))
        s[--len] = '\0';
    return s;
}

/* ===================================================================== */
/*                               sconfig                                 */
/* ===================================================================== */

/*
 * sconfig format (blank lines and lines starting with # are skipped):
 *   line 1: port
 *   line 2: shared folder for .track files (e.g. torrents)
 *   line 3: dead-peer timeout in seconds (optional, default 900)
 */
static int load_config(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    int field = 0;

    g_cfg.port = -1;
    g_cfg.shared_dir[0] = '\0';
    g_cfg.dead_timeout = 900;

    if (!f) {
        fprintf(stderr, "Tracker: cannot open config file '%s': %s\n", path, strerror(errno));
        return -1;
    }
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        size_t len;
        while (isspace((unsigned char)*s))
            s++;
        len = strlen(s);
        while (len > 0 && isspace((unsigned char)s[len - 1]))
            s[--len] = '\0';
        if (*s == '\0' || *s == '#')
            continue;

        if (field == 0) {
            long long v;
            if (parse_nonneg(s, &v) < 0 || v < 1 || v > 65535) {
                fprintf(stderr, "Tracker: bad port '%s' in %s\n", s, path);
                fclose(f);
                return -1;
            }
            g_cfg.port = (int)v;
        } else if (field == 1) {
            if (len >= sizeof(g_cfg.shared_dir)) {
                fprintf(stderr, "Tracker: shared folder name too long in %s\n", path);
                fclose(f);
                return -1;
            }
            strcpy(g_cfg.shared_dir, s);
        } else if (field == 2) {
            long long v;
            if (parse_nonneg(s, &v) < 0 || v < 1) {
                fprintf(stderr, "Tracker: bad dead-peer timeout '%s' in %s\n", s, path);
                fclose(f);
                return -1;
            }
            g_cfg.dead_timeout = (int)v;
        }
        field++;
    }
    fclose(f);

    if (g_cfg.port < 0 || g_cfg.shared_dir[0] == '\0') {
        fprintf(stderr, "Tracker: %s must have a port line and a shared folder line\n", path);
        return -1;
    }
    return 0;
}

/* ===================================================================== */
/*                                 LIST                                  */
/* ===================================================================== */

static int cmp_header(const void *a, const void *b)
{
    return strcmp(((const struct tracker_header *)a)->filename,
                  ((const struct tracker_header *)b)->filename);
}

/*
 * Reply:
 *   <REP LIST X>\n
 *   <1 name size md5>\n ...
 *   <REP LIST END>\n
 */
static void handle_list_req(int sock, const char *client_ip)
{
    DIR *d;
    struct dirent *e;
    struct tracker_header *list = NULL;
    int count = 0, cap = 0, i;
    char line[MAX_NAME + 128];

    pthread_mutex_lock(&tracker_lock);
    d = opendir(g_cfg.shared_dir);
    if (d) {
        while ((e = readdir(d)) != NULL) {
            size_t len = strlen(e->d_name), ext = strlen(TRACK_EXT);
            char path[MAX_PATH_LEN];
            struct tracker_header h;

            /* only "<name>.track" files (skips ., .., and *.tmp leftovers) */
            if (len <= ext || strcmp(e->d_name + len - ext, TRACK_EXT) != 0)
                continue;
            snprintf(path, sizeof(path), "%s/%s", g_cfg.shared_dir, e->d_name);
            if (read_tracker_header(path, &h) < 0)
                continue;                  /* skip unreadable / malformed files */

            if (count == cap) {
                struct tracker_header *bigger;
                cap = cap ? cap * 2 : 16;
                bigger = realloc(list, (size_t)cap * sizeof(*list));
                if (!bigger)
                    break;
                list = bigger;
            }
            list[count++] = h;
        }
        closedir(d);
    }
    pthread_mutex_unlock(&tracker_lock);

    qsort(list, (size_t)count, sizeof(*list), cmp_header);

    snprintf(line, sizeof(line), "<REP LIST %d>\n", count);
    if (send_str(sock, line) < 0)
        goto done;
    for (i = 0; i < count; i++) {
        snprintf(line, sizeof(line), "<%d %s %lld %s>\n",
                 i + 1, list[i].filename, list[i].filesize, list[i].md5);
        if (send_str(sock, line) < 0)
            goto done;
    }
    send_str(sock, "<REP LIST END>\n");
    log_event("LIST from %s -> %d file(s)", client_ip, count);

done:
    free(list);
}

/* ===================================================================== */
/*                                  GET                                  */
/* ===================================================================== */

/*
 * Reply:
 *   <REP GET BEGIN>\n
 *   ...exact bytes of the .track file...
 *   <REP GET END md5-of-those-bytes>\n
 *
 * Assumptions (not in the spec): missing file -> <REP GET ferr>\n,
 * any other problem -> <REP GET fail>\n
 */
/*
 * Pull the tracker name out of "GET movie1.avi.track" into fname, without
 * the ".track" ending ("GET movie1.avi" is accepted too).
 * Returns 0 on success, -1 if the command is malformed or the name is unsafe.
 */
static int xtrct_fname(char *msg, char fname[MAX_NAME])
{
    char *tok[3];
    size_t len, ext = strlen(TRACK_EXT);

    if (split_tokens(msg, tok, 3) != 2 || strlen(tok[1]) >= MAX_NAME)
        return -1;
    strcpy(fname, tok[1]);
    len = strlen(fname);
    if (len > ext && strcmp(fname + len - ext, TRACK_EXT) == 0)
        fname[len - ext] = '\0';
    return valid_filename(fname) ? 0 : -1;
}

static void handle_get_req(int sock, const char *name, const char *client_ip)
{
    char path[MAX_PATH_LEN], md5[33], endline[64];
    char *buf = NULL;
    long size;
    FILE *f;

    build_track_path(path, sizeof(path), name);

    /* Read the whole file under the lock so content and MD5 always match */
    pthread_mutex_lock(&tracker_lock);
    f = fopen(path, "rb");
    if (!f) {
        int err = errno;
        pthread_mutex_unlock(&tracker_lock);
        send_str(sock, err == ENOENT ? "<REP GET ferr>\n" : "<REP GET fail>\n");
        log_event("GET %s%s from %s -> %s", name, TRACK_EXT, client_ip,
                  err == ENOENT ? "ferr (no such tracker file)" : "fail");
        return;
    }
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0 &&
        (buf = malloc((size_t)size + 1)) != NULL &&
        fread(buf, 1, (size_t)size, f) == (size_t)size) {
        fclose(f);
        pthread_mutex_unlock(&tracker_lock);
    } else {
        fclose(f);
        pthread_mutex_unlock(&tracker_lock);
        free(buf);
        send_str(sock, "<REP GET fail>\n");
        log_event("GET %s%s from %s -> fail (read error)", name, TRACK_EXT, client_ip);
        return;
    }

    md5_hex(buf, (size_t)size, md5);
    snprintf(endline, sizeof(endline), "<REP GET END %s>\n", md5);

    if (send_str(sock, "<REP GET BEGIN>\n") == 0 &&
        send_all(sock, buf, (size_t)size) == 0 &&
        /* our files always end in '\n'; add one if not so END is on its own line */
        (size == 0 || buf[size - 1] == '\n' || send_str(sock, "\n") == 0) &&
        send_str(sock, endline) == 0)
        log_event("GET %s%s from %s -> sent %ld bytes (md5 %s)", name, TRACK_EXT, client_ip, size, md5);
    free(buf);
}

/* ===================================================================== */
/*                         worker thread + main                          */
/* ===================================================================== */

/*
 * Read one command from the peer and call the matching handler.
 * Accepts the spec format ("<REQ LIST>\n") and the manual format ("REQ LIST").
 */
static void peer_handler(int sock_child, const char *client_ip)
{
    char raw[MAX_MSG], cmdcopy[MAX_MSG];
    char *msg, *first;
    char *save = NULL;
    ssize_t n = read_message(sock_child, raw, sizeof(raw));

    if (n <= 0) {
        if (n < 0)
            log_event("read error from %s: %s", client_ip, strerror(errno));
        return;
    }

    msg = normalize_msg(raw);
    snprintf(cmdcopy, sizeof(cmdcopy), "%s", msg);   /* split_tokens edits its input */
    first = strtok_r(cmdcopy, " \t", &save);
    if (!first) {
        send_str(sock_child, "<ERR empty command>\n");
        return;
    }

    if (strcasecmp(first, "REQ") == 0) {                  /* list command received */
        char *second = strtok_r(NULL, " \t", &save);
        if (second && strcasecmp(second, "LIST") == 0 && !strtok_r(NULL, " \t", &save))
            handle_list_req(sock_child, client_ip);
        else
            send_str(sock_child, "<ERR unknown command>\n");
    } else if (strcasecmp(first, "GET") == 0) {           /* get command received */
        char fname[MAX_NAME];
        if (xtrct_fname(msg, fname) == 0) {
            handle_get_req(sock_child, fname, client_ip);
        } else {
            send_str(sock_child, "<REP GET fail>\n");
            log_event("GET from %s -> fail (bad format or file name)", client_ip);
        }
    } else if (strcasecmp(first, "createtracker") == 0) { /* createtracker command received */
        struct createtracker_msg t;
        if (tokenize_createmsg(msg, &t) == 0) {
            handle_createtracker_req(sock_child, &t);
        } else {
            send_str(sock_child, "<createtracker fail>\n");
            log_event("createtracker from %s -> fail (wrong number of values)", client_ip);
        }
    } else if (strcasecmp(first, "updatetracker") == 0) { /* updatetracker command received */
        struct updatetracker_msg t;
        if (tokenize_updatemsg(msg, &t) == 0) {
            handle_updatetracker_req(sock_child, &t);
        } else {
            char reply[MAX_NAME + 64];
            snprintf(reply, sizeof(reply), "<updatetracker %s fail>\n", t.t_filename);
            send_str(sock_child, reply);
            log_event("updatetracker %s from %s -> fail (bad format or invalid field)",
                      t.t_filename, client_ip);
        }
    } else {
        send_str(sock_child, "<ERR unknown command>\n");
        log_event("unknown command from %s: %s", client_ip, msg);
    }
}

/* Worker thread: serves exactly one peer connection, then closes it and exits.
 * (Replaces the old fork() child process.) */
static void *worker(void *p)
{
    struct worker_arg *arg = p;
    peer_handler(arg->sock, arg->ip);
    close(arg->sock);
    free(arg);
    return NULL;
}

int main(int argc, char *argv[])
{
    const char *cfg_path = (argc > 1) ? argv[1] : "sconfig";
    struct sockaddr_in server_addr;
    pthread_attr_t attr;
    int listen_sock, yes = 1;

    /* Writing to a socket the peer already closed must not kill the server */
    signal(SIGPIPE, SIG_IGN);

    if (load_config(cfg_path) < 0)
        return 1;

    /* Create the shared folder if it isn't there yet */
    if (mkdir(g_cfg.shared_dir, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "Tracker: cannot create folder '%s': %s\n", g_cfg.shared_dir, strerror(errno));
        return 1;
    }

    if ((listen_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Tracker: socket");
        return 1;
    }
    /* Lets you restart the tracker right away without "Address already in use" */
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((unsigned short)g_cfg.port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);   /* all interfaces */

    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Tracker: bind");
        close(listen_sock);
        return 1;
    }
    if (listen(listen_sock, BACKLOG) < 0) {
        perror("Tracker: listen");
        close(listen_sock);
        return 1;
    }

    /* Worker threads clean themselves up when they finish (no join needed) */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    log_event("listening on port %d, tracker files in '%s/', dead-peer timeout %ds",
              g_cfg.port, g_cfg.shared_dir, g_cfg.dead_timeout);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        struct worker_arg *arg;
        pthread_t tid;
        int client = accept(listen_sock, (struct sockaddr *)&client_addr, &addr_len);

        if (client < 0) {
            if (errno != EINTR)
                log_event("accept failed: %s", strerror(errno));
            continue;                      /* keep serving other peers */
        }

        arg = malloc(sizeof(*arg));
        if (!arg) {
            close(client);
            continue;
        }
        arg->sock = client;
        inet_ntop(AF_INET, &client_addr.sin_addr, arg->ip, sizeof(arg->ip));

        /* Hand the connection to a new worker thread straight away */
        if (pthread_create(&tid, &attr, worker, arg) != 0) {
            log_event("could not create worker thread for %s", arg->ip);
            close(client);
            free(arg);
        }
    }
}
