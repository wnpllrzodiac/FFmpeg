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
    MKVideoDemuxerContext *s = ctx->priv_data;
    AVStream *st;

    int width, height, bitrate, fps;
    int av_flags;
    int channels, sample_rate;
    int has_video, has_audio;
    
    if (avio_skip(ctx->pb, 8) < 0) {
        av_log(s, AV_LOG_ERROR, "failed to skip mark 8\n");
        return -1;
    }
    
    av_flags = avio_r8(ctx->pb);
    has_video = av_flags & 0x1;
    has_audio = (av_flags & 0x2) >> 1;
    av_log(s, AV_LOG_ERROR, "has video: %d, has audio: %d\n", has_video, has_audio);
    
    width = avio_rb16(ctx->pb);
    height = avio_rb16(ctx->pb);
    bitrate = avio_rb16(ctx->pb);
    fps = avio_rb16(ctx->pb);
    
    channels = avio_r8(ctx->pb);
    sample_rate = avio_rb16(ctx->pb);
    
    if (has_video) {
        av_log(s, AV_LOG_ERROR, "res: %d x %d, bitrate: %d kb, fps: %d\n", width, height, bitrate, fps);
    
        st = avformat_new_stream(ctx, NULL);
        if (!st)
            return AVERROR(ENOMEM);

        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_id = AV_CODEC_ID_H264;

        st->time_base.num = 1;
        st->time_base.den = 1000;
        
        st->codecpar->width  = width;
        st->codecpar->height = height;
        st->codecpar->format = (int)AV_PIX_FMT_YUV420P; // AVPixelFormat
        
        st->codecpar->bit_rate = bitrate * 1000;
        
        st->r_frame_rate = av_make_q(fps, 1);
        
        //st->need_parsing = AVSTREAM_PARSE_HEADERS;
    }
    if (has_audio) {
        av_log(s, AV_LOG_ERROR, "channels: %d, sample_rate %d\n", channels, sample_rate);
        
        st = avformat_new_stream(ctx, NULL);
        if (!st)
            return AVERROR(ENOMEM);

        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id = AV_CODEC_ID_AAC;
        st->codecpar->channels = channels;
        st->codecpar->sample_rate = sample_rate;
    }
    
    ctx->bit_rate = bitrate * 1000;
    ctx->duration = av_rescale_q(20000, av_make_q(1, 1000), AV_TIME_BASE_Q);
    
    return 0;
}
 
static int mkvideo_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int packet_size, ret, type;
    
    if (avio_feof(s->pb))
        return AVERROR_EOF;
    
    type = avio_r8(s->pb);
    if (type == 0) {
        return AVERROR_EOF;
    }
    
    packet_size = avio_rb32(s->pb);
    pkt->pts = avio_rb64(s->pb);
    pkt->dts = avio_rb64(s->pb);
    
    //av_log(s, AV_LOG_INFO, "packet size: %d, pts %" PRId64", dts %" PRId64"\n", packet_size, pkt->pts, pkt->dts);
    
    ret = av_get_packet(s->pb, pkt, packet_size);

    if (pkt->size >= 64) {
        for (int i=0;i<64;i++) {
            pkt->data[i] ^= 0x34; 
        }
    }
    
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