/*
 *  Copyright (c) 2024-2025, Peter Haag
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
 *   * Neither the name of SWITCH nor the names of its contributors may be
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

#ifndef _SSL_H
#define _SSL_H 1

#include <stdint.h>
#include <sys/types.h>

typedef struct uint16Array_s {
    uint32_t numElements;
    uint16_t *array;
} uint16Array_t;

#define arrayMask 0x1F

#define NewArray(a)          \
    {                        \
        (a).numElements = 0; \
        (a).array = NULL;    \
    }

#define AppendArray(a, v)                                                                                   \
    if (((a).numElements & arrayMask) == 0) {                                                               \
        (a).array = (uint16_t *)realloc((a).array, sizeof(uint16_t) * ((a).numElements + (arrayMask + 1))); \
        if (!(a).array) {                                                                                   \
            LogError("malloc() error in %s line %d: %s", __FILE__, __LINE__, strerror(errno));              \
            exit(255);                                                                                      \
        }                                                                                                   \
    }                                                                                                       \
    (a).array[a.numElements++] = (v);

#define FreeArray(a)                    \
    if ((a).numElements && (a).array) { \
        free((a).array);                \
        (a).numElements = 0;            \
        (a).array = NULL;               \
    }

#define LenArray(a) (a).numElements

#define ArrayElement(a, n) (a).array[n]

typedef struct ssl_s {
    uint16_t tlsVersion;
    char tlsCharVersion[2];
    uint16_t protocolVersion;
#define OFFsslVersion offsetof(ssl_t, protocolVersion)
#define SIZEsslVersion MemberSize(ssl_t, protocolVersion)
    /*
        protocol Version:
        0x0304 = TLS 1.3 = “13”
        0x0303 = TLS 1.2 = “12”
        0x0302 = TLS 1.1 = “11”
        0x0301 = TLS 1.0 = “10”
        0x0300 = SSL 3.0 = “s3”
        0x0002 = SSL 2.0 = “s2”
        Unknown = “00”
     */

    uint16_t type;
#define CLIENTssl 1
#define SERVERssl 2
    uint16Array_t cipherSuites;
    uint16Array_t extensions;
    uint16Array_t ellipticCurves;
    uint16Array_t ellipticCurvesPF;
    uint16Array_t signatures;
#define ALPNmaxLen 256
    // ALPN are currently defined up to 8 bytes
    char alpnName[ALPNmaxLen];
    char sniName[256];
#define OFFsslSNI offsetof(ssl_t, sniName)
#define SIZEsslSNI MemberSize(ssl_t, sniName)
} ssl_t;

void sslPrint(ssl_t *ssl);

void sslFree(ssl_t *ssl);

void sslPrint(ssl_t *ssl);

ssl_t *sslProcess(const uint8_t *data, size_t len);

void sslTest(void);

#endif
