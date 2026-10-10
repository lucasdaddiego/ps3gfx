#ifndef PV_NET_SOCKET_H
#define PV_NET_SOCKET_H
#include "../ppu-types.h"
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOL_SOCKET 0xFFFF
#define SO_REUSEADDR 0x0004
#define SO_KEEPALIVE 0x0008
#define SO_SNDBUF 0x1001
#define SO_RCVBUF 0x1002
#define SO_SNDTIMEO 0x1005
#define SO_RCVTIMEO 0x1006
#define SO_ERROR 0x1007
#define SO_NBIO 0x1100
#define MSG_PEEK 0x0002
#define MSG_WAITALL 0x0040
#define MSG_DONTWAIT 0x0080
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2
typedef u32 socklen_t;
typedef u8 sa_family_t;
struct sockaddr { u8 sa_len; sa_family_t sa_family; char sa_data[14]; };
#endif
