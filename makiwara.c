/*
 * Makiwara 
 *
 * Single threaded http server, that returns empty response.
 * Supports keep-alive connections.
 * Created for stress testing of http frontends.
 * Depends on libev.
 * Compilation: gcc -o makiwara makiwara.c -lev
 *
 * Based on code from http://codefundas.blogspot.com/2010/09/create-tcp-echo-server-using-libev.html
 *
 * The makiwara is a padded striking post used as a training tool in various styles of traditional karate.
 *
 * Author: Kirill Timofeev <kt97679@gmail.com>
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <netinet/in.h>
#include <ev.h>

#define BUFFER_SIZE 1024

char *response_fmt = "HTTP/1.1 200 OK\n"
"Server: makiwara/0.0.1\n"
"Content-Type: text/html\n"
"Content-Length: %d\n"
"Connection: keep-alive\n"
"Accept-Ranges: bytes\n"
"\n"
"%s";

struct global {
    char *response;
    int response_length;
    char *keep_alive_str;
    int keep_alive_length;
    char *program;
    int verbose:1;
};

struct global global;

void accept_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);
void read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);

void usage() {
    printf("Usage:   %s [-p port] [-b response_body] [-v]\n", global.program);
    printf("            -p PORT (by default 8888) \n");
    printf("            -b BODY (by default empty) \n");
    printf("            -v use verbose output (by default off)\n");
    printf("Example: %s -p 8080 -b '<html></html>'\n", global.program);
    exit(-1);
}

void check_port(int port) {
    if (port < 1 || port > 65535) {
        printf("Port should be in 1..65535 range\n");
        usage();
    }
}

void check_argument(int argc, char **argv) {
    if (argc < 2) {
        printf("No argument for \"%s\" option\n", *argv);
        usage();
    }
}

int main(int argc, char *argv[]) {
    struct ev_loop *loop = ev_default_loop(0);
    int sd;
    struct sockaddr_in addr;
    int addr_len = sizeof(addr);
    struct ev_io w_accept;
    int port = 8888;
    char *body = "";
    int body_length = 0;

    global.verbose = 0;
    global.program = *argv;
    argc--;

    while(argc > 0) {
        argv++;
        if (**argv == '-') {
            switch (*(*argv + 1)) {
                case 'v':
                    global.verbose = 1;
                    argc--;
                    continue;
                case 'b':
                    check_argument(argc, argv);
                    body = *++argv;
                    body_length = strlen(body);
                    argc -= 2;
                    continue;
                case 'p':
                    check_argument(argc, argv);
                    port = atoi(*++argv);
                    check_port(port);
                    argc -= 2;
                    continue;
                case 'h':
                    usage();
            }
        }
        printf("Unknown option: \"%s\"\n", *argv);
        usage();
    }

    global.response = (char *)malloc(strlen(response_fmt) + body_length + 16); // plus overhead for content-length and terminating null
    if (global.response == NULL) {
        perror("Failed to allocate memory");
        return(-3);
    }

    sprintf(global.response, response_fmt, body_length, body);
    global.response_length = strlen(global.response);

    global.keep_alive_str = "Connection: Keep-Alive";
    global.keep_alive_length = strlen(global.keep_alive_str);

    if ((sd = socket(PF_INET, SOCK_STREAM, 0)) < 0 ) {
        perror("socket error");
        return(-4);
    }
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sd, (struct sockaddr*) &addr, sizeof(addr)) != 0) {
        perror("bind error");
        return(-5);
    }

    if (listen(sd, 2) < 0) {
        perror("listen error");
        return(-6);
    }

    ev_io_init(&w_accept, accept_cb, sd, EV_READ);
    ev_io_start(loop, &w_accept);

    while (1) {
        ev_loop(loop, 0);
    }
    return(0);
}

void accept_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_sd;
    struct ev_io *w_client = (struct ev_io*) malloc (sizeof(struct ev_io));

    if (EV_ERROR & revents) {
        if (global.verbose) {
            perror("got invalid event");
        }
        return;
    }

    client_sd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_len);

    if (client_sd < 0) {
        if (global.verbose) {
            perror("accept error");
        }
        return;
    }

    ev_io_init(w_client, read_cb, client_sd, EV_READ);
    ev_io_start(loop, w_client);
}

void read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents) {
    char buffer[BUFFER_SIZE];
    ssize_t read;
    int i;

    if (EV_ERROR & revents) {
        if (global.verbose) {
            perror("got invalid event");
        }
        return;
    }

    read = recv(watcher->fd, buffer, BUFFER_SIZE, 0);

    if (read < 0) {
        if (global.verbose) {
            perror("read error");
        }
        return;
    }

    if (read > 0) {
        if (global.verbose) {
            buffer[read] = 0;
            printf("<<< request start <<<\n%s>>> request end >>>\n", buffer);
        }
        send(watcher->fd, global.response, global.response_length, 0);
        for (i = 0; i < read; i++) {
            if (buffer[i] == '\n' && strncasecmp((buffer + i + 1), global.keep_alive_str, global.keep_alive_length) == 0) {
                return; // NB! keep-alive check can be more strict
            }
        }
        shutdown(watcher->fd, SHUT_RDWR);
    }
    // we are here because we need to close connection
    // either read == 0 or no keep-alive in request
    close(watcher->fd);
    ev_io_stop(loop, watcher);
    free(watcher);
}
