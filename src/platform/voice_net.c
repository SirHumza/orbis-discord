#include "voice_net.h"
#include "log.h"
#ifdef ORBISRPC_SDK_PAYLOAD
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#else
#include <orbis/Net.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#include <stdio.h>
#include <string.h>

#ifndef SOL_SOCKET
#define SOL_SOCKET 0xffff
#endif
#ifndef SO_NBIO
#define SO_NBIO 0x2000
#endif

int voice_udp_open(void){
#ifdef ORBISRPC_SDK_PAYLOAD
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd >= 0){
        int fl = fcntl(fd, F_GETFL, 0);
        if(fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }
    return fd;
#else
    int fd = sceNetSocket("orbisvoice", AF_INET, SOCK_DGRAM, 0);
    if(fd >= 0){
        int on = 1;
        sceNetSetsockopt(fd, SOL_SOCKET, SO_NBIO, &on, sizeof on);
    }
    return fd;
#endif
}

int voice_udp_send(int fd, const char *ip, int port, const unsigned char *buf,
                   size_t len){
    struct sockaddr_in a;
    int r;
    if(fd < 0 || !ip || !buf) return -1;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    if(inet_pton(AF_INET, ip, &a.sin_addr) != 1) return -1;
#ifdef ORBISRPC_SDK_PAYLOAD
    r = (int)sendto(fd, buf, len, 0, (struct sockaddr *)&a, sizeof a);
#else
    r = (int)sceNetSendto(fd, buf, len, 0, (OrbisNetSockaddr *)&a,
                          sizeof a);
#endif
    return r == (int)len ? 0 : -1;
}

int voice_udp_recv(int fd, unsigned char *buf, size_t cap, char *ip,
                   size_t ip_cap, int *port){
    struct sockaddr_in a;
    int n;
    if(fd < 0 || !buf || !ip || !ip_cap || !port) return -1;
    memset(&a, 0, sizeof a);
#ifdef ORBISRPC_SDK_PAYLOAD
    {
        socklen_t alen = sizeof a;
        n = (int)recvfrom(fd, buf, cap, 0, (struct sockaddr *)&a, &alen);
    }
#else
    {
        OrbisNetSocklen_t olen = sizeof a;
        n = (int)sceNetRecvfrom(fd, buf, cap, 0, (OrbisNetSockaddr *)&a,
                                &olen);
    }
#endif
    if(n <= 0) return n;
    snprintf(ip, ip_cap, "%u.%u.%u.%u",
             (unsigned)((unsigned char *)&a.sin_addr.s_addr)[0],
             (unsigned)((unsigned char *)&a.sin_addr.s_addr)[1],
             (unsigned)((unsigned char *)&a.sin_addr.s_addr)[2],
             (unsigned)((unsigned char *)&a.sin_addr.s_addr)[3]);
    *port = ntohs(a.sin_port);
    return n;
}

void voice_udp_close(int fd){
    if(fd < 0) return;
#ifdef ORBISRPC_SDK_PAYLOAD
    close(fd);
#else
    sceNetSocketClose(fd);
#endif
}
