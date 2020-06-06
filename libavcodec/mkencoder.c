/*
 * mk encoder code
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
#include "libavutil/pixdesc.h"
 
static av_cold int mk_encode_init(AVCodecContext *avctx)
{
    av_log(avctx, AV_LOG_ERROR, "init mk encoder\n");
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);

    avctx->coded_frame = av_frame_alloc();
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
    avctx->bits_per_coded_sample = av_get_bits_per_pixel(desc);
    if(!avctx->codec_tag)
        avctx->codec_tag = avcodec_pix_fmt_to_codec_tag(avctx->pix_fmt);
    return 0;
}
 
static int mk_encode(AVCodecContext *avctx, AVPacket *pkt,
	const AVFrame *frame, int *got_packet)
{
    int ret = avpicture_get_size(avctx->pix_fmt, avctx->width, avctx->height);

    if (ret < 0)
        return ret;

    if (pkt->data == NULL && pkt->size == 0) {
        av_new_packet(pkt,ret);
        pkt->size = ret;
    }

    // 	if ((ret = ff_alloc_packet2(avctx, pkt, ret)) < 0)
    // 		return ret;

    if ((ret = avpicture_layout((const AVPicture *)frame, avctx->pix_fmt, avctx->width,
        avctx->height, pkt->data, pkt->size)) < 0)
        return ret;

    // 	if(avctx->codec_tag == AV_RL32("yuv2") && ret > 0 &&
    // 		avctx->pix_fmt   == AV_PIX_FMT_YUYV422) 
    // 	{
    // 			int x;
    // 			for(x = 1; x < avctx->height*avctx->width*2; x += 2)
    // 				pkt->data[x] ^= 0x80;
    // 	}
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}
 
static av_cold int mk_close(AVCodecContext *avctx)
{
    av_log(avctx, AV_LOG_ERROR, "close mk encoder\n");
    //av_frame_free(&avctx->coded_frame);
    return 0;
}

AVCodec ff_mkvideo_encoder = {
    .name           = "mkvideo",
    .long_name      = NULL_IF_CONFIG_SMALL("mk video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MKVIDEO,
    //.defaults       = opusenc_defaults,
    //.priv_class     = &opusenc_class,
    //.priv_data_size = sizeof(OpusEncContext),
    .init           = mk_encode_init,
    .encode2        = mk_encode,
    .close          = mk_close,
    .capabilities   = AV_CODEC_CAP_DELAY,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P,
                                                    AV_PIX_FMT_NONE },
};