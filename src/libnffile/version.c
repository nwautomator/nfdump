/*
 *  Copyright (c) 2009-2023, Peter Haag
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

#include "version.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "vcs_track.h"

char *versionString(void) {
    static char version_string[128];

    char *nsel = "";
#ifdef NSEL
    nsel = " NSEL-NEL";
#endif

    char *zstdlib = "";
#ifdef HAVE_ZSTD
    zstdlib = " ZSTD";
#endif

    char *bzlib = "";
#ifdef HAVE_ZSTD
    bzlib = " BZIP2";
#endif

    char *option = (strlen(nsel) + strlen(zstdlib)) > 0 ? "Options:" : "";
    snprintf(version_string, 128, "Version: %s-%s %s%s%s%s Date: %s", VERSION, VCS_TRACK_HASH, option, nsel, zstdlib, bzlib, VCS_TRACK_DATE);
    version_string[127] = '\0';

    return version_string;
}
