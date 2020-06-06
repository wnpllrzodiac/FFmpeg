/*
 * mk muxer
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
 
typedef struct MKContext {
    AVClass *av_class;
    int     reserved;
    
    int     video_strm_index;
    int     video_fmt;
    int     width;
    int     height;
    int     v_br;
    int     fps;
    
    int     audio_strm_index;
    int     audio_fmt;
    int     channels;
    int     sample_rate;
    int     a_br;
} MKContext;

static int mk_init(struct AVFormatContext *s)
{
    AVStream *stream    = NULL;
    MKContext *mk = s->priv_data;
    
    memset(mk, 0, sizeof(MKContext));
    
    mk->audio_strm_index    = -1;
    mk->video_strm_index    = -1;
    
    for (int i=0;i<s->nb_streams;i++) {
        stream = s->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            mk->video_strm_index    = i;
            mk->video_fmt           = stream->codecpar->format;
            mk->width               = stream->codecpar->width;
            mk->height              = stream->codecpar->height;
            mk->v_br                = stream->codecpar->bit_rate;
            mk->fps                 = 25;
        }
        else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            mk->audio_strm_index    = i;
            mk->audio_fmt           = stream->codecpar->format;
            mk->channels            = stream->codecpar->channels;
            mk->sample_rate         = stream->codecpar->sample_rate;
            mk->a_br                = stream->codecpar->bit_rate;
        }
    }
    
    if (mk->video_strm_index == -1 && mk->audio_strm_index == -1) {
        av_log(s, AV_LOG_ERROR, "both audio and video stream NOT found\n");
        return -1;
    }
    
    av_log(s, AV_LOG_ERROR, "video stream idx: %d, audio stream idx: %d\n", mk->video_strm_index, mk->audio_strm_index);
    av_log(s, AV_LOG_ERROR, "res: %d x %d, fps: %d, video br: %d kb\n", mk->width, mk->height, mk->fps, mk->v_br / 1000);
    av_log(s, AV_LOG_ERROR, "channels: %d, sample_rate %d, fmt %d, bitrate: %d kb\n", mk->channels, mk->sample_rate, mk->audio_fmt, mk->a_br / 1000);
    
    return 0;
}

static int mk_write_header(AVFormatContext *s)
{
    int av_flag         = 0;
    
    AVIOContext *pb = s->pb;
    MKContext *mk = s->priv_data;
    
    if (mk->video_strm_index != -1)
        av_flag |= 0x1;
    if (mk->audio_strm_index != -1)
        av_flag |= 0x2;
    
    avio_write(pb, "HELLO", 5);
    avio_w8(pb, 1);
    avio_w8(pb, 2);
    avio_w8(pb, 3);
    
    avio_w8(pb, av_flag);
    
    // 8 format
    // 16 width
    // 16 height
    // 16 video bitrate
    // 8 fps
    avio_w8(pb, mk->video_fmt);
    avio_wb16(pb, mk->width);
    avio_wb16(pb, mk->height);
    avio_wb16(pb, mk->v_br / 1000);
    avio_w8(pb, mk->fps);
    
    // 8 format
    // 8 channels
    // 16 sample_rate
    // 16 audio bitrate
    avio_w8(pb, mk->audio_fmt);
    avio_w8(pb, mk->channels);
    avio_wb16(pb, mk->sample_rate);
    avio_wb16(pb, mk->a_br / 1000);
    
    return 0;
}
 
static int mk_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    int type;
    int64_t pts, dts;
    MKContext *mk = s->priv_data;
    
    if (pkt->size >= 64) {
        for (int i=0;i<64;i++) {
            pkt->data[i] ^= 0x34; 
        }
    }
    
    if (pkt->stream_index == mk->video_strm_index)
        type = 1;
    else if (pkt->stream_index == mk->audio_strm_index)
        type = 2;
    else {
        av_log(s, AV_LOG_ERROR, "pkt stream id NOT found: %d\n", pkt->stream_index);
        return -1;
    }
    
    pts = av_rescale_q(pkt->pts, s->streams[pkt->stream_index]->time_base, av_make_q(1, 1000));
    dts = av_rescale_q(pkt->dts, s->streams[pkt->stream_index]->time_base, av_make_q(1, 1000));
    
    avio_w8(s->pb, type);
    avio_wb32(s->pb, pkt->size);
    avio_wb64(s->pb, pkt->pts);
    avio_wb64(s->pb, pkt->dts);
    avio_write(s->pb, pkt->data, pkt->size);
    
    //av_log(s, AV_LOG_ERROR, "write pkt: %d (%" PRId64" %" PRId64")\n", pkt->size, pts, dts);
    
    return 0;
}
 
static int mk_write_trailer(AVFormatContext *s)
{
    // 0-eof, 1-video, 2-audio
    avio_w8(s->pb, 0);
    
    return 0;
}

AVOutputFormat ff_mk_muxer = {
    .name           = "mk",
    .long_name      = NULL_IF_CONFIG_SMALL("mk (MK Video Container)"),
    .mime_type      = "mkvideo/x-msvideo",
    .extensions     = "mk",
    .priv_data_size = sizeof(MKContext),
    .audio_codec    = AV_CODEC_ID_AAC,
    .video_codec    = AV_CODEC_ID_H264,
    .init           = mk_init,
    .write_header   = mk_write_header,
    .write_packet   = mk_write_packet,
    .write_trailer  = mk_write_trailer,
    //.deinit         = mpegts_deinit,
    //.check_bitstream = mpegts_check_bitstream,
    .flags          = AVFMT_NOTIMESTAMPS,
    //.priv_class     = &mk_muxer_class,
};
