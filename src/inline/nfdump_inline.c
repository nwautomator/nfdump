/*
 *  Copyright (c) 2009-2024, Peter Haag
 *  Copyright (c) 2004-2008, SWITCH - Teleinformatikdienste fuer Lehre und Forschung
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *	 this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright notice,
 *	 this list of conditions and the following disclaimer in the documentation
 *	 and/or other materials provided with the distribution.
 *   * Neither the name of the author nor the names of its contributors may be
 *	 used to endorse or promote products derived from this software without
 *	 specific prior written permission.
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

static inline void UpdateStatRecord(stat_record_t *stat_record, recordHandle_t *recordHandle);

static inline void UpdateStatRecord(stat_record_t *stat_record, recordHandle_t *recordHandle) {
    EXgenericFlow_t *genericFlow = (EXgenericFlow_t *)recordHandle->extensionList[EXgenericFlowID];
    EXcntFlow_t *cntFlow = (EXcntFlow_t *)recordHandle->extensionList[EXcntFlowID];

    if (!genericFlow) return;

    uint64_t inPackets = genericFlow->inPackets;
    uint64_t inBytes = genericFlow->inBytes;
    uint64_t outPackets, outBytes, flows;
    if (cntFlow) {
        outPackets = cntFlow->outPackets;
        outBytes = cntFlow->outBytes;
        flows = cntFlow->flows;
    } else {
        outPackets = 0;
        outBytes = 0;
        flows = 0;
    }

    switch (genericFlow->proto) {
        case IPPROTO_ICMP:
        case IPPROTO_ICMPV6:
            stat_record->numflows_icmp += flows ? flows : 1;
            stat_record->numpackets_icmp += inPackets;
            stat_record->numpackets_icmp += outPackets;
            stat_record->numbytes_icmp += inBytes;
            stat_record->numbytes_icmp += outBytes;
            break;
        case IPPROTO_TCP:
            stat_record->numflows_tcp += flows ? flows : 1;
            stat_record->numpackets_tcp += inPackets;
            stat_record->numpackets_tcp += outPackets;
            stat_record->numbytes_tcp += inBytes;
            stat_record->numbytes_tcp += outBytes;
            break;
        case IPPROTO_UDP:
            stat_record->numflows_udp += flows ? flows : 1;
            stat_record->numpackets_udp += inPackets;
            stat_record->numpackets_udp += outPackets;
            stat_record->numbytes_udp += inBytes;
            stat_record->numbytes_udp += outBytes;
            break;
        default:
            stat_record->numflows_other += flows ? flows : 1;
            stat_record->numpackets_other += inPackets;
            stat_record->numpackets_other += outPackets;
            stat_record->numbytes_other += inBytes;
            stat_record->numbytes_other += outBytes;
    }
    stat_record->numflows += flows ? flows : 1;
    stat_record->numpackets += inPackets;
    stat_record->numpackets += outPackets;
    stat_record->numbytes += inBytes;
    stat_record->numbytes += outBytes;

    if (genericFlow->msecFirst < stat_record->firstseen) {
        stat_record->firstseen = genericFlow->msecFirst;
    }
    if (genericFlow->msecLast > stat_record->lastseen) {
        stat_record->lastseen = genericFlow->msecLast;
    }

}  // End of UpdateStat

static inline void UpdateRawStat(stat_record_t *stat_record, EXgenericFlow_t *genericFlow, EXcntFlow_t *cntFlow);

static inline void UpdateRawStat(stat_record_t *stat_record, EXgenericFlow_t *genericFlow, EXcntFlow_t *cntFlow) {
    uint64_t inBytes, inPackets, outBytes, outPackets, flows;
    uint64_t msecFirst, msecLast;
    uint32_t proto;

    if (genericFlow) {
        proto = genericFlow->proto;
        inPackets = genericFlow->inPackets;
        inBytes = genericFlow->inBytes;
        msecFirst = genericFlow->msecFirst;
        msecLast = genericFlow->msecLast;
    } else {
        proto = 0;
        inPackets = 0;
        inBytes = 0;
        msecFirst = 0;
        msecLast = 0;
    }
    if (cntFlow) {
        outPackets = cntFlow->outPackets;
        outBytes = cntFlow->outBytes;
        flows = cntFlow->flows;
    } else {
        outPackets = 0;
        outBytes = 0;
        flows = 1;
    }

    switch (proto) {
        case IPPROTO_ICMP:
        case IPPROTO_ICMPV6:
            stat_record->numflows_icmp += flows;
            stat_record->numpackets_icmp += inPackets;
            stat_record->numpackets_icmp += outPackets;
            stat_record->numbytes_icmp += inBytes;
            stat_record->numbytes_icmp += outBytes;
            break;
        case IPPROTO_TCP:
            stat_record->numflows_tcp += flows;
            stat_record->numpackets_tcp += inPackets;
            stat_record->numpackets_tcp += outPackets;
            stat_record->numbytes_tcp += inBytes;
            stat_record->numbytes_tcp += outBytes;
            break;
        case IPPROTO_UDP:
            stat_record->numflows_udp += flows;
            stat_record->numpackets_udp += inPackets;
            stat_record->numpackets_udp += outPackets;
            stat_record->numbytes_udp += inBytes;
            stat_record->numbytes_udp += outBytes;
            break;
        default:
            stat_record->numflows_other += flows;
            stat_record->numpackets_other += inPackets;
            stat_record->numpackets_other += outPackets;
            stat_record->numbytes_other += inBytes;
            stat_record->numbytes_other += outBytes;
    }
    stat_record->numflows += flows;
    stat_record->numpackets += inPackets;
    stat_record->numpackets += outPackets;
    stat_record->numbytes += inBytes;
    stat_record->numbytes += outBytes;

    if (msecFirst < stat_record->firstseen) {
        stat_record->firstseen = msecFirst;
    }
    if (msecLast > stat_record->lastseen) {
        stat_record->lastseen = msecLast;
    }

}  // End of UpdateRawStat
