#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef _WIN32

#include <WinSock2.h>
#include <Windows.h>

#else

#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#define closesocket(S) close(S)

#endif

#endif // PLATFORM_H
