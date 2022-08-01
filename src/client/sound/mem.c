/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
// snd_mem.c: sound caching

#include "sound.h"

wavinfo_t s_info;

#if USE_SNDDMA
/*
================
ResampleSfx
================
*/
static sfxcache_t *ResampleSfx(sfx_t *sfx)
{
    int         outcount;
    int         srcsample;
    float       stepscale;
    int         i;
    int         samplefrac, fracstep;
    sfxcache_t  *sc;

    stepscale = (float)s_info.rate / dma.speed;      // this is usually 0.5, 1, or 2

    outcount = s_info.samples / stepscale;
    if (!outcount) {
        Com_DPrintf("%s resampled to zero length\n", s_info.name);
        sfx->error = Q_ERR_TOO_FEW;
        return NULL;
    }

    sc = sfx->cache = S_Malloc(outcount * s_info.width + sizeof(sfxcache_t) - 1);

    sc->length = outcount;
    sc->loopstart = s_info.loopstart == -1 ? -1 : s_info.loopstart / stepscale;
    sc->width = s_info.width;

// resample / decimate to the current source rate
//Com_Printf("%s: %f, %d\n",sfx->name,stepscale,sc->width);
    if (stepscale == 1) {
// fast special case
        if (sc->width == 1) {
            memcpy(sc->data, s_info.data, outcount);
        } else {
#if __BYTE_ORDER == __LITTLE_ENDIAN
            memcpy(sc->data, s_info.data, outcount << 1);
#else
            for (i = 0; i < outcount; i++) {
                ((uint16_t *)sc->data)[i] = LittleShort(((uint16_t *)s_info.data)[i]);
            }
#endif
        }
    } else {
// general case
        samplefrac = 0;
        fracstep = stepscale * 256;
        if (sc->width == 1) {
            for (i = 0; i < outcount; i++) {
                srcsample = samplefrac >> 8;
                samplefrac += fracstep;
                sc->data[i] = s_info.data[srcsample];
            }
        } else {
            for (i = 0; i < outcount; i++) {
                srcsample = samplefrac >> 8;
                samplefrac += fracstep;
                ((uint16_t *)sc->data)[i] = LittleShort(((uint16_t *)s_info.data)[srcsample]);
            }
        }
    }

    return sc;
}
#endif

/*
===============================================================================

WAV loading

===============================================================================
*/

static byte     *iff_data;
static int      iff_cursize;
static int      iff_readcount;

static int GetLittleShort(void)
{
    int val;

    if (iff_readcount + 2 > iff_cursize) {
        return -1;
    }

    val = LittleShortMem(iff_data + iff_readcount);
    iff_readcount += 2;
    return val;
}

static int GetLittleLong(void)
{
    int val;

    if (iff_readcount + 4 > iff_cursize) {
        return -1;
    }

    val = LittleLongMem(iff_data + iff_readcount);
    iff_readcount += 4;
    return val;
}

static int FindChunk(uint32_t search)
{
    uint32_t chunk, len;
    int remaining;

    while (iff_readcount + 8 < iff_cursize) {
        chunk = GetLittleLong();
        len = GetLittleLong();
        remaining = iff_cursize - iff_readcount;
        if (len > remaining) {
            len = remaining;
        }
        if (chunk == search) {
            return len;
        }
        iff_readcount += ALIGN(len, 2);
    }

    return 0;
}

#define MakeTag(b1,b2,b3,b4) (((unsigned)(b4)<<24)|((b3)<<16)|((b2)<<8)|(b1))

#define TAG_RIFF    MakeTag('R', 'I', 'F', 'F')
#define TAG_WAVE    MakeTag('W', 'A', 'V', 'E')
#define TAG_fmt     MakeTag('f', 'm', 't', ' ')
#define TAG_cue     MakeTag('c', 'u', 'e', ' ')
#define TAG_LIST    MakeTag('L', 'I', 'S', 'T')
#define TAG_mark    MakeTag('m', 'a', 'r', 'k')
#define TAG_data    MakeTag('d', 'a', 't', 'a')

static bool GetWavinfo(void)
{
    int format, samples, width, chunk_len, next_chunk;

// find "RIFF" chunk
    if (!FindChunk(TAG_RIFF)) {
        Com_DPrintf("%s has missing/invalid RIFF chunk\n", s_info.name);
        return false;
    }
    if (GetLittleLong() != TAG_WAVE) {
        Com_DPrintf("%s has missing/invalid WAVE chunk\n", s_info.name);
        return false;
    }

// save position after "WAVE" tag
    next_chunk = iff_readcount;

// find "fmt " chunk
    if (!FindChunk(TAG_fmt)) {
        Com_DPrintf("%s has missing/invalid fmt chunk\n", s_info.name);
        return false;
    }
    format = GetLittleShort();
    if (format != 1) {
        Com_DPrintf("%s has non-Microsoft PCM format\n", s_info.name);
        return false;
    }
    format = GetLittleShort();
    if (format != 1) {
        Com_DPrintf("%s has bad number of channels\n", s_info.name);
        return false;
    }

    s_info.rate = GetLittleLong();
    if (s_info.rate < 8000 || s_info.rate > 48000) {
        Com_DPrintf("%s has bad rate\n", s_info.name);
        return false;
    }

    iff_readcount += 6;
    width = GetLittleShort();
    switch (width) {
    case 8:
        s_info.width = 1;
        break;
    case 16:
        s_info.width = 2;
        break;
    default:
        Com_DPrintf("%s has bad width\n", s_info.name);
        return false;
    }

// find "data" chunk
    iff_readcount = next_chunk;
    chunk_len = FindChunk(TAG_data);
    if (!chunk_len) {
        Com_DPrintf("%s has missing/invalid data chunk\n", s_info.name);
        return false;
    }

    s_info.samples = chunk_len / s_info.width;
    if (!s_info.samples) {
        Com_DPrintf("%s has zero length\n", s_info.name);
        return false;
    }

    s_info.data = iff_data + iff_readcount;
    s_info.loopstart = -1;

// find "cue " chunk
    iff_readcount = next_chunk;
    chunk_len = FindChunk(TAG_cue);
    if (!chunk_len) {
        return true;
    }

// save position after "cue " chunk
    next_chunk = iff_readcount + ALIGN(chunk_len, 2);

    iff_readcount += 24;
    samples = GetLittleLong();
    if (samples < 0 || samples >= s_info.samples) {
        Com_DPrintf("%s has bad loop start\n", s_info.name);
        return true;
    }
    s_info.loopstart = samples;

// if the next chunk is a "LIST" chunk, look for a cue length marker
    iff_readcount = next_chunk;
    if (!FindChunk(TAG_LIST)) {
        return true;
    }

    iff_readcount += 20;
    if (GetLittleLong() != TAG_mark) {
        return true;
    }

// this is not a proper parse, but it works with cooledit...
    iff_readcount -= 8;
    samples = GetLittleLong();  // samples in loop
    if (samples < 1 || samples > s_info.samples - s_info.loopstart) {
        Com_DPrintf("%s has bad loop length\n", s_info.name);
        return true;
    }
    s_info.samples = s_info.loopstart + samples;

    return true;
}

/*
==============
S_LoadSound
==============
*/
sfxcache_t *S_LoadSound(sfx_t *s)
{
    byte        *data;
    sfxcache_t  *sc;
    int         len;
    char        *name;

    if (s->name[0] == '*')
        return NULL;

// see if still in memory
    sc = s->cache;
    if (sc)
        return sc;

// don't retry after error
    if (s->error)
        return NULL;

// load it in
    if (s->truename)
        name = s->truename;
    else
        name = s->name;

    len = FS_LoadFile(name, (void **)&data);
    if (!data) {
        s->error = len;
        return NULL;
    }

    memset(&s_info, 0, sizeof(s_info));
    s_info.name = name;

    iff_data = data;
    iff_cursize = len;
    iff_readcount = 0;

    if (!GetWavinfo()) {
        s->error = Q_ERR_INVALID_FORMAT;
        goto fail;
    }

#if USE_OPENAL
    if (s_started == SS_OAL)
        sc = AL_UploadSfx(s);
#endif

#if USE_SNDDMA
    if (s_started == SS_DMA)
        sc = ResampleSfx(s);
#endif

fail:
    FS_FreeFile(data);
    return sc;
}
