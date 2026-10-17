#ifndef PV_ARPA_INET_H
#define PV_ARPA_INET_H
/* The console is big-endian: the byte order calls change nothing, as
 * PSL1GHT's. <sys/types.h> first: on macOS it defines byte-swapping ones,
 * and its guard keeps them from coming back after these. */
#include <sys/types.h>
#include "../netinet/in.h"
#undef htonl
#undef htons
#undef ntohl
#undef ntohs
#define htonl(x) (x)
#define htons(x) (x)
#define ntohl(x) (x)
#define ntohs(x) (x)
in_addr_t inet_addr(const char *cp);
#endif
