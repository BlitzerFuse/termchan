#include "network.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int init_listener(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return -1; }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(port)
    };

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sock); return -1;
    }
    if (listen(sock, 5) < 0) {
        perror("listen"); close(sock); return -1;
    }
    return sock;
}

int accept_connection(int listener_fd, char *peer_ip) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int conn = accept(listener_fd, (struct sockaddr *)&addr, &len);
    if (conn < 0) { perror("accept"); return -1; }
    if (peer_ip) strncpy(peer_ip, inet_ntoa(addr.sin_addr), 63);
    return conn;
}

int connect_to_peer(const char *ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return -1; }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(port)
    };
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); close(sock); return -1;
    }
    return sock;
}
