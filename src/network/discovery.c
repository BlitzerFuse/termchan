#include "discovery.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define DISC_PING   "TERMCHAT_DISCOVER"
#define DISC_PONG   "TERMCHAT_HERE"
#define TIMEOUT_SEC 2

/* Walk interfaces with getifaddrs and return the broadcast address of
   the first non-loopback, broadcast-capable, up interface.
   Falls back to 255.255.255.255 if nothing found. */
static in_addr_t get_broadcast_addr(void) {
    struct ifaddrs *ifap, *ifa;
    in_addr_t result = INADDR_BROADCAST;

    if (getifaddrs(&ifap) < 0) return result;

    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK)   continue;
        if (!(ifa->ifa_flags & IFF_UP))       continue;
        if (!(ifa->ifa_flags & IFF_BROADCAST)) continue;
        if (!ifa->ifa_broadaddr)              continue;

        result = ((struct sockaddr_in *)ifa->ifa_broadaddr)->sin_addr.s_addr;
        break;
    }

    freeifaddrs(ifap);
    return result;
}

int discover_peers(Peer *peers, int max, const char *my_nickname) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return 0;

    int bcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast));

    /* bind ephemeral port — replies come back here */
    struct sockaddr_in local = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = 0
    };
    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
        close(sock); return 0;
    }

    in_addr_t baddr = get_broadcast_addr();
    char baddr_str[64];
    inet_ntop(AF_INET, &baddr, baddr_str, sizeof(baddr_str));

    char ping[128];
    snprintf(ping, sizeof(ping), "%s %s", DISC_PING, my_nickname);
    struct sockaddr_in dest = {
        .sin_family      = AF_INET,
        .sin_port        = htons(DISCOVERY_PORT),
        .sin_addr.s_addr = baddr
    };
    sendto(sock, ping, strlen(ping), 0, (struct sockaddr *)&dest, sizeof(dest));

    /* get our own IP so we can filter out our own broadcast loopback */
    struct sockaddr_in self = {0};
    socklen_t slen = sizeof(self);
    getsockname(sock, (struct sockaddr *)&self, &slen);

    int count = 0;
    /* deadline-based timeout: record when we started so each select()
       call gets the remaining time, not a fresh 2s after each discard */
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += TIMEOUT_SEC;

    fd_set fds;

    while (count < max) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long ms_left = (deadline.tv_sec  - now.tv_sec)  * 1000
                     + (deadline.tv_nsec - now.tv_nsec) / 1000000;
        if (ms_left <= 0) break;

        struct timeval tv = { ms_left / 1000, (ms_left % 1000) * 1000 };
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        if (select(sock + 1, &fds, NULL, NULL, &tv) <= 0) break;

        char buf[256] = {0};
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&from, &flen);
        if (n <= 0) continue;

        /* discard anything that isn't a pong */
        if (strncmp(buf, DISC_PONG, strlen(DISC_PONG)) != 0) continue;

        /* discard our own broadcast looping back (same IP as us) */
        if (from.sin_addr.s_addr == self.sin_addr.s_addr) continue;
        if ((ntohl(from.sin_addr.s_addr) >> 24) == 127) continue;

        const char *nick = buf + strlen(DISC_PONG);
        while (*nick == ' ') nick++;

        strncpy(peers[count].ip, inet_ntoa(from.sin_addr),
                sizeof(peers[count].ip) - 1);
        snprintf(peers[count].nickname, sizeof(peers[count].nickname), "%.*s",
                 (int)(sizeof(peers[count].nickname) - 1), nick);
        count++;
    }

    close(sock);
    return count;
}

void discovery_respond(const char *my_nickname) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return;

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(DISCOVERY_PORT)
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock); return;
    }

    while (1) {
        char buf[256] = {0};
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&from, &flen);
        if (n <= 0) break;
        if (strncmp(buf, DISC_PING, strlen(DISC_PING)) != 0) continue;

        const char *nick = buf + strlen(DISC_PING);
        while (*nick == ' ') nick++;

        char reply[128];
        snprintf(reply, sizeof(reply), "%s %s", DISC_PONG, my_nickname);
        sendto(sock, reply, strlen(reply), 0,
               (struct sockaddr *)&from, flen);
    }

    close(sock);
}
