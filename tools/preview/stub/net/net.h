#ifndef PV_NET_H
#define PV_NET_H
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <poll.h>
#include "../ppu-types.h"
#include "socket.h"
#define NET_EAGAIN 35
#define NET_EWOULDBLOCK NET_EAGAIN
#define NET_EINPROGRESS 36
#define NET_EALREADY 37
#define NET_ENETDOWN 50
#define NET_ECONNABORTED 53
#define NET_ECONNRESET 54
#define NET_EISCONN 56
#define NET_ENOTCONN 57
#define NET_ETIMEDOUT 60
#define NET_ECONNREFUSED 61
#define NET_EHOSTUNREACH 65
#define net_errno (*netErrnoLoc())
struct net_hostent { u32 h_name, h_aliases; s32 h_addrtype, h_length; u32 h_addr_list; };
s32 netInitialize(void); s32 netDeinitialize(void);
s32 netSocket(s32 d, s32 t, s32 p); s32 netConnect(s32 s, const struct sockaddr *a, socklen_t l); s32 netClose(s32 s);
s32 netBind(s32 s, const struct sockaddr *a, socklen_t l); s32 netListen(s32 s, s32 b); s32 netAccept(s32 s, const struct sockaddr *a, socklen_t *l);
s32 netSetSockOpt(s32 s, s32 level, s32 opt, const void *v, socklen_t l);
ssize_t netSend(s32 s, const void *b, size_t l, s32 f); ssize_t netRecv(s32 s, void *b, size_t l, s32 f);
struct net_hostent *netGetHostByName(const char *name);
s32 *netErrnoLoc(void); s32 netShutdown(s32 s, s32 how);
ssize_t netRecvFrom(s32 s, void *b, size_t l, s32 f, struct sockaddr *from, socklen_t *fl);
ssize_t netSendTo(s32 s, const void *b, size_t l, s32 f, const struct sockaddr *to, socklen_t tl);
s32 netPoll(struct pollfd *fds, nfds_t nfds, s32 timeout);
s32 netSelect(s32 nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *t);
s32 netGetSockOpt(s32 s, s32 level, s32 opt, void *v, socklen_t *l);
#endif
