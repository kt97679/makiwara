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
"Connection: %s\n"
"Accept-Ranges: bytes\n"
"\n"
"%s";

char *response_close;
char *response_keep_alive;

int response_close_length = 0;
int response_keep_alive_length = 0;

char *keep_alive_str = "Connection: Keep-Alive";
int keep_alive_length = 0;

void accept_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);
void read_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);

int main(int argc, char *argv[]) {
    struct ev_loop *loop = ev_default_loop(0);
    int sd;
    struct sockaddr_in addr;
    int addr_len = sizeof(addr);
    struct ev_io w_accept;
    int port = 0;
    char *body = "";
    int body_length = 0;

    if (argc != 2 && argc != 3) {
        printf("Usage:   %s port [response_body]\n", argv[0]);
        printf("Example: %s 8080 '<html></html>'\n", argv[0]);
        printf("response_body can be empty\n");
        return(-1);
    }

    port = atoi(argv[1]);

    if (port < 1 || port > 65535) {
        printf("Port should be in 1..65535 range\n");
        return(-2);
    }

    if (argc == 3) {
        body = argv[2];
        body_length = strlen(body);
    }

    response_close = (char *)malloc(strlen(response_fmt) + body_length + 16); // plus overhead for content-length, connection and terminating null
    response_keep_alive = (char *)malloc(strlen(response_fmt) + body_length + 16);
    if (response_close == NULL || response_keep_alive == NULL) {
        perror("Failed to allocate memory");
        return(-3);
    }

    sprintf(response_close, response_fmt, body_length, "close", body);
    sprintf(response_keep_alive, response_fmt, body_length, "keep-alive", body);

    response_close_length = strlen(response_close);
    response_keep_alive_length = strlen(response_keep_alive);

    keep_alive_length = strlen(keep_alive_str);

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
        perror("got invalid event");
        return;
    }

    client_sd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_len);

    if (client_sd < 0) {
        perror("accept error");
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
        perror("got invalid event");
        return;
    }

    read = recv(watcher->fd, buffer, BUFFER_SIZE, 0);

    if (read < 0) {
//        perror("read error");
        return;
    }

    if (read > 0) {
//        printf("*** request start\n%s\n*** request end\n", buffer);
        for (i = 0; i < read; i++) {
            if (buffer[i] == '\n' && strncasecmp((buffer + i + 1), keep_alive_str, keep_alive_length) == 0) {
                send(watcher->fd, response_keep_alive, response_keep_alive_length, 0);
                return; // NB! keep-alive check can be more strict
            }
        }
        send(watcher->fd, response_close, response_close_length, 0);
        shutdown(watcher->fd, SHUT_RDWR);
    }
    // we are here because we need to close connection
    // either read == 0 or no keep-alive in request
    close(watcher->fd);
    ev_io_stop(loop, watcher);
    free(watcher);
}
