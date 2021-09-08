/*
 * WebP demuxer
 * Copyright (c) 2020 Pexeso Inc.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * WebP demuxer.
 */

// https://patchwork.ffmpeg.org/project/ffmpeg/list/?submitter=1040
// https://patchwork.ffmpeg.org/project/ffmpeg/patch/20200911063613.4475-2-josef@pex.com/

#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavcodec/webp.h"

/**
 * WebP headers (chunks before the first frame) and important info from them.
 */
typedef struct WebPHeaders {
    int64_t offset;                 ///< offset in the (concatenated) file
    uint8_t *data;                  ///< raw data
    uint32_t size;                  ///< size of data
    uint32_t webp_size;             ///< size of the WebP file
    int canvas_width;               ///< width of the canvas
    int canvas_height;              ///< height of the canvas
    int num_loop;                   ///< number of times to loop the animation
} WebPHeaders;

typedef struct WebPDemuxContext {
    const AVClass *class;
    /**
     * Time span in milliseconds before the next frame
     * should be drawn on screen.
     */
    int delay;
    /**
     * Minimum allowed delay between frames in milliseconds.
     * Values below this threshold are considered to be invalid
     * and set to value of default_delay.
     */
    int min_delay;
    int max_delay;
    int default_delay;

    /*
     * loop options
     */
    int ignore_loop;                ///< ignore loop setting
    int num_loop;                   ///< number of times to loop the animation
    int cur_loop;                   ///< current loop counter
    int64_t file_start;             ///< start position of the current animation file
    int64_t infinite_loop_start;    ///< start position of the infinite loop

    uint32_t remaining_size;        ///< remaining size of the current animation file
    int64_t seekback_buffer_end;    ///< position of the end of the seek back buffer
    int64_t prev_end_position;      ///< position after the previous packet
    size_t num_webp_headers;        ///< number of (concatenated) WebP files' headers
    WebPHeaders *webp_headers;      ///< (concatenated) WebP files' headers

    /*
     * variables for the key frame detection
     */
    int nb_frames;                  ///< number of frames of the current animation file
    int canvas_width;               ///< width of the canvas
    int canvas_height;              ///< height of the canvas
    int prev_width;                 ///< width of the previous frame
    int prev_height;                ///< height of the previous frame
    int prev_anmf_flags;            ///< flags of the previous frame
    int prev_key_frame;             ///< flag if the previous frame was a key frame
} WebPDemuxContext;

/**
 * Major web browsers display WebPs at ~10-15fps when rate is not
 * explicitly set or have too low values. We assume default rate to be 10.
 * Default delay = 1000 microseconds / 10fps = 100 milliseconds per frame.
 */
#define WEBP_DEFAULT_DELAY   100
/**
 * By default delay values less than this threshold considered to be invalid.
 */
#define WEBP_MIN_DELAY       10

static int webp_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if (AV_RB32(b)     == MKBETAG('R', 'I', 'F', 'F') &&
        AV_RB32(b + 8) == MKBETAG('W', 'E', 'B', 'P'))
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int ensure_seekback(AVFormatContext *s, int64_t bytes)
{
    WebPDemuxContext *wdc = s->priv_data;
    AVIOContext      *pb  = s->pb;
    int ret;

    int64_t pos = avio_tell(pb);
    if (pos < 0)
        return pos;

    if (pos + bytes <= wdc->seekback_buffer_end)
        return 0;

    if ((ret = ffio_ensure_seekback(pb, bytes)) < 0)
        return ret;

    wdc->seekback_buffer_end = pos + bytes;
    return 0;
}

static int resync(AVFormatContext *s, int seek_to_start)
{
    WebPDemuxContext *wdc = s->priv_data;
    AVIOContext      *pb  = s->pb;
    int ret;
    int i;
    uint64_t state = 0;

    // ensure seek back for the file header and the first chunk header
    if ((ret = ensure_seekback(s, 12 + 8)) < 0)
        return ret;

    for (i = 0; i < 12; i++) {
        state = (state << 8) | avio_r8(pb);
        if (i == 11) {
            if ((uint32_t) state == MKBETAG('W', 'E', 'B', 'P'))
                break;
            i -= 4;
        }
        if (i == 7) {
            // ensure seek back for the rest of file header and the chunk header
            if ((ret = ensure_seekback(s, 4 + 8)) < 0)
                return ret;

            if ((state >> 32) != MKBETAG('R', 'I', 'F', 'F'))
                i--;
            else {
                uint32_t fsize = av_bswap32(state);
                if (!(fsize > 15 && fsize <= UINT32_MAX - 10))
                    i -= 4;
                else
                    wdc->remaining_size = fsize - 4;
            }
        }
        if (avio_feof(pb))
            return AVERROR_EOF;
    }

    wdc->file_start = avio_tell(pb) - 12;

    if (seek_to_start) {
        if ((ret = avio_seek(pb, -12, SEEK_CUR)) < 0)
            return ret;
        wdc->remaining_size += 12;
    }

    return 0;
}

static int is_key_frame(AVFormatContext *s, int has_alpha, int anmf_flags,
                        int width, int height)
{
    WebPDemuxContext *wdc = s->priv_data;

    if (wdc->nb_frames == 1)
        return 1;

    if (width  == wdc->canvas_width &&
        height == wdc->canvas_height &&
        (!has_alpha || (anmf_flags & ANMF_BLENDING_METHOD) == ANMF_BLENDING_METHOD_OVERWRITE))
        return 1;

    if ((wdc->prev_anmf_flags & ANMF_DISPOSAL_METHOD) == ANMF_DISPOSAL_METHOD_BACKGROUND &&
        (wdc->prev_key_frame || (wdc->prev_width  == wdc->canvas_width &&
                                 wdc->prev_height == wdc->canvas_height)))
        return 1;

    return 0;
}

static int webp_read_header(AVFormatContext *s)
{
    WebPDemuxContext *wdc = s->priv_data;
    AVIOContext      *pb  = s->pb;
    AVStream         *st;
    int ret, n;
    uint32_t chunk_type, chunk_size;
    int canvas_width  = 0;
    int canvas_height = 0;
    int width         = 0;
    int height        = 0;
    int is_frame      = 0;

    wdc->delay = wdc->default_delay;
    wdc->num_loop = 1;
    wdc->infinite_loop_start = -1;

    if ((ret = resync(s, 0)) < 0)
        return ret;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    while (!is_frame && wdc->remaining_size > 0 && !avio_feof(pb)) {
        chunk_type = avio_rl32(pb);
        chunk_size = avio_rl32(pb);
        if (chunk_size == UINT32_MAX)
            return AVERROR_INVALIDDATA;
        chunk_size += chunk_size & 1;
        if (avio_feof(pb))
            break;

        if (wdc->remaining_size < 8 + chunk_size)
            return AVERROR_INVALIDDATA;
        wdc->remaining_size -= 8 + chunk_size;

        // ensure seek back for the chunk body and the next chunk header
        if ((ret = ensure_seekback(s, chunk_size + 8)) < 0)
            return ret;

        switch (chunk_type) {
        case MKTAG('V', 'P', '8', 'X'):
            if (chunk_size >= 10) {
                avio_skip(pb, 4);
                canvas_width  = avio_rl24(pb) + 1;
                canvas_height = avio_rl24(pb) + 1;
                ret = avio_skip(pb, chunk_size - 10);
            } else
                ret = avio_skip(pb, chunk_size);
            break;
        case MKTAG('V', 'P', '8', ' '):
            if (chunk_size >= 10) {
                avio_skip(pb, 6);
                width  = avio_rl16(pb) & 0x3fff;
                height = avio_rl16(pb) & 0x3fff;
                is_frame = 1;
                ret = avio_skip(pb, chunk_size - 10);
            } else
                ret = avio_skip(pb, chunk_size);
            break;
        case MKTAG('V', 'P', '8', 'L'):
            if (chunk_size >= 5) {
                avio_skip(pb, 1);
                n = avio_rl32(pb);
                width  = (n & 0x3fff) + 1;          // first 14 bits
                height = ((n >> 14) & 0x3fff) + 1;  // next 14 bits
                is_frame = 1;
                ret = avio_skip(pb, chunk_size - 5);
            } else
                ret = avio_skip(pb, chunk_size);
            break;
        case MKTAG('A', 'N', 'M', 'F'):
            if (chunk_size >= 12) {
                avio_skip(pb, 6);
                width  = avio_rl24(pb) + 1;
                height = avio_rl24(pb) + 1;
                is_frame = 1;
                ret = avio_skip(pb, chunk_size - 12);
            } else
                ret = avio_skip(pb, chunk_size);
            break;
        default:
            ret = avio_skip(pb, chunk_size);
            break;
        }

        if (ret < 0)
            return ret;

        // fallback if VP8X chunk was not present
        if (!canvas_width && width > 0)
            canvas_width = width;
        if (!canvas_height && height > 0)
            canvas_height = height;
    }

    // WebP format operates with time in "milliseconds", therefore timebase is 1/100
    avpriv_set_pts_info(st, 64, 1, 1000);
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_WEBP;
    st->codecpar->codec_tag  = MKTAG('W', 'E', 'B', 'P');
    st->codecpar->width      = canvas_width;
    st->codecpar->height     = canvas_height;
    st->start_time           = 0;

    // jump to start because WebP decoder needs header data too
    if ((ret = avio_seek(pb, wdc->file_start, SEEK_SET)) < 0)
        return ret;
    wdc->remaining_size = 0;

    return 0;
}

static WebPHeaders *webp_headers_lower_or_equal(WebPHeaders *headers, size_t n,
                                                int64_t offset)
{
    size_t s, e;

    if (n == 0)
        return NULL;
    if (headers[0].offset > offset)
        return NULL;

    s = 0;
    e = n - 1;
    while (s < e) {
        size_t mid = (s + e + 1) / 2;
        if (headers[mid].offset == offset)
            return &headers[mid];
        else if (headers[mid].offset > offset)
            e = mid - 1;
        else
            s = mid;
    }

    return &headers[s];
}

static int append_chunk(WebPHeaders *headers, AVIOContext *pb,
                        uint32_t chunk_size)
{
    uint32_t previous_size = headers->size;
    uint8_t *new_data;

    if (headers->size > UINT32_MAX - chunk_size)
        return AVERROR_INVALIDDATA;

    new_data = av_realloc(headers->data, headers->size + chunk_size);
    if (!new_data)
        return AVERROR(ENOMEM);

    headers->data = new_data;
    headers->size += chunk_size;

    return avio_read(pb, headers->data + previous_size, chunk_size);
}

static int webp_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    WebPDemuxContext *wdc = s->priv_data;
    AVIOContext      *pb  = s->pb;
    int ret, n;
    int64_t packet_start = avio_tell(pb), packet_end;
    uint32_t chunk_type, chunk_size;
    int width = 0, height = 0;
    int is_frame = 0;
    int key_frame = 0;
    int anmf_flags = 0;
    int has_alpha = 0;
    int reading_headers = 0;
    int reset_key_frame = 0;
    WebPHeaders *headers = NULL;

    if (packet_start != wdc->prev_end_position) {
        // seek occurred, find the corresponding WebP headers
        headers = webp_headers_lower_or_equal(wdc->webp_headers, wdc->num_webp_headers,
                                              packet_start);
        if (!headers)
            return AVERROR_BUG;

        wdc->file_start     = headers->offset;
        wdc->remaining_size = headers->webp_size - (packet_start - headers->offset);
        wdc->canvas_width   = headers->canvas_width;
        wdc->canvas_height  = headers->canvas_height;
        wdc->num_loop       = headers->num_loop;
        wdc->cur_loop       = 0;
        reset_key_frame     = 1;
    }

    if (wdc->remaining_size == 0) {
        // if the loop count is finite, loop the current animation
        if (avio_tell(pb) != wdc->file_start &&
            !wdc->ignore_loop && wdc->num_loop > 1 && ++wdc->cur_loop < wdc->num_loop) {
            if ((ret = avio_seek(pb, wdc->file_start, SEEK_SET)) < 0)
                return ret;
            packet_start = avio_tell(pb);
        } else {
            // start of a new animation file
            wdc->delay = wdc->default_delay;
            if (wdc->num_loop)
                wdc->num_loop = 1;
        }

        // resync to the start of the next file
        ret = resync(s, 1);
        if (ret == AVERROR_EOF) {
            // we reached EOF, if the loop count is infinite, loop the whole input
            if (!wdc->ignore_loop && !wdc->num_loop) {
                if ((ret = avio_seek(pb, wdc->infinite_loop_start, SEEK_SET)) < 0)
                    return ret;
                ret = resync(s, 1);
            } else {
                wdc->prev_end_position = avio_tell(pb);
                return AVERROR_EOF;
            }
        }
        if (ret < 0)
            return ret;
        packet_start = avio_tell(pb);

        reset_key_frame = 1;
    }

    if (reset_key_frame) {
        // reset variables used for key frame detection
        wdc->nb_frames       = 0;
        wdc->canvas_width    = 0;
        wdc->canvas_height   = 0;
        wdc->prev_width      = 0;
        wdc->prev_height     = 0;
        wdc->prev_anmf_flags = 0;
        wdc->prev_key_frame  = 0;
    }

    if (packet_start == wdc->file_start) {
        headers = webp_headers_lower_or_equal(wdc->webp_headers, wdc->num_webp_headers,
                                              packet_start);
        if (!headers || headers->offset != wdc->file_start) {
            // grow the array of WebP files' headers
            wdc->num_webp_headers++;
            wdc->webp_headers = av_realloc_f(wdc->webp_headers,
                                             wdc->num_webp_headers,
                                             sizeof(WebPHeaders));
            if (!wdc->webp_headers)
                return AVERROR(ENOMEM);

            headers = &wdc->webp_headers[wdc->num_webp_headers - 1];
            memset(headers, 0, sizeof(*headers));
            headers->offset = wdc->file_start;
        } else {
            // headers for this WebP file have been already read, skip them
            if ((ret = avio_seek(pb, headers->size, SEEK_CUR)) < 0)
                return ret;
            packet_start = avio_tell(pb);

            wdc->remaining_size = headers->webp_size - headers->size;
            wdc->canvas_width   = headers->canvas_width;
            wdc->canvas_height  = headers->canvas_height;

            if (wdc->cur_loop >= wdc->num_loop)
                wdc->cur_loop = 0;
            wdc->num_loop = headers->num_loop;
        }
    }

    while (wdc->remaining_size > 0 && !avio_feof(pb)) {
        chunk_type = avio_rl32(pb);
        chunk_size = avio_rl32(pb);
        if (chunk_size == UINT32_MAX)
            return AVERROR_INVALIDDATA;
        chunk_size += chunk_size & 1;

        if (avio_feof(pb))
            break;

        // dive into RIFF chunk and do not ensure seek back for the whole file
        if (chunk_type == MKTAG('R', 'I', 'F', 'F') && chunk_size > 4)
            chunk_size = 4;

        // ensure seek back for the chunk body and the next chunk header
        if ((ret = ensure_seekback(s, chunk_size + 8)) < 0)
            return ret;

        switch (chunk_type) {
        case MKTAG('R', 'I', 'F', 'F'):
            if (avio_tell(pb) != wdc->file_start + 8) {
                // premature RIFF found, shorten the file size
                WebPHeaders *tmp = webp_headers_lower_or_equal(wdc->webp_headers,
                                                               wdc->num_webp_headers,
                                                               avio_tell(pb));
                tmp->webp_size -= wdc->remaining_size;
                wdc->remaining_size = 0;
                goto flush;
            }

            reading_headers = 1;
            if ((ret = avio_seek(pb, -8, SEEK_CUR)) < 0 ||
                (ret = append_chunk(headers, pb, 8 + chunk_size)) < 0)
                return ret;
            packet_start = avio_tell(pb);

            headers->offset = wdc->file_start;
            headers->webp_size = 8 + AV_RL32(headers->data + headers->size - chunk_size - 4);
            break;
        case MKTAG('V', 'P', '8', 'X'):
            reading_headers = 1;
            if ((ret = avio_seek(pb, -8, SEEK_CUR)) < 0 ||
                (ret = append_chunk(headers, pb, 8 + chunk_size)) < 0)
                return ret;
            packet_start = avio_tell(pb);

            if (chunk_size >= 10) {
                headers->canvas_width  = AV_RL24(headers->data + headers->size - chunk_size + 4) + 1;
                headers->canvas_height = AV_RL24(headers->data + headers->size - chunk_size + 7) + 1;
            }
            break;
        case MKTAG('A', 'N', 'I', 'M'):
            reading_headers = 1;
            if ((ret = avio_seek(pb, -8, SEEK_CUR)) < 0 ||
                (ret = append_chunk(headers, pb, 8 + chunk_size)) < 0)
                return ret;
            packet_start = avio_tell(pb);

            if (chunk_size >= 6) {
                headers->num_loop = AV_RL16(headers->data + headers->size - chunk_size + 4);
                wdc->num_loop = headers->num_loop;
                wdc->cur_loop = 0;
                if (!wdc->ignore_loop && wdc->num_loop != 1) {
                    // ensure seek back for the rest of the file
                    // and for the header of the next concatenated file
                    uint32_t loop_end = wdc->remaining_size - chunk_size + 12;
                    if ((ret = ensure_seekback(s, loop_end)) < 0)
                        return ret;

                    if (!wdc->num_loop && wdc->infinite_loop_start < 0)
                        wdc->infinite_loop_start = wdc->file_start;
                }
            }
            break;
        case MKTAG('V', 'P', '8', ' '):
            if (is_frame)
                // found a start of the next non-animated frame
                goto flush;
            is_frame = 1;

            reading_headers = 0;
            if (chunk_size >= 10) {
                avio_skip(pb, 6);
                width  = avio_rl16(pb) & 0x3fff;
                height = avio_rl16(pb) & 0x3fff;
                wdc->nb_frames++;
                ret = avio_skip(pb, chunk_size - 10);
            } else
                ret = avio_skip(pb, chunk_size);
            break;
        case MKTAG('V', 'P', '8', 'L'):
            if (is_frame)
                // found a start of the next non-animated frame
                goto flush;
            is_frame = 1;

            reading_headers = 0;
            if (chunk_size >= 5) {
                avio_skip(pb, 1);
                n = avio_rl32(pb);
                width     = (n & 0x3fff) + 1;           // first 14 bits
                height    = ((n >> 14) & 0x3fff) + 1;   ///next 14 bits
                has_alpha = (n >> 28) & 1;              // next 1 bit
                wdc->nb_frames++;
                ret = avio_skip(pb, chunk_size - 5);
            } else
                ret = avio_skip(pb, chunk_size);
            break;
        case MKTAG('A', 'N', 'M', 'F'):
            if (is_frame)
                // found a start of the next animated frame
                goto flush;

            reading_headers = 0;
            if (chunk_size >= 16) {
                avio_skip(pb, 6);
                width      = avio_rl24(pb) + 1;
                height     = avio_rl24(pb) + 1;
                wdc->delay = avio_rl24(pb);
                anmf_flags = avio_r8(pb);
                if (wdc->delay < wdc->min_delay)
                    wdc->delay = wdc->default_delay;
                wdc->delay = FFMIN(wdc->delay, wdc->max_delay);
                // dive into the chunk to set the has_alpha flag
                chunk_size = 16;
                ret = 0;
            } else
                ret = avio_skip(pb, chunk_size);
            break;
        case MKTAG('A', 'L', 'P', 'H'):
            reading_headers = 0;
            has_alpha = 1;
            ret = avio_skip(pb, chunk_size);
            break;
        default:
            if (reading_headers) {
                if ((ret = avio_seek(pb, -8, SEEK_CUR)) < 0 ||
                    (ret = append_chunk(headers, pb, 8 + chunk_size)) < 0)
                    return ret;
                packet_start = avio_tell(pb);
            } else
                ret = avio_skip(pb, chunk_size);
            break;
        }
        if (ret == AVERROR_EOF) {
            // EOF was reached but the position may still be in the middle
            // of the buffer. Seek to the end of the buffer so that EOF is
            // handled properly in the next invocation of webp_read_packet.
            if ((ret = avio_seek(pb, pb->buf_end - pb->buf_ptr, SEEK_CUR) < 0))
                return ret;
            wdc->prev_end_position = avio_tell(pb);
            wdc->remaining_size = 0;
            return AVERROR_EOF;
        }
        if (ret < 0)
            return ret;

        // fallback if VP8X chunk was not present
        if (headers) {
            if (!headers->canvas_width && width > 0)
                headers->canvas_width = width;
            if (!headers->canvas_height && height > 0)
                headers->canvas_height = height;
        }

        if (wdc->remaining_size < 8 + chunk_size)
            return AVERROR_INVALIDDATA;
        wdc->remaining_size -= 8 + chunk_size;

        packet_end = avio_tell(pb);
    }

    if (wdc->remaining_size > 0 && avio_feof(pb)) {
        // premature EOF, shorten the file size
        WebPHeaders *tmp = webp_headers_lower_or_equal(wdc->webp_headers,
                                                       wdc->num_webp_headers,
                                                       avio_tell(pb));
        tmp->webp_size -= wdc->remaining_size;
        wdc->remaining_size = 0;
    }

flush:
    if ((ret = avio_seek(pb, packet_start, SEEK_SET)) < 0)
        return ret;

    if ((ret = av_get_packet(pb, pkt, packet_end - packet_start)) < 0)
        return ret;

    wdc->prev_end_position = packet_end;

    if (headers && headers->data) {
        uint8_t *data = av_packet_new_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA,
                                                headers->size);
        if (!data)
            return AVERROR(ENOMEM);
        memcpy(data, headers->data, headers->size);

        s->streams[0]->internal->need_context_update = 1;
        s->streams[0]->codecpar->width  = headers->canvas_width;
        s->streams[0]->codecpar->height = headers->canvas_height;

        // copy the fields needed for the key frame detection
        wdc->canvas_width  = headers->canvas_width;
        wdc->canvas_height = headers->canvas_height;
    }

    key_frame = is_frame && is_key_frame(s, has_alpha, anmf_flags, width, height);
    if (key_frame)
        pkt->flags |= AV_PKT_FLAG_KEY;
    else
        pkt->flags &= ~AV_PKT_FLAG_KEY;

    wdc->prev_width      = width;
    wdc->prev_height     = height;
    wdc->prev_anmf_flags = anmf_flags;
    wdc->prev_key_frame  = key_frame;

    pkt->stream_index = 0;
    pkt->duration = is_frame ? wdc->delay : 0;
    pkt->pts = pkt->dts = AV_NOPTS_VALUE;

    if (is_frame && wdc->nb_frames == 1)
        s->streams[0]->r_frame_rate = (AVRational) {1000, pkt->duration};

    return ret;
}

static int webp_read_close(AVFormatContext *s)
{
    WebPDemuxContext *wdc = s->priv_data;

    for (size_t i = 0; i < wdc->num_webp_headers; ++i)
        av_freep(&wdc->webp_headers[i].data);
    av_freep(&wdc->webp_headers);
    wdc->num_webp_headers = 0;

    return 0;
}

#define OFFSET(x) offsetof(WebPDemuxContext, x)
static const AVOption options[] = {
    { "min_delay"     , "minimum valid delay between frames (in milliseconds)", OFFSET(min_delay)    , AV_OPT_TYPE_INT, {.i64 = WEBP_MIN_DELAY}    , 0, 1000 * 60, AV_OPT_FLAG_DECODING_PARAM },
    { "max_webp_delay", "maximum valid delay between frames (in milliseconds)", OFFSET(max_delay)    , AV_OPT_TYPE_INT, {.i64 = 0xffffff}          , 0, 0xffffff , AV_OPT_FLAG_DECODING_PARAM },
    { "default_delay" , "default delay between frames (in milliseconds)"      , OFFSET(default_delay), AV_OPT_TYPE_INT, {.i64 = WEBP_DEFAULT_DELAY}, 0, 1000 * 60, AV_OPT_FLAG_DECODING_PARAM },
    { "ignore_loop"   , "ignore loop setting"                                 , OFFSET(ignore_loop)  , AV_OPT_TYPE_BOOL,{.i64 = 1}                 , 0, 1        , AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass demuxer_class = {
    .class_name = "WebP demuxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEMUXER,
};

AVInputFormat ff_webp_demuxer = {
    .name           = "webp",
    .long_name      = NULL_IF_CONFIG_SMALL("WebP image"),
    .priv_data_size = sizeof(WebPDemuxContext),
    .read_probe     = webp_probe,
    .read_header    = webp_read_header,
    .read_packet    = webp_read_packet,
    .read_close     = webp_read_close,
    .flags          = AVFMT_GENERIC_INDEX,
    .priv_class     = &demuxer_class,
};