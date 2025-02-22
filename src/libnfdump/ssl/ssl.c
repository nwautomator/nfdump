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

#include "ssl.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stream.h"
#include "util.h"

// array handling

static int sslParseExtensions(ssl_t *ssl, BytesStream_t sslStream, uint16_t length);

static int sslParseClientHandshake(ssl_t *ssl, BytesStream_t sslStream, uint32_t messageLength);

static int sslParseServerHandshake(ssl_t *ssl, BytesStream_t sslStream, uint32_t messageLength);

static int checkGREASE(uint16_t val);

/*
 * grease_table = {0x0a0a, 0x1a1a, 0x2a2a, 0x3a3a,
 *              0x4a4a, 0x5a5a, 0x6a6a, 0x7a7a,
 *              0x8a8a, 0x9a9a, 0xaaaa, 0xbaba,
 *              0xcaca, 0xdada, 0xeaea, 0xfafa};
 */
static int checkGREASE(uint16_t val) {
    if ((val & 0x0f0f) != 0x0a0a) {
        return 0;
    } else {
        uint8_t *p = (uint8_t *)&val;
        return p[0] == p[1] ? 1 : 0;
    }
    // not reached

}  // End of checkGrease

static int ProcessExtSNI(ssl_t *ssl, BytesStream_t *sslStream) {
    uint16_t sniListLength;
    ByteStream_GET_u16(*sslStream, sniListLength);

    // skip server name type 1
    ByteStream_SKIP(*sslStream, 1);

    uint16_t sniLen;
    ByteStream_GET_u16(*sslStream, sniLen);
    if (sniLen > ByteStream_AVAILABLE(*sslStream) || sniLen > 255) {
        LogError("%s():%d sni extension length error", __FUNCTION__, __LINE__);
        return 0;
    }

    ByteStream_GET_X(*sslStream, ssl->sniName, sniLen);
    ssl->sniName[sniLen] = '\0';
    dbg_printf("Found sni name: %s\n", ssl->sniName);

    if ((sniLen + 3) < sniListLength) {
        // should not happen as only one host_type suported
        size_t skipBytes = sniListLength - sniLen - 3;
        ByteStream_SKIP(*sslStream, skipBytes);
    }

    return 1;
}  // End of ProcessExtSNI

static int ProcessExtElCurves(ssl_t *ssl, BytesStream_t *sslStream) {
    uint16_t ecsLen;
    ByteStream_GET_u16(*sslStream, ecsLen);

    if (ecsLen > ByteStream_AVAILABLE(*sslStream)) {
        LogError("%s():%d ecs extension length error", __FUNCTION__, __LINE__);
        return 0;
    }

    for (int i = 0; i < (ecsLen >> 1); i++) {
        uint16_t curve;
        ByteStream_GET_u16(*sslStream, curve);
        AppendArray(ssl->ellipticCurves, curve);
        dbg_printf("Found curve: 0x%x\n", curve);
    }
    return 1;
}  // End of ProcessExtElCurves

static int ProcessSignatures(ssl_t *ssl, BytesStream_t *sslStream) {
    uint16_t sigLen;
    ByteStream_GET_u16(*sslStream, sigLen);

    if (sigLen > ByteStream_AVAILABLE(*sslStream)) {
        LogError("%s():%d ecsp extension length error", __FUNCTION__, __LINE__);
        return 0;
    }

    for (int i = 0; i < sigLen >> 1; i++) {
        uint16_t signature;
        ByteStream_GET_u16(*sslStream, signature);
        AppendArray(ssl->signatures, signature);
        dbg_printf("Found signature: 0x%x\n", signature);
    }
    return 1;
}  // End of ProcessSignatures

static int ProcessExtElCurvesPoints(ssl_t *ssl, BytesStream_t *sslStream) {
    uint8_t ecspLen;
    ByteStream_GET_u8(*sslStream, ecspLen);

    if (ecspLen > ByteStream_AVAILABLE(*sslStream)) {
        LogError("%s():%d ecsp extension length error", __FUNCTION__, __LINE__);
        return 0;
    }

    for (int i = 0; i < ecspLen; i++) {
        uint8_t curvePF;
        ByteStream_GET_u8(*sslStream, curvePF);
        AppendArray(ssl->ellipticCurvesPF, curvePF);
        dbg_printf("Found curvePF: 0x%x\n", curvePF);
    }
    return 1;
}  // End of ProcessExtElCurvesPoints

static int ProcessExtALPN(ssl_t *ssl, BytesStream_t *sslStream) {
    uint16_t alpnLength;
    ByteStream_GET_u16(*sslStream, alpnLength);

    if (alpnLength > ByteStream_AVAILABLE(*sslStream)) {
        printf("## alpnLength: %u, available: %zu\n", alpnLength, ByteStream_AVAILABLE(*sslStream));
        LogError("%s(): ALPN extension length error in %s:%d", __FUNCTION__, __FILE__, __LINE__);
        return 0;
    }

    uint32_t alpnCnt = 0;
    do {
        uint8_t alpnStrLen;
        ByteStream_GET_u8(*sslStream, alpnStrLen);
        if (alpnCnt == 0) {  // add first ALPN
            ByteStream_GET_X(*sslStream, ssl->alpnName, alpnStrLen);
            ssl->alpnName[alpnStrLen] = '\0';
            dbg_printf("Found first ALPN: %s\n", ssl->alpnName);
        } else {
            ByteStream_SKIP(*sslStream, alpnStrLen);
        }
        alpnCnt += (alpnStrLen + 1);
    } while ((alpnCnt < alpnLength) && ByteStream_NO_ERROR(*sslStream));
    if (ByteStream_IS_ERROR(*sslStream)) {
        LogError("%s():%d ALPN decoding error", __FUNCTION__, __LINE__);
        return 0;
    }
    return 1;
}  // End of ProcessExtALPN

static int sslParseExtensions(ssl_t *ssl, BytesStream_t sslStream, uint16_t length) {
    dbg_printf("Parse extensions: %x\n", length);
    if (length == 0) {
        LogError("%s() extension length is 0", __FUNCTION__);
        return 0;
    }

    int extensionLength = length;
    NewArray(ssl->extensions);
    NewArray(ssl->ellipticCurves);
    NewArray(ssl->ellipticCurvesPF);
    while (extensionLength >= 4) {
        uint16_t exType, exLength;
        ByteStream_GET_u16(sslStream, exType);
        ByteStream_GET_u16(sslStream, exLength);
        dbg_printf("Ex Type: %x, Length: %x\n", exType, exLength);

        if (checkGREASE(exType)) {
            extensionLength -= (4 + exLength);
            if (exLength) ByteStream_SKIP(sslStream, exLength);
            continue;
        }

        if (exLength > ByteStream_AVAILABLE(sslStream)) {
            LogError("%s():%d extension length error", __FUNCTION__, __LINE__);
            return 0;
        }

        AppendArray(ssl->extensions, exType);
        int ret = 1;
        switch (exType) {
            case 0:  // sni name
                ret = ProcessExtSNI(ssl, &sslStream);
                break;
            case 10:  // Elliptic curves
                ret = ProcessExtElCurves(ssl, &sslStream);
                break;
            case 11:  // Elliptic curve point formats uncompressed
                ret = ProcessExtElCurvesPoints(ssl, &sslStream);
                break;
            case 13:  // signatures
                ret = ProcessSignatures(ssl, &sslStream);
                break;
            case 16:  // application_layer_protocol_negotiation (16)
                ret = ProcessExtALPN(ssl, &sslStream);
                break;
            default:
                if (exLength) ByteStream_SKIP(sslStream, exLength);
        }
        if (ret == 0) return 0;
        extensionLength -= (4 + exLength);
    }
    dbg_printf("End extension. size: %d\n", extensionLength);

    return 1;

}  // End of sslParseExtensions

static int sslParseClientHandshake(ssl_t *ssl, BytesStream_t sslStream, uint32_t messageLength) {
    // version(2) random(32) sessionIDLen(1) = 35 bytes
    if (ByteStream_AVAILABLE(sslStream) < 35) return 0;

    uint16_t version;
    ByteStream_GET_u16(sslStream, version);  // client hello protocol version
    ByteStream_SKIP(sslStream, 32);          // random init bytes

    /*
    0x0304 = TLS 1.3 = “13”
    0x0303 = TLS 1.2 = “12”
    0x0302 = TLS 1.1 = “11”
    0x0301 = TLS 1.0 = “10”
    0x0300 = SSL 3.0 = “s3”
    0x0002 = SSL 2.0 = “s2”

    Unknown = “00”
    */
    ssl->protocolVersion = version;
    switch (version) {
        case 0x0002:  // SSL 2.0
            ssl->tlsCharVersion[0] = 's';
            ssl->tlsCharVersion[1] = '2';
            break;
        case 0x0300:  // SSL 3.0
            ssl->tlsCharVersion[0] = 's';
            ssl->tlsCharVersion[1] = '3';
            break;
        case 0x0301:  // TLS 1.0
            ssl->tlsCharVersion[0] = '1';
            ssl->tlsCharVersion[1] = '0';
            break;
        case 0x0302:  // TLS 1.2
            ssl->tlsCharVersion[0] = '1';
            ssl->tlsCharVersion[1] = '1';
            break;
        case 0x0303:  // TLS 1.2
            ssl->tlsCharVersion[0] = '1';
            ssl->tlsCharVersion[1] = '2';
            break;
        case 0x0304:  // TLS 1.3
            ssl->tlsCharVersion[0] = '1';
            ssl->tlsCharVersion[1] = '3';
            break;
        default:
            LogError("%s():%d Not an SSL 2.0 - TLS 1.3 protocol", __FUNCTION__, __LINE__);
            dbg_printf("Client handshake: Not an SSL 2.0 - TLS 1.3 protocol\n");
            return 0;
    }

    uint8_t sessionIDLen;
    ByteStream_GET_u8(sslStream, sessionIDLen);  // session ID length (followed by session ID if non-zero)

    // sessionIDLen + cipherSuiteHeaderLen(2)
    if (ByteStream_AVAILABLE(sslStream) < (sessionIDLen + 2)) return 0;
    if (sessionIDLen) ByteStream_SKIP(sslStream, sessionIDLen);

    uint16_t cipherSuiteHeaderLen;
    ByteStream_GET_u16(sslStream, cipherSuiteHeaderLen);  // Cipher suites length

    // cipherSuiteHeaderLen + compressionMethodes(1)
    if (ByteStream_AVAILABLE(sslStream) < (cipherSuiteHeaderLen + 1)) return 0;

    int numCiphers = cipherSuiteHeaderLen >> 1;
    if (numCiphers == 0) {
        LogError("%s():%d Number of ciphers is 0", __FUNCTION__, __LINE__);
        return 0;
    }

    NewArray(ssl->cipherSuites);
    for (int i = 0; i < numCiphers; i++) {
        uint16_t cipher;
        ByteStream_GET_u16(sslStream, cipher);  // get next cipher

        if (checkGREASE(cipher) == 0) {
            AppendArray(ssl->cipherSuites, cipher);
        }
    }

    uint8_t compressionMethodes;
    ByteStream_GET_u8(sslStream, compressionMethodes);  // number of compression methods to follow

    // compressionMethodes extensionLength(2)
    if (ByteStream_AVAILABLE(sslStream) < (compressionMethodes + 2)) return 0;
    if (compressionMethodes) ByteStream_SKIP(sslStream, compressionMethodes);

    uint16_t extensionLength;
    ByteStream_GET_u16(sslStream, extensionLength);  // length of extensions

    if (ByteStream_AVAILABLE(sslStream) < (extensionLength)) return 0;

    return sslParseExtensions(ssl, sslStream, extensionLength);

}  // End of sslParseClientHandshake

static int sslParseServerHandshake(ssl_t *ssl, BytesStream_t sslStream, uint32_t messageLength) {
    // version(2) random(32) sessionIDLen(1) = 35 bytes
    if (ByteStream_AVAILABLE(sslStream) < 35) return 0;

    uint16_t version;
    ByteStream_GET_u16(sslStream, version);  // client hello protocol version
    ByteStream_SKIP(sslStream, 32);          // random init bytes

    ssl->protocolVersion = version;
    switch (version) {
        case 0x0002:  // SSL 2.0
            ssl->tlsCharVersion[0] = 's';
            ssl->tlsCharVersion[1] = '2';
            break;
        case 0x0300:  // SSL 3.0
            ssl->tlsCharVersion[0] = 's';
            ssl->tlsCharVersion[1] = '3';
            break;
        case 0x0301:  // TLS 1.1
            ssl->tlsCharVersion[0] = '1';
            ssl->tlsCharVersion[1] = '0';
            break;
        case 0x0302:  // TLS 1.2
            ssl->tlsCharVersion[0] = '1';
            ssl->tlsCharVersion[1] = '2';
            break;
        case 0x0303:  // TLS 1.3
            ssl->tlsCharVersion[0] = '1';
            ssl->tlsCharVersion[1] = '3';
            break;
        default:
            LogError("%s():%d Not an SSL 2.0 - TLS 1.3 protocol", __FUNCTION__, __LINE__);
            dbg_printf("Client handshake: Not an SSL 2.0 - TLS 1.3 protocol\n");
            return 0;
    }

    uint8_t sessionIDLen;
    ByteStream_GET_u8(sslStream, sessionIDLen);  // session ID length (followed by session ID if non-zero)

    // sessionIDLen + cipherSuite (2) + compression(1) + extensionLength(2)
    if (ByteStream_AVAILABLE(sslStream) < (sessionIDLen + 5)) return 0;
    if (sessionIDLen) ByteStream_SKIP(sslStream, sessionIDLen);

    uint16_t cipherSuite;
    ByteStream_GET_u16(sslStream, cipherSuite);  // Cipher suite

    NewArray(ssl->cipherSuites);
    AppendArray(ssl->cipherSuites, cipherSuite);

    // skip compression
    ByteStream_SKIP(sslStream, 1);

    uint16_t extensionLength;
    ByteStream_GET_u16(sslStream, extensionLength);  // extension length

    if (ByteStream_AVAILABLE(sslStream) < extensionLength) return 0;

    NewArray(ssl->extensions);

    int sizeLeft = extensionLength;
    while (sizeLeft >= 4) {
        uint16_t exType, exLength;
        ByteStream_GET_u16(sslStream, exType);
        ByteStream_GET_u16(sslStream, exLength);

        sizeLeft -= (4 + exLength);
        if (checkGREASE(exType)) {
            if (exLength) ByteStream_SKIP(sslStream, exLength);
            continue;
        }

        dbg_printf("Found extension type: %u, len: %u\n", exType, exLength);
        AppendArray(ssl->extensions, exType);
        if (exLength) ByteStream_SKIP(sslStream, exLength);
    }
    dbg_printf("End extension. size: %d\n", sizeLeft);

    return 1;

}  // End of sslParseServerHandshake

void sslPrint(ssl_t *ssl) {
    if (ssl->type == CLIENTssl)
        printf("ssl client record for %s:\n", ssl->sniName);
    else
        printf("ssl server record\n");

    printf("TLS        : 0x%x\n", ssl->tlsVersion);
    printf("Protocol   : 0x%x\n", ssl->protocolVersion);
    printf("ciphers    : ");
    for (int i = 0; i < LenArray(ssl->cipherSuites); i++) {
        printf("0x%x ", ssl->cipherSuites.array[i]);
    }
    printf("\nextensions :");
    for (int i = 0; i < LenArray(ssl->extensions); i++) {
        printf(" 0x%x", ssl->extensions.array[i]);
    }
    printf("\nsignatures :");
    for (int i = 0; i < LenArray(ssl->signatures); i++) {
        printf(" 0x%x", ssl->signatures.array[i]);
    }
    printf("\n");

    if (ssl->sniName[0]) {
        printf("SNI name   : %s\n", ssl->sniName);
    }

    if (ssl->alpnName[0]) {
        printf("ALPN name  : %s\n", ssl->alpnName);
    }

    if (ssl->type == CLIENTssl) {
        printf("curves     :");
        for (int i = 0; i < LenArray(ssl->ellipticCurves); i++) {
            printf(" 0x%x", ssl->ellipticCurves.array[i]);
        }
        printf("\ncurves PF  :");
        for (int i = 0; i < LenArray(ssl->ellipticCurvesPF); i++) {
            printf(" 0x%x", ssl->ellipticCurvesPF.array[i]);
        }
        printf("\n");
    }

}  // End of sslPrint

void sslFree(ssl_t *ssl) {
    FreeArray(ssl->cipherSuites);
    FreeArray(ssl->extensions);
    FreeArray(ssl->ellipticCurves);
    FreeArray(ssl->ellipticCurvesPF);

    free(ssl);

}  // End of sslFree

ssl_t *sslProcess(const uint8_t *data, size_t len) {
    dbg_printf("\nsslProcess new packet. size: %zu\n", len);
    // Check for ssl record
    // - TLS header length (5)
    // - message type/length (4)
    //
    // TLS Record header
    // 0--------8-------16-------24-------32-------40
    // | type   |     version     |     length      | TLS Record header
    // +--------+--------+--------+--------+--------+
    //
    // type:
    // Record Type Values       dec      hex
    // -------------------------------------
    // CHANGE_CIPHER_SPEC        20     0x14
    // ALERT                     21     0x15
    // HANDSHAKE                 22     0x16
    // APPLICATION_DATA          23     0x17
    //
    // version:
    // Version Values            dec     hex
    // -------------------------------------
    // SSL 3.0                   3,0  0x0300
    // TLS 1.0                   3,1  0x0301
    // TLS 1.1                   3,2  0x0302
    // TLS 1.2                   3,3  0x0303
    //
    // record type (1 byte)

    // - and handshake content type (22)
    if (len < 9 || data[0] != 0x16) {
        dbg_printf("Not a TLS handshake record: 0x%x\n", data[0]);
        return NULL;
    }

    ByteStream_INIT(sslStream, data, len);
    ByteStream_SKIP(sslStream, 1);  // 0x22 data[0]

    uint16_t sslVersion;
    ByteStream_GET_u16(sslStream, sslVersion);
    switch (sslVersion) {
        case 0x0002:  // SSL 2.0
        case 0x0300:  // SSL 3.0
        case 0x0301:  // TLS 1.1
        case 0x0302:  // TLS 1.2
        case 0x0303:  // TLS 1.3
            break;
        default:
            dbg_printf("SSL version: 0x%x not SSL 2.0 - TLS 1.3 connection\n", sslVersion);
            return NULL;
    }

    uint16_t contentLength;
    ByteStream_GET_u16(sslStream, contentLength);

    if (contentLength > ByteStream_AVAILABLE(sslStream)) {
        dbg_printf("Short ssl packet -  have: %zu, need contentLength: %u\n", len, contentLength);
        return NULL;
    }

    uint8_t messageType;
    uint32_t messageLength;
    ByteStream_GET_u8(sslStream, messageType);
    ByteStream_GET_u24(sslStream, messageLength);

    dbg_printf("Message type: %u, length: %u\n", messageType, messageLength);
    if (messageLength > ByteStream_AVAILABLE(sslStream)) {
        dbg_printf("Message length error: %u > %zu\n", messageLength, len);
        return NULL;
    }

    ssl_t *ssl = (ssl_t *)calloc(1, sizeof(ssl_t));
    if (!ssl) {
        LogError("calloc() error in %s line %d: %s", __FILE__, __LINE__, strerror(errno));
        return NULL;
    }
    ssl->tlsVersion = sslVersion;

    int ok = 0;
    switch (messageType) {
        case 0:  // hello_request(0)
            break;
        case 1:  // client_hello(1)
            ssl->type = CLIENTssl;
            ok = sslParseClientHandshake(ssl, sslStream, messageLength);
            break;
        case 2:  // server_hello(2),
            ssl->type = SERVERssl;
            ok = sslParseServerHandshake(ssl, sslStream, messageLength);
            break;
        case 11:  // certificate(11)
        case 12:  // server_key_exchange (12),
        case 13:  //  certificate_request(13)
        case 14:  //  server_hello_done(14),
        case 15:  // certificate_verify(15)
        case 16:  // client_key_exchange(16)
        case 20:  // finished(20),
            break;
        default:
            dbg_printf("ssl process: Message type not ClientHello or ServerHello: %u\n", messageType);
            sslFree(ssl);
            return NULL;
    }

    if (!ok) {
        sslFree(ssl);
        return NULL;
    }

    dbg_printf("ssl process message: %u, Length: %u\n", messageType, messageLength);
    // sslPrint(ssl);

    return ssl;

}  // End of sslProcess

#ifdef MAIN
void sslTest(void) {
    const uint8_t clientHello2[] = {
        0x16, 0x03, 0x01, 0x00, 0xc8, 0x01, 0x00, 0x00, 0xc4, 0x03, 0x03, 0xec, 0x12, 0xdd, 0x17, 0x64, 0xa4, 0x39, 0xfd, 0x7e, 0x8c, 0x85, 0x46,
        0xb8, 0x4d, 0x1e, 0xa0, 0x6e, 0xb3, 0xd7, 0xa0, 0x51, 0xf0, 0x3c, 0xb8, 0x17, 0x47, 0x0d, 0x4c, 0x54, 0xc5, 0xdf, 0x72, 0x00, 0x00, 0x1c,
        0xea, 0xea, 0xc0, 0x2b, 0xc0, 0x2f, 0xc0, 0x2c, 0xc0, 0x30, 0xcc, 0xa9, 0xcc, 0xa8, 0xc0, 0x13, 0xc0, 0x14, 0x00, 0x9c, 0x00, 0x9d, 0x00,
        0x2f, 0x00, 0x35, 0x00, 0x0a, 0x01, 0x00, 0x00, 0x7f, 0xda, 0xda, 0x00, 0x00, 0xff, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x16, 0x00,
        0x14, 0x00, 0x00, 0x11, 0x77, 0x77, 0x77, 0x2e, 0x77, 0x69, 0x6b, 0x69, 0x70, 0x65, 0x64, 0x69, 0x61, 0x2e, 0x6f, 0x72, 0x67, 0x00, 0x17,
        0x00, 0x00, 0x00, 0x23, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x14, 0x00, 0x12, 0x04, 0x03, 0x08, 0x04, 0x04, 0x01, 0x05, 0x03, 0x08, 0x05, 0x05,
        0x01, 0x08, 0x06, 0x06, 0x01, 0x02, 0x01, 0x00, 0x05, 0x00, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x10, 0x00,
        0x0e, 0x00, 0x0c, 0x02, 0x68, 0x32, 0x08, 0x68, 0x74, 0x74, 0x70, 0x2f, 0x31, 0x2e, 0x31, 0x75, 0x50, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x02,
        0x01, 0x00, 0x00, 0x0a, 0x00, 0x0a, 0x00, 0x08, 0x1a, 0x1a, 0x00, 0x1d, 0x00, 0x17, 0x00, 0x18, 0x1a, 0x1a, 0x00, 0x01, 0x00};

    uint8_t clientHello[] = {
        0x16, 0x03, 0x01, 0x02, 0x00, 0x01, 0x00, 0x01, 0xFC, 0x03, 0x03, 0x3C, 0xCA, 0xDD, 0xA8, 0xB0, 0x3F, 0x00, 0xBB, 0xCB, 0x0E, 0x41, 0x8B,
        0xEF, 0x0E, 0xEC, 0x8E, 0xDC, 0x44, 0xDF, 0x52, 0x3A, 0x31, 0x86, 0x8F, 0x72, 0xD1, 0xD1, 0xCC, 0x6F, 0xC1, 0x79, 0x46, 0x20, 0x41, 0xB3,
        0x5E, 0x05, 0x64, 0x48, 0x95, 0x04, 0x84, 0xF5, 0x5B, 0x62, 0xDD, 0xD6, 0x1F, 0xB8, 0xE6, 0x4E, 0x2D, 0xAD, 0xC5, 0xBF, 0x67, 0x16, 0x66,
        0x61, 0x17, 0xDB, 0x27, 0x4F, 0xDC, 0x86, 0x00, 0x2A, 0xDA, 0xDA, 0x13, 0x01, 0x13, 0x02, 0x13, 0x03, 0xC0, 0x2C, 0xC0, 0x2B, 0xCC, 0xA9,
        0xC0, 0x30, 0xC0, 0x2F, 0xCC, 0xA8, 0xC0, 0x0A, 0xC0, 0x09, 0xC0, 0x14, 0xC0, 0x13, 0x00, 0x9D, 0x00, 0x9C, 0x00, 0x35, 0x00, 0x2F, 0xC0,
        0x08, 0xC0, 0x12, 0x00, 0x0A, 0x01, 0x00, 0x01, 0x89, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1A, 0x00, 0x18, 0x00, 0x00, 0x15, 0x77,
        0x77, 0x77, 0x2E, 0x6D, 0x61, 0x72, 0x6B, 0x64, 0x6F, 0x77, 0x6E, 0x67, 0x75, 0x69, 0x64, 0x65, 0x2E, 0x6F, 0x72, 0x67, 0x00, 0x17, 0x00,
        0x00, 0xFF, 0x01, 0x00, 0x01, 0x00, 0x00, 0x0A, 0x00, 0x0C, 0x00, 0x0A, 0xCA, 0xCA, 0x00, 0x1D, 0x00, 0x17, 0x00, 0x18, 0x00, 0x19, 0x00,
        0x0B, 0x00, 0x02, 0x01, 0x00, 0x00, 0x10, 0x00, 0x0E, 0x00, 0x0C, 0x02, 0x68, 0x32, 0x08, 0x68, 0x74, 0x74, 0x70, 0x2F, 0x31, 0x2E, 0x31,
        0x00, 0x05, 0x00, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D, 0x00, 0x18, 0x00, 0x16, 0x04, 0x03, 0x08, 0x04, 0x04, 0x01, 0x05, 0x03,
        0x02, 0x03, 0x08, 0x05, 0x08, 0x05, 0x05, 0x01, 0x08, 0x06, 0x06, 0x01, 0x02, 0x01, 0x00, 0x12, 0x00, 0x00, 0x00, 0x33, 0x00, 0x2B, 0x00,
        0x29, 0xCA, 0xCA, 0x00, 0x01, 0x00, 0x00, 0x1D, 0x00, 0x20, 0x55, 0x8B, 0xA5, 0x3F, 0x92, 0x92, 0xF8, 0x1B, 0xB5, 0xA8, 0xE2, 0xA9, 0xD2,
        0xEF, 0xAF, 0x90, 0x41, 0x69, 0x4E, 0x93, 0xFE, 0x77, 0x62, 0x17, 0x2F, 0xB8, 0x9E, 0x9C, 0xF7, 0x29, 0x1C, 0x4B, 0x00, 0x2D, 0x00, 0x02,
        0x01, 0x01, 0x00, 0x2B, 0x00, 0x0B, 0x0A, 0xDA, 0xDA, 0x03, 0x04, 0x03, 0x03, 0x03, 0x02, 0x03, 0x01, 0x00, 0x1B, 0x00, 0x03, 0x02, 0x00,
        0x01, 0x3A, 0x3A, 0x00, 0x01, 0x00, 0x00, 0x15, 0x00, 0xB9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x08, 0x00,
    };

    size_t len = sizeof(clientHello);

    ssl_t *ssl = sslProcess(clientHello, len);
    if (ssl)
        sslPrint(ssl);
    else
        printf("Failed to parse ssl\n");
}

int main(int argc, char **argv) {
    sslTest();
    return 0;
}

#endif
