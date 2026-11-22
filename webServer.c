
/*
 * Minimal embedded HTTP server
 * ARM Linux / POSIX
 *
 * Build:
 * arm-linux-gnueabihf-gcc -O2 -Wall -Wextra \
 *     -static -o http_server http_server.c
 *
 * Run:
 * mkdir -p www
 * ./http_server [port]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_PORT 8021
#define ROOT       "./www"
#define MAX_REQ    8192
#define MAX_PATH   1024

static const char *mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');

    if (!ext) return "application/octet-stream";

    if (!strcmp(ext, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(ext, ".js"))   return "application/javascript";
    if (!strcmp(ext, ".css"))  return "text/css";
    if (!strcmp(ext, ".json")) return "application/json";
    if (!strcmp(ext, ".svg"))  return "image/svg+xml";
    if (!strcmp(ext, ".png"))  return "image/png";
    if (!strcmp(ext, ".jpg"))  return "image/jpeg";
    if (!strcmp(ext, ".ico"))  return "image/x-icon";

    return "application/octet-stream";
}

static int send_all(int fd, const void *data, size_t len)
{
    const char *p = data;

    while (len) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);

        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        if (n == 0) return -1;

        p += n;
        len -= (size_t)n;
    }

    return 0;
}

static void response(int fd, int status,
                     const char *reason,
                     const char *type,
                     const char *body)
{
    char header[512];

    size_t len = strlen(body);

    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n",
        status, reason, type, (unsigned long)len);

    if (n > 0 && (size_t)n < sizeof(header)) {
        send_all(fd, header, (size_t)n);
        send_all(fd, body, len);
    }
}

static void handle_client(int fd)
{
    char req[MAX_REQ + 1];
    size_t used = 0;

    /* Read request headers. */
    while (used < MAX_REQ) {
        ssize_t n = recv(fd, req + used, MAX_REQ - used, 0);

        if (n < 0) {
            if (errno == EINTR) continue;
            return;
        }

        if (n == 0) return;

        used += (size_t)n;
        req[used] = '\0';

        if (strstr(req, "\r\n\r\n"))
            break;
    }

    if (used == MAX_REQ) {
        response(fd, 431, "Request Header Fields Too Large",
                 "text/plain", "Headers too large\n");
        return;
    }

    char method[16];
    char path[MAX_PATH];

    if (sscanf(req, "%15s %1023s", method, path) != 2) {
        response(fd, 400, "Bad Request",
                 "text/plain", "Bad request\n");
        return;
    }

    if (strcmp(method, "GET") != 0) {
        response(fd, 405, "Method Not Allowed",
                 "text/plain", "GET only\n");
        return;
    }

    /* Remove query string. */
    char *query = strchr(path, '?');
    if (query) *query = '\0';

    /* Only allow absolute paths without traversal. */
    if (path[0] != '/' || strstr(path, "..") ||
        strchr(path, '\\')) {
        response(fd, 400, "Bad Request",
                 "text/plain", "Invalid path\n");
        return;
    }

    if (strcmp(path, "/") == 0)
        strcpy(path, "/index.html");

    char filename[MAX_PATH + 16];

    int n = snprintf(filename, sizeof(filename),
                     "%s%s", ROOT, path);

    if (n < 0 || (size_t)n >= sizeof(filename)) {
        response(fd, 400, "Bad Request",
                 "text/plain", "Invalid path\n");
        return;
    }

    int file = open(filename, O_RDONLY | O_NOFOLLOW);

    if (file < 0) {
        response(fd, 404, "Not Found",
                 "text/plain", "File not found\n");
        return;
    }

    struct stat st;

    if (fstat(file, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(file);
        response(fd, 404, "Not Found",
                 "text/plain", "File not found\n");
        return;
    }

    char header[512];

    n = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n",
        mime_type(filename), (unsigned long)st.st_size);

    if (n > 0 && (size_t)n < sizeof(header)) {
        if (send_all(fd, header, (size_t)n) == 0) {
            char buffer[4096];

            for (;;) {
                ssize_t r = read(file, buffer, sizeof(buffer));

                if (r < 0) {
                    if (errno == EINTR) continue;
                    break;
                }

                if (r == 0) break;

                if (send_all(fd, buffer, (size_t)r) < 0)
                    break;
            }
        }
    }

    close(file);
}

int main(int argc, char *argv[])
{
    int port = DEFAULT_PORT;

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [port]\n", argv[0]);
        return 1;
    }

    if (argc == 2) {
        char *end;
        long value;

        errno = 0;
        value = strtol(argv[1], &end, 10);

        if (errno != 0 || *argv[1] == '\0' || *end != '\0' ||
            value < 1 || value > 65535) {
            fprintf(stderr, "Invalid port: %s (expected 1-65535)\n",
                    argv[1]);
            return 1;
        }

        port = (int)value;
    }

    signal(SIGPIPE, SIG_IGN);

    int server = socket(AF_INET, SOCK_STREAM, 0);

    if (server < 0) {
        perror("socket");
        return 1;
    }

    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               &yes, sizeof(yes));

    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server, (struct sockaddr *)&addr,
             sizeof(addr)) < 0) {
        perror("bind");
        close(server);
        return 1;
    }

    if (listen(server, 8) < 0) {
        perror("listen");
        close(server);
        return 1;
    }

    printf("HTTP server listening on port %d\n", port);

    for (;;) {
        int client = accept(server, NULL, NULL);

        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        handle_client(client);
        close(client);
    }

    close(server);
    return 0;
}
