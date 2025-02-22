/*
 *  Copyright (c) 2025, Peter Haag
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

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>

#include "nfxV3.h"
#include "output_csv.h"
#include "output_util.h"
#include "util.h"

#define IP_STRING_LEN (INET6_ADDRSTRLEN)

// record counter
static uint32_t recordCount;

#include "itoa.c"

#define AddString(s)               \
    do {                           \
        size_t len = strlen(s);    \
        memcpy(streamPtr, s, len); \
        streamPtr += len;          \
        *streamPtr++ = ',';        \
    } while (0)

#define AddU64(u64)                                       \
    do {                                                  \
        streamPtr = itoa_u64((uint64_t)(u64), streamPtr); \
        *streamPtr++ = ',';                               \
    } while (0)

#define AddU32(u32)                                       \
    do {                                                  \
        streamPtr = itoa_u32((uint32_t)(u32), streamPtr); \
        *streamPtr++ = ',';                               \
    } while (0)

#define STREAMBUFFSIZE 1014
static char *streamBuff = NULL;

void csv_prolog_fast(outputParams_t *outputParam) {
    // empty prolog
    recordCount = 0;
    streamBuff = malloc(STREAMBUFFSIZE);
    if (!streamBuff) {
        LogError("malloc() error in %s line %d: %s", __FILE__, __LINE__, strerror(errno));
        exit(EXIT_FAILURE);
    }
    streamBuff[0] = '\0';
    printf("cnt,af,firstSeen,lastSeen,proto,srcAddr,srcPort,dstAddr,dstPort,srcAS,dstAS,input,output,flags,srcTos,packets,bytes\n");
}  // End of csv_prolog_fast

void csv_epilog_fast(outputParams_t *outputParam) {
    // empty epilog
    free(streamBuff);
    streamBuff = NULL;
}  // End of csv_epilog_fast

void csv_record_fast(FILE *stream, recordHandle_t *recordHandle, outputParams_t *outputParam) {
    EXgenericFlow_t *genericFlow = (EXgenericFlow_t *)recordHandle->extensionList[EXgenericFlowID];
    EXipv4Flow_t *ipv4Flow = (EXipv4Flow_t *)recordHandle->extensionList[EXipv4FlowID];
    EXipv6Flow_t *ipv6Flow = (EXipv6Flow_t *)recordHandle->extensionList[EXipv6FlowID];
    EXflowMisc_t *flowMisc = (EXflowMisc_t *)recordHandle->extensionList[EXflowMiscID];
    EXasRouting_t *asRouting = (EXasRouting_t *)recordHandle->extensionList[EXasRoutingID];

    EXgenericFlow_t genericNull = {0};
    if (!genericFlow) genericFlow = &genericNull;

    EXflowMisc_t miscNull = {0};
    if (!flowMisc) flowMisc = &miscNull;

    EXasRouting_t asNULL = {0};
    if (!asRouting) asRouting = &asNULL;

    streamBuff[0] = '\0';
    char *streamPtr = streamBuff;

    int af = 0;
    char sa[IP_STRING_LEN], da[IP_STRING_LEN];
    if (ipv4Flow) {
        af = PF_INET;
        uint32_t src = htonl(ipv4Flow->srcAddr);
        uint32_t dst = htonl(ipv4Flow->dstAddr);

        inet_ntop(AF_INET, &src, sa, sizeof(sa));
        inet_ntop(AF_INET, &dst, da, sizeof(da));
    }

    if (ipv6Flow) {
        af = PF_INET6;
        uint64_t src[2], dst[2];
        src[0] = htonll(ipv6Flow->srcAddr[0]);
        src[1] = htonll(ipv6Flow->srcAddr[1]);
        dst[0] = htonll(ipv6Flow->dstAddr[0]);
        dst[1] = htonll(ipv6Flow->dstAddr[1]);

        inet_ntop(AF_INET6, &src, sa, sizeof(sa));
        inet_ntop(AF_INET6, &dst, da, sizeof(da));
    }

    AddU32(++recordCount);
    AddU32(af);
    AddU64(genericFlow->msecFirst);
    AddU64(genericFlow->msecLast);
    AddU32(genericFlow->proto);
    AddString(sa);
    AddU32(genericFlow->srcPort);
    AddString(da);
    AddU32(genericFlow->dstPort);
    AddU32(asRouting->srcAS);
    AddU32(asRouting->dstAS);
    AddU32(flowMisc->input);
    AddU32(flowMisc->output);
    AddString(FlagsString(genericFlow->proto == IPPROTO_TCP ? genericFlow->tcpFlags : 0));
    AddU32(genericFlow->srcTos);
    AddU64(genericFlow->inPackets);
    AddU64(genericFlow->inBytes);

    *--streamPtr = '\n';
    *++streamPtr = '\0';

    if (unlikely((streamBuff + STREAMBUFFSIZE - streamPtr) < 512)) {
        LogError("csv_record_fast() error in %s line %d: %s", __FILE__, __LINE__, "buffer error");
        exit(EXIT_FAILURE);
    }
    fputs(streamBuff, stream);

}  // End of csv_record_fast
