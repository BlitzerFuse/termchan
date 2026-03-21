#ifndef FIREWALL_H
#define FIREWALL_H

void firewall_open(int tcp_port, int udp_port);

void firewall_close(int tcp_port, int udp_port);

#endif
