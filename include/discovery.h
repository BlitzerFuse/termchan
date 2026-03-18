#ifndef DISCOVERY_H
#define DISCOVERY_H

#include "protocol.h"

#define DISCOVERY_PORT 5051
#define MAX_PEERS      16

typedef struct {
    char nickname[MAX_NAME];
    char ip[64];
} Peer;

void discovery_start(const char *my_nickname);

void discovery_stop(void);

int  discovery_peers(Peer *peers, int max);
void discovery_reset(void);

#endif
