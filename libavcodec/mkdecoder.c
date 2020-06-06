/*
 * mk decoder code
 * Copyright (c) 2007-2008 Ian Caulfield
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
 
#include "avcodec.h"
 
static av_cold int ff_mkvideo_decode_init(AVCodecContext *avctx)
{
    av_log(avctx, AV_LOG_ERROR, "init mk decoder\n");
    return 0;
}
 
static int ff_mkvideo_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
    AVFrame   *frame   = (AVFrame*)data;
    AVPicture *picture = (AVPicture*)data;
    const uint8_t *buf             = avpkt->data;
    int buf_size                   = avpkt->size;

    int size = avpicture_get_size(avctx->pix_fmt, avctx->width,
        avctx->height);

    frame->pict_type        = AV_PICTURE_TYPE_I;
    frame->key_frame        = 1;

    frame->buf[0] = av_buffer_alloc(size);

    memcpy(frame->buf[0]->data, buf, buf_size);

    int res = 0;
    if ((res = avpicture_fill(picture, frame->buf[0]->data, avctx->pix_fmt,
        avctx->width, avctx->height)) < 0) 
    {
            av_buffer_unref(&frame->buf[0]);
            return res;
    }

    *got_frame = 1;
    return 0;
}
 
static av_cold int ff_mkvideo_decode_end(AVCodecContext *avctx)
{
    av_log(avctx, AV_LOG_ERROR, "uninit mk decoder\n");
    return 0;
}

AVCodec ff_mkvideo_decoder = {
    .name           = "mkvideo",
    .long_name      = NULL_IF_CONFIG_SMALL("mk video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MKVIDEO,
    //.priv_data_size = sizeof(MpegEncContext),
    .init           = ff_mkvideo_decode_init,
    .close          = ff_mkvideo_decode_end,
    .decode         = ff_mkvideo_decode_frame,
    //.flush          = ff_mpeg_flush,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P,
                                                    AV_PIX_FMT_NONE },
};