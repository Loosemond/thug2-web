#pragma once
// thug2-web: Winsock on POSIX sockets. Online play is out of scope for the benchmark; this only has to compile and
// fail cleanly (Emscripten's sockets need a WebSocket proxy).
#include <windows.h>
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

typedef int SOCKET;
typedef unsigned long u_long;
typedef unsigned short u_short;
typedef struct sockaddr SOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct hostent HOSTENT;
typedef struct WSAData {
    WORD wVersion;
    WORD wHighVersion;
    char szDescription[257];
    char szSystemStatus[129];
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char* lpVendorInfo;
} WSADATA, *LPWSADATA;

#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define WSAEWOULDBLOCK EWOULDBLOCK
#define WSAEINPROGRESS EINPROGRESS
#define WSAEADDRINUSE EADDRINUSE
#define WSAECONNRESET ECONNRESET
#define WSAECONNREFUSED ECONNREFUSED
#define WSAEMSGSIZE EMSGSIZE
#define WSAENOTCONN ENOTCONN
#define WSAETIMEDOUT ETIMEDOUT
#define SD_RECEIVE SHUT_RD
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR
#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffff
#endif
#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))

inline int closesocket(SOCKET s) { return close(s); }
inline int ioctlsocket(SOCKET s, long command, u_long* argument)
{
    if (command == (long)FIONBIO) {
        int flags = fcntl(s, F_GETFL, 0);
        return fcntl(s, F_SETFL, *argument ? flags | O_NONBLOCK : flags & ~O_NONBLOCK) < 0 ? SOCKET_ERROR : 0;
    }
    return ioctl(s, command, argument);
}
inline int WSAGetLastError() { return errno; }
inline void WSASetLastError(int error) { errno = error; }
inline int WSAStartup(WORD, LPWSADATA data)
{
    if (data) *data = {};
    return 0;
}
inline int WSACleanup() { return 0; }

// Winsock's int* lengths (POSIX: socklen_t*)
inline int recvfrom(SOCKET s, char* buffer, int length, int flags, sockaddr* from, int* fromLength)
{
    socklen_t n = fromLength ? static_cast<socklen_t>(*fromLength) : 0;
    int got = static_cast<int>(::recvfrom(s, buffer, length, flags, from, fromLength ? &n : nullptr));
    if (fromLength)
        *fromLength = static_cast<int>(n);
    return got;
}
inline int getsockname(SOCKET s, sockaddr* name, int* nameLength)
{
    socklen_t n = static_cast<socklen_t>(*nameLength);
    int result = ::getsockname(s, name, &n);
    *nameLength = static_cast<int>(n);
    return result;
}
