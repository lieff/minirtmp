/*
 *  Copyright (C) 2008-2009 Andrej Stepanchuk
 *  Copyright (C) 2009-2010 Howard Chu
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

#ifndef __RTMP_LOG_H__
#define __RTMP_LOG_H__

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    RTMP_LOGCRIT = 0, RTMP_LOGERROR, RTMP_LOGWARNING, RTMP_LOGINFO, RTMP_LOGDEBUG, RTMP_LOGDEBUG2, RTMP_LOGALL
} RTMP_LogLevel;

#ifdef _DEBUG

#ifdef __GNUC__
void RTMP_Log(int level, const char *format, ...) __attribute__ ((__format__ (__printf__, 2, 3)));
#else
void RTMP_Log(int level, const char *format, ...);
#endif
void RTMP_LogHex(int level, const uint8_t *data, unsigned long len);
void RTMP_LogHexString(int level, const uint8_t *data, unsigned long len);

#else

#define RTMP_Log(level, format, ...)
#define RTMP_LogHex(level, data, len)
#define RTMP_LogHexString(level, data, len)

#endif

#ifdef __cplusplus
}
#endif

#endif
