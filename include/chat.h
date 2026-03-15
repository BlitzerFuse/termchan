#ifndef CHAT_H
#define CHAT_H

#include "protocol.h"

void start_chat(int socket_fd, char *nickname, void (*display_cb)(Packet *));

#endif
