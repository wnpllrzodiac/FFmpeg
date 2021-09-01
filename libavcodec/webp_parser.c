/*
 * WebP parser
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
 * WebP parser
 */

#include "libavutil/bswap.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"

#include "parser.h"

typedef struct WebPParseContext {
    ParseContext pc;
    int frame;
    int first_frame;
    uint32_t fsize;
    uint32_t remaining_file_size;
    uint32_t remaining_tag_size;
} WebPParseContext;

static int webp_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                      const uint8_t **poutbuf, int *poutbuf_size,
                      const uint8_t *buf, int buf_size)
{
    WebPParseContext *ctx = s->priv_data;
    uint64_t state = ctx->pc.state64;
    int next = END_NOT_FOUND;
    int i, len;

    for (i = 0; i < buf_size;) {
        if (ctx->remaining_tag_size) {
            /* consuming tag */
            len = FFMIN(ctx->remaining_tag_size, buf_size - i);
            i += len;
            ctx->remaining_tag_size -= len;
            ctx->remaining_file_size -= len;
        } else {
            /* scan for the next tag or file */
            state = (state << 8) | buf[i];
            i++;

            if (!ctx->remaining_file_size) {
                /* scan for the next file */
                if (ctx->pc.frame_start_found == 4) {
                    ctx->pc.frame_start_found = 0;
                    if ((uint32_t) state == MKBETAG('W', 'E', 'B', 'P')) {
                        if (ctx->frame || i != 12) {
                            ctx->frame = 0;
                            next = i - 12;
                            state = 0;
                            ctx->pc.frame_start_found = 0;
                            break;
                        }
                        ctx->remaining_file_size = ctx->fsize - 4;
                        ctx->first_frame = 1;
                        continue;
                    }
                }
            if (ctx->pc.frame_start_found == 0) {
                    if ((state >> 32) == MKBETAG('R', 'I', 'F', 'F')) {
                        ctx->fsize = av_bswap32(state);
                        if (ctx->fsize > 15 && ctx->fsize <= UINT32_MAX - 10) {
                            ctx->fsize += (ctx->fsize & 1);
                            ctx->pc.frame_start_found = 1;
                        }
                    }
                } else
                    ctx->pc.frame_start_found++;
            } else {
                /* read the next tag */
                ctx->remaining_file_size--;
                if (ctx->remaining_file_size == 0) {
                    ctx->pc.frame_start_found = 0;
                    continue;
                }
                ctx->pc.frame_start_found++;
                if (ctx->pc.frame_start_found < 8)
                    continue;

                switch (state >> 32) {
                case MKBETAG('A', 'N', 'M', 'F'):
                case MKBETAG('V', 'P', '8', ' '):
                case MKBETAG('V', 'P', '8', 'L'):
                    if (ctx->frame) {
                        ctx->frame = 0;
                        next = i - 8;
                        state = 0;
                        ctx->pc.frame_start_found = 0;
                        goto flush;
                    }
                    ctx->frame = 1;
                    break;
                default:
                    break;
                }
                ctx->remaining_tag_size = av_bswap32(state);
                ctx->remaining_tag_size += ctx->remaining_tag_size & 1;
                if (ctx->remaining_tag_size > ctx->remaining_file_size) {
                    /* this is probably trash at the end of file */
                    ctx->remaining_tag_size = ctx->remaining_file_size;
                }
                ctx->pc.frame_start_found = 0;
                state = 0;
            }
        }
    }

flush:
    ctx->pc.state64 = state;

    if (ff_combine_frame(&ctx->pc, next, &buf, &buf_size) < 0) {
        *poutbuf      = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    // Extremely simplified key frame detection:
    // - the first frame (containing headers) is marked as a key frame
    // - other frames are marked as non-key frames
    if (ctx->first_frame) {
        ctx->first_frame = 0;
        s->pict_type = AV_PICTURE_TYPE_I;
        s->key_frame = 1;
    } else {
        s->pict_type = AV_PICTURE_TYPE_P;
        s->key_frame = 0;
    }

    *poutbuf      = buf;
    *poutbuf_size = buf_size;

    return next;
}

AVCodecParser ff_webp_parser = {
    .codec_ids      = { AV_CODEC_ID_WEBP },
    .priv_data_size = sizeof(WebPParseContext),
    .parser_parse   = webp_parse,
    .parser_close   = ff_parse_close,
};
