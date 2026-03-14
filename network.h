#ifndef NETWORK_H
#define NETWORK_H

int start_listener(int port);
int connect_to_peer(const char *ip, int port);

#endif
