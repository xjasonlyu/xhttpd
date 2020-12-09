#include <arpa/inet.h>
#include <cassert>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "http.h"

void addsig(int sig, void(handler)(int), bool restart = true) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

int main(int argc, char *argv[]) {
    int ret = -1;

    if (argc < 3) {
        printf("usage: %s host port <dir>\n", *argv);
        return -ret;
    }

    argc--;
    argv++;

    char host[0x20];
    strcpy(host, *argv);

    argc--;
    argv++;

    int port;
    port = atoi(*argv);

    argc--;
    argv++;

    char doc_root[FILENAME_LEN];
    (argc > 0) ? strcpy(doc_root, *argv) : strcpy(doc_root, "./");

    addsig(SIGPIPE, SIG_IGN);

    HTTPServer server(host, port, doc_root);

    return server.serve_forever();
}
