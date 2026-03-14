#include "chat.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/select.h>
#include <string.h>
#include <sys/socket.h>

#define BUFFER_SIZE 1024

void start_chat(int socket_fd) {
    char buffer[BUFFER_SIZE];
    fd_set fds;

    while (1) {
        FD_ZERO(&fds);
        FD_SET(0, &fds);
        FD_SET(socket_fd, &fds);

        int max_fd = socket_fd;
        if (select(max_fd + 1, &fds, NULL, NULL, NULL) < 0) { perror("select"); break; }

        if (FD_ISSET(0, &fds)) {
            memset(buffer, 0, BUFFER_SIZE);
            if (!fgets(buffer, BUFFER_SIZE, stdin)) continue;
            send(socket_fd, buffer, strlen(buffer), 0);
        }

        if (FD_ISSET(socket_fd, &fds)) {
            memset(buffer, 0, BUFFER_SIZE);
            int bytes = recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);
            if (bytes <= 0) {
                printf("Peer disconnected\n");
                break;
            }
            printf("Peer: %s", buffer);
        }
    }

    close(socket_fd);
}
