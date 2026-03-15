#ifndef NETWORK_H
#define NETWORK_H

int init_listener(int port);
int accept_connection(int listener_fd, char *peer_ip);
int connect_to_peer(const char *ip, int port);

#endif
