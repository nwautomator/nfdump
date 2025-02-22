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

#include "netflow_v1.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "collector.h"
#include "metric.h"
#include "nfdump.h"
#include "nffile.h"
#include "nfnet.h"
#include "nfxV3.h"
#include "output_short.h"
#include "util.h"

#define NETFLOW_V1_HEADER_LENGTH 16
#define NETFLOW_V1_RECORD_LENGTH 48
#define NETFLOW_V1_MAX_RECORDS 24

/* v1 structures */
typedef struct netflow_v1_header {
    uint16_t version;
    uint16_t count;
    uint32_t SysUptime;
    uint32_t unix_secs;
    uint32_t unix_nsecs;
} netflow_v1_header_t;

typedef struct netflow_v1_record {
    uint32_t srcaddr;
    uint32_t dstaddr;
    uint32_t nexthop;
    uint16_t input;
    uint16_t output;
    uint32_t dPkts;
    uint32_t dOctets;
    uint32_t First;
    uint32_t Last;
    uint16_t srcPort;
    uint16_t dstPort;
    uint16_t pad1;
    uint8_t prot;
    uint8_t tos;
    uint8_t tcp_flags;
    uint8_t pad2[7];
} netflow_v1_record_t;

typedef struct exporter_v1_s {
    // struct exporter_s
    struct exporter_v1_s *next;

    // exporter information
    exporter_info_record_t info;  // exporter record nffile

    uint64_t packets;           // number of packets sent by this exporter
    uint64_t flows;             // number of flow records sent by this exporter
    uint32_t sequence_failure;  // number of sequence failures
    uint32_t padding_errors;    // number of sequence failures

    sampler_t *sampler;  // list of samplers associated with this exporter
    // end of struct exporter_s

    // pre-calculated v1 output record header values
    uint16_t outRecordSize;

} exporter_v1_t;

/* module limited globals */
static int printRecord;
static uint32_t baseRecordSize;

static inline exporter_v1_t *getExporter(FlowSource_t *fs, netflow_v1_header_t *header);

/* functions */

int Init_v1(int verbose) {
    printRecord = verbose > 2;
    baseRecordSize = sizeof(recordHeaderV3_t) + EXgenericFlowSize + EXipv4FlowSize + EXflowMiscSize + EXipNextHopV4Size;

    LogVerbose("Init v1");
    return 1;
}  // End of Init_v1

static inline exporter_v1_t *getExporter(FlowSource_t *fs, netflow_v1_header_t *header) {
    exporter_v1_t **e = (exporter_v1_t **)&(fs->exporter_data);
    uint16_t version = ntohs(header->version);

    // search the matching v1 exporter
    while (*e) {
        if ((*e)->info.version == version && (*e)->info.ip.V6[0] == fs->ip.V6[0] && (*e)->info.ip.V6[1] == fs->ip.V6[1]) return *e;
        e = &((*e)->next);
    }

    // nothing found
    *e = (exporter_v1_t *)malloc(sizeof(exporter_v1_t));
    if (!(*e)) {
        LogError("Process_v1: malloc() error in %s line %d: %s", __FILE__, __LINE__, strerror(errno));
        return NULL;
    }
    (**e) = (exporter_v1_t){.info = {.header = {.type = ExporterInfoRecordType, .size = sizeof(exporter_info_record_t)},
                                     .version = version,
                                     .id = 0,
                                     .ip = fs->ip,
                                     .sa_family = fs->sa_family,
                                     .sysid = 0}};
    char *ipstr = GetExporterIP(fs);
    if (fs->sa_family == PF_INET6) {
        (*e)->outRecordSize = baseRecordSize + EXipReceivedV6Size;
        dbg_printf("Process_v1: New IPv6 exporter %s - add EXipReceivedV6\n", ipstr);
    } else {
        (*e)->outRecordSize = baseRecordSize + EXipReceivedV4Size;
        dbg_printf("Process_v1: New IPv4 exporter %s - add EXipReceivedV4\n", ipstr);
    }

    FlushInfoExporter(fs, &((*e)->info));

    LogInfo("Process_v1: SysID: %u, New exporter: IP: %s\n", (*e)->info.sysid, ipstr);

    return (*e);

}  // End of getExporter

void Process_v1(void *in_buff, ssize_t in_buff_cnt, FlowSource_t *fs) {
    // map v1 data structure to input buffer
    netflow_v1_header_t *v1_header = (netflow_v1_header_t *)in_buff;

    exporter_v1_t *exporter = getExporter(fs, v1_header);
    if (!exporter) {
        LogError("Process_v1: NULL Exporter: Abort v1 record processing");
        return;
    }
    exporter->packets++;

    // this many data to process
    ssize_t size_left = in_buff_cnt;

    // time received for this packet
    uint64_t msecReceived = ((uint64_t)fs->received.tv_sec * 1000LL) + (uint64_t)((uint64_t)fs->received.tv_usec / 1000LL);

    int done = 0;
    while (!done) {
        // count check
        uint16_t count = ntohs(v1_header->count);
        if (count > NETFLOW_V1_MAX_RECORDS) {
            LogError("Process_v1: Unexpected record count in header: %i. Abort v1 record processing", count);
            return;
        }

        // input buffer size check for all expected records
        if (size_left < (NETFLOW_V1_HEADER_LENGTH + count * NETFLOW_V1_RECORD_LENGTH)) {
            LogError("Process_v1: Not enough data to process v1 record. Abort v1 record processing");
            return;
        }

        // output buffer size check for all expected records
        void *outBuff = GetCurrentCursor(fs->dataBlock);
        if (!IsAvailable(fs->dataBlock, count * exporter->outRecordSize)) {
            // flush block - get an empty one
            fs->dataBlock = WriteBlock(fs->nffile, fs->dataBlock);
            // map output memory buffer
            outBuff = GetCursor(fs->dataBlock);
        }

        v1_header->SysUptime = ntohl(v1_header->SysUptime);
        v1_header->unix_secs = ntohl(v1_header->unix_secs);
        v1_header->unix_nsecs = ntohl(v1_header->unix_nsecs);

        /* calculate boot time in msec */
        uint64_t msecBoot =
            ((uint64_t)(v1_header->unix_secs) * 1000 + ((uint64_t)(v1_header->unix_nsecs) / 1000000)) - (uint64_t)(v1_header->SysUptime);

        // process all records
        netflow_v1_record_t *v1_record = (netflow_v1_record_t *)((void *)v1_header + NETFLOW_V1_HEADER_LENGTH);

        /* loop over each records associated with this header */
        uint32_t outSize = 0;
        for (int i = 0; i < count; i++) {
            // header data gets initialized by macro
            AddV3Header(outBuff, recordHeader);
            recordHeader->exporterID = exporter->info.sysid;
            recordHeader->nfversion = 1;

            PushExtension(recordHeader, EXgenericFlow, genericFlow);
            genericFlow->msecReceived = msecReceived;
            genericFlow->inPackets = ntohl(v1_record->dPkts);
            genericFlow->inBytes = ntohl(v1_record->dOctets);
            genericFlow->srcPort = ntohs(v1_record->srcPort);
            genericFlow->dstPort = ntohs(v1_record->dstPort);
            genericFlow->proto = v1_record->prot;
            genericFlow->srcTos = v1_record->tos;
            genericFlow->tcpFlags = v1_record->tcp_flags;

            PushExtension(recordHeader, EXipv4Flow, ipv4Flow);
            ipv4Flow->srcAddr = ntohl(v1_record->srcaddr);
            ipv4Flow->dstAddr = ntohl(v1_record->dstaddr);

            if (v1_record->input || v1_record->output) {
                PushExtension(recordHeader, EXflowMisc, flowMisc);
                flowMisc->input = ntohs(v1_record->input);
                flowMisc->output = ntohs(v1_record->output);
            }

            if (v1_record->nexthop) {
                PushExtension(recordHeader, EXipNextHopV4, ipNextHopV4);
                ipNextHopV4->ip = ntohl(v1_record->nexthop);
            }

            // post process required data

            // calculate msec values first/last
            uint64_t First = ntohl(v1_record->First);
            uint64_t Last = ntohl(v1_record->Last);
            uint64_t msecStart, msecEnd;

            if (First > Last) {
                // First in msec, in case of msec overflow, between start and end
                msecStart = msecBoot - 0x100000000LL + First;
            } else {
                msecStart = msecBoot + First;
            }

            // end time in msecs
            msecEnd = Last + msecBoot;

            // if overflow happened after flow ended but before got exported
            if (Last > v1_header->SysUptime) {
                msecStart -= 0x100000000LL;
                msecEnd -= 0x100000000LL;
            }
            genericFlow->msecFirst = msecStart;
            genericFlow->msecLast = msecEnd;

            UpdateFirstLast(fs, msecStart, msecEnd);

            // router IP
            if (fs->sa_family == PF_INET6) {
                PushExtension(recordHeader, EXipReceivedV6, ipReceivedV6);
                ipReceivedV6->ip[0] = fs->ip.V6[0];
                ipReceivedV6->ip[1] = fs->ip.V6[1];
            } else {
                PushExtension(recordHeader, EXipReceivedV4, ipReceivedV4);
                ipReceivedV4->ip = fs->ip.V4;
            }

            // Update stats
            switch (genericFlow->proto) {
                case IPPROTO_ICMP:
                    fs->nffile->stat_record->numflows_icmp++;
                    fs->nffile->stat_record->numpackets_icmp += genericFlow->inPackets;
                    fs->nffile->stat_record->numbytes_icmp += genericFlow->inBytes;
                    // fix odd CISCO behaviour for ICMP port/type in src port
                    if (genericFlow->srcPort != 0) {
                        uint8_t *s1 = (uint8_t *)&(genericFlow->srcPort);
                        uint8_t *s2 = (uint8_t *)&(genericFlow->dstPort);
                        s2[0] = s1[1];
                        s2[1] = s1[0];
                        genericFlow->srcPort = 0;
                    }
                    break;
                case IPPROTO_TCP:
                    fs->nffile->stat_record->numflows_tcp++;
                    fs->nffile->stat_record->numpackets_tcp += genericFlow->inPackets;
                    fs->nffile->stat_record->numbytes_tcp += genericFlow->inBytes;
                    break;
                case IPPROTO_UDP:
                    fs->nffile->stat_record->numflows_udp++;
                    fs->nffile->stat_record->numpackets_udp += genericFlow->inPackets;
                    fs->nffile->stat_record->numbytes_udp += genericFlow->inBytes;
                    break;
                default:
                    fs->nffile->stat_record->numflows_other++;
                    fs->nffile->stat_record->numpackets_other += genericFlow->inPackets;
                    fs->nffile->stat_record->numbytes_other += genericFlow->inBytes;
            }
            exporter->flows++;
            fs->nffile->stat_record->numflows++;
            fs->nffile->stat_record->numpackets += genericFlow->inPackets;
            fs->nffile->stat_record->numbytes += genericFlow->inBytes;

            uint32_t exporterIdent = MetricExpporterID(recordHeader);
            UpdateMetric(fs->nffile->ident, exporterIdent, genericFlow);

            if (printRecord) {
                flow_record_short(stdout, recordHeader);
            }

            // advance output buffer
            outBuff += recordHeader->size;
            outSize += recordHeader->size;
            // advance input buffer to next flow record
            v1_record = (netflow_v1_record_t *)((void *)v1_record + NETFLOW_V1_RECORD_LENGTH);

            if (recordHeader->size > exporter->outRecordSize) {
                LogError("Process_v1: Record size check failed! Expected: %u, counted: %u\n", exporter->outRecordSize, recordHeader->size);
                exit(255);
            }

        }  // End of foreach v1 record

        // update file record size ( -> output buffer size )
        fs->dataBlock->NumRecords += count;
        fs->dataBlock->size += outSize;

        // still to go for this many input bytes
        size_left -= NETFLOW_V1_HEADER_LENGTH + count * NETFLOW_V1_RECORD_LENGTH;

        // next header
        v1_header = (netflow_v1_header_t *)v1_record;

        // should never be < 0
        done = size_left <= 0;

    }  // End of while !done

    return;

} /* End of Process_v1 */
