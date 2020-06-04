/*
 * mk demuxer
 * Copyright (c) 2016 Paul B Mahol
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

#include "avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
 
typedef struct MKVideoDemuxerContext {
    const AVClass *pclass;     /**< Class for private options. */
    int width, height;        /**< Integers describing video size, set by a private option. */
    char *pixel_format;       /**< Set by a private option. */
    AVRational framerate;     /**< AVRational describing framerate, set by a private option. */
} MKVideoDemuxerContext;

static int mkvideo_probe(const AVProbeData *p)
{
    const uint8_t *d = p->buf;

    if (d[0] == 'H' &&
        d[1] == 'E' &&
        d[2] == 'L' &&
        d[3] == 'L' &&
        d[4] == 'O' &&
        d[5] == 1 &&
        d[6] == 2 && 
        d[7] == 3) {
        return AVPROBE_SCORE_MAX;
    }
    return 0;
}

static int mkvideo_read_header(AVFormatContext *ctx)
{
    int ret;
    MKVideoDemuxerContext *s = ctx->priv_data;
    enum AVPixelFormat pix_fmt;
    AVStream *st;

    char buf[64] = {0};
    int width, height, fps;
    ret = avio_read(ctx->pb, buf, 8);
    if (ret != 8) {
        av_log(s, AV_LOG_ERROR, "mark not enough\n");
        return -1;
    }
    
    avio_read(ctx->pb, &width, sizeof(width));
    avio_read(ctx->pb, &height, sizeof(height));
    avio_read(ctx->pb, &fps, sizeof(fps));
    
    av_log(s, AV_LOG_ERROR, "res: %d x %d, fps: %d\n", width, height, fps);
    
    st = avformat_new_stream(ctx, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;

    st->codec->codec_id = AV_CODEC_ID_H264;

    pix_fmt = AV_PIX_FMT_YUV420P;

    st->time_base.num = s->framerate.den;
    st->time_base.den = s->framerate.num;
    st->pts_wrap_bits = 64;


    st->codec->width  = s->width;
    st->codec->height = s->height;
    st->codec->pix_fmt = pix_fmt;

    AVRational tmpRa;
    tmpRa.den = 1;
    tmpRa.num = 8;
    st->codec->bit_rate = av_rescale_q(avpicture_get_size(st->codec->pix_fmt, s->width, s->height),
        tmpRa, st->time_base);
        
    return 0;
}
 
static int mkvideo_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int packet_size, ret, width, height;
    AVStream *st = s->streams[0];

    width = st->codecpar->width;
    height = st->codecpar->height;

    avio_read(s->pb, &packet_size, sizeof(packet_size));
    if (packet_size < 0)
        return -1;

    av_log(s, AV_LOG_INFO, "packet size: %d\n", packet_size);
    ret = av_get_packet(s->pb, pkt, packet_size);
    pkt->pts = pkt->dts = pkt->pos / packet_size;

    pkt->stream_index = 0;
    if (ret < 0)
        return ret;

    return 0;
}
 
#define OFFSET(x) offsetof(MKVideoDemuxerContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
 
static const AVOption mk_options[] = 
{
    { "video_size", "set frame size", OFFSET(width), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, DEC },
    { "pixel_format", "set pixel format", OFFSET(pixel_format), AV_OPT_TYPE_STRING, {.str = "yuv420p"}, 0, 0, DEC },
    { "framerate", "set frame rate", OFFSET(framerate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, 0, DEC },
    { NULL },
};
 
static const AVClass mk_demuxer_class = {
    .class_name = "mk video demuxer",
    .item_name  = av_default_item_name,
    .option     = mk_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_mk_demuxer = {
    .name           = "mk",
    .long_name      = NULL_IF_CONFIG_SMALL("MK Video Container"),
    .priv_data_size = sizeof(MKVideoDemuxerContext),
    .read_probe     = mkvideo_probe,
    .read_header    = mkvideo_read_header,
    .read_packet    = mkvideo_read_packet,
    //.read_seek      = flv_read_seek,
    //.read_close     = flv_read_close,
    .extensions     = "mk",
    .priv_class     = &mk_demuxer_class,
};