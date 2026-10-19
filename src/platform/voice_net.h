#ifndef ORBIS_VOICE_NET_H
#define ORBIS_VOICE_NET_H
#include <stddef.h>

int voice_udp_open(void);
int voice_udp_send(int fd, const char *ip, int port, const unsigned char *buf,
                   size_t len);
int voice_udp_recv(int fd, unsigned char *buf, size_t cap, char *ip,
                   size_t ip_cap, int *port);
void voice_udp_close(int fd);

#endif
