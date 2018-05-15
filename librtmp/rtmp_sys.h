/*
 *      Copyright (C) 2010 Howard Chu
 *
 *  This file is part of librtmp.
 *
 *  librtmp is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1,
 *  or (at your option) any later version.
 *
 *  librtmp is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with librtmp see the file COPYING.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA  02110-1301, USA.
 *  http://www.gnu.org/copyleft/lgpl.html
 */

#ifndef __RTMP_SYS_H__
#define __RTMP_SYS_H__

#ifdef _WIN32

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#pragma warning(disable: 4018)

#include <winsock2.h>
#include <ws2tcpip.h>

#ifdef _MSC_VER    /* MSVC */
#if _MSC_VER < 1900
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#endif
#define strcasecmp stricmp
#define strncasecmp strnicmp
#endif

#define GetSockError()  WSAGetLastError()
#define SetSockError(e) WSASetLastError(e)
#define setsockopt(a, b, c, d, e) (setsockopt)(a, b, c, (const char *)d, (int)e)
#undef EWOULDBLOCK
#define EWOULDBLOCK WSAETIMEDOUT    /* we don't use nonblocking, but we do use timeouts */
#define sleep(n)    Sleep(n*1000)
#define msleep(n)   Sleep(n)
#define SET_RCVTIMEO(tv, s) int tv = s*1000
#else /* !_WIN32 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/times.h>
#include <netdb.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#define GetSockError()  errno
#define SetSockError(e) errno = e
#undef closesocket
#define closesocket(s)  close(s)
#define msleep(n)       usleep(n*1000)
#define SET_RCVTIMEO(tv, s) struct timeval tv = { s, 0 }
#endif

#include "rtmp.h"

#endif
