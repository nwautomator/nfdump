/*
 *  Copyright (c) 2009-2025, Peter Haag
 *  Copyright (c) 2004-2008, SWITCH - Teleinformatikdienste fuer Lehre und Forschung
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *   * Neither the name of the author nor the names of its contributors may be
 *     used to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _UTIL_H
#define _UTIL_H 1

#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

#include "config.h"

#ifdef DEVEL
#include <assert.h>
#include <stdio.h>
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_assert(a) assert(a)
#define dbg(a) a
#else
#define dbg_printf(...) /* printf(__VA_ARGS__) */
#define dbg_assert(a)   /* assert(a) */
#define dbg(a)          /* a */
#endif

#define UNUSED(expr)  \
    do {              \
        (void)(expr); \
    } while (0)

#define EBUFF_SIZE 256

#ifndef HAVE_HTONLL
#ifdef WORDS_BIGENDIAN
#define ntohll(n) (n)
#define htonll(n) (n)
#else
#define ntohll(n) ((((uint64_t)ntohl(n)) << 32) + (ntohl((n) >> 32)))
#define htonll(n) ((((uint64_t)htonl(n)) << 32) + (htonl((n) >> 32)))
#endif
#endif

#if (SIZEOF_VOID_P == 8)
typedef uint64_t pointer_addr_t;
#else
typedef uint32_t pointer_addr_t;
#endif

#define _1KB (double)(1000.0)
#define _1MB (double)(1000.0 * 1000.0)
#define _1GB (double)(1000.0 * 1000.0 * 1000.0)
#define _1TB (double)(1000.0 * 1000.0 * 1000.0 * 1000.0)

#define SetFlag(var, flag) (var |= flag)
#define ClearFlag(var, flag) (var &= ~flag)
#define TestFlag(var, flag) (var & flag)

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

typedef struct stringlist_s {
    uint32_t block_size;
    uint32_t max_index;
    uint32_t num_strings;
    char **list;
} stringlist_t;

typedef struct timeWindow_s {
    uint64_t msecFirst;
    uint64_t msecLast;
} timeWindow_t;

double t(void);

void xsleep(suseconds_t usec);

void CheckArgLen(char *arg, size_t len);

int CheckPath(char *path, unsigned type);

#define PATH_ERROR -1
#define PATH_NOTEXISTS 0
#define PATH_WRONGTYPE 1
#define PATH_OK 2
int TestPath(char *path, unsigned type);

void EndLog(void);

int InitLog(int want_syslog, char *name, char *facility, int verbose_log);

void LogError(char *format, ...);

void LogInfo(char *format, ...);

void LogVerbose(char *format, ...);

void InitStringlist(stringlist_t *list, int block_size);

void InsertString(stringlist_t *list, char *string);

timeWindow_t *ScanTimeFrame(char *tstring);

char *TimeString(uint64_t msecStart, uint64_t msecEnd);

char *UNIX2ISO(time_t t);

time_t ISO2UNIX(char *timestring);

uint64_t ParseTime8601(const char *s);

long getTick(void);

char *DurationString(uint64_t duration);

#define DONT_SCALE_NUMBER 0
#define DO_SCALE_NUMBER 1
#define FIXED_WIDTH 1
#define VAR_LENGTH 0

#define NUMBER_STRING_SIZE 32
typedef char numStr[NUMBER_STRING_SIZE];
void format_number(uint64_t num, numStr s, int plain, int fixed_width);

void Setv6Mode(int mode);

void inet_ntop_mask(uint32_t ipv4, int mask, char *s, socklen_t sSize);

void inet6_ntop_mask(uint64_t ipv6[2], int mask, char *s, socklen_t sSize);

#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

uint32_t validate_utf8(uint32_t *state, char *str, size_t len);

char *HexString(uint8_t *hex, size_t len, char *hexString);

void DumpHex(FILE *stream, const void *data, size_t size);

#endif  //_UTIL_H
