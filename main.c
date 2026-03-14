#include <stdio.h>
#include <string.h>
#include "network.h"
#include "chat.h"

#define PORT 5000

int main(int argc, char *argv[]) {
    int sockfd = -1;

    if (argc == 2 && strcmp(argv[1], "listen") == 0) {
        sockfd = start_listener(PORT);
    } else if (argc == 3 && strcmp(argv[1], "connect") == 0) {
        sockfd = connect_to_peer(argv[2], PORT);
    } else {
        printf("Usage:\n  %s listen\n  %s connect <ip>\n", argv[0], argv[0]);
        return 1;
    }

    start_chat(sockfd);
    return 0;
}
