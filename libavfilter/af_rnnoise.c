/*
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

#include <rnnoise.h>

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "internal.h"

#define FRAME_SIZE 480

// ffmpeg -y -i out_no.wav -af rnnoise del.wav

// ffmpeg -i out_no.wav -f s16le -c:a pcm_s16le out_no.pcm
// ../rnnoise/examples/rnnoise_demo out_no.pcm out_fix.pcm
// ffmpeg -f s16le -i out_fix.pcm out_fix.wav

typedef struct RnnoiseContext {
    const AVClass *class;

    int64_t nb_samples_out;
    int64_t nb_samples_in;
    int64_t first_pts;
    int nb_samples;

    DenoiseState* sts[2];
    uint16_t *process_buf[2];
    int buf_offset[2];
    int tempo;
} RnnoiseContext;

#define OFFSET(x) offsetof(RnnoiseContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define AT AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption rnnoise_options[] = {
    { "tempo",      "set tempo scale factor", OFFSET(tempo), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.01, 100, AT },
    { NULL },
};

AVFILTER_DEFINE_CLASS(rnnoise);

static av_cold void uninit(AVFilterContext *ctx)
{
    RnnoiseContext *s = ctx->priv;

    for (int i=0;i<2;i++) {
        if (s->sts[i])
            rnnoise_destroy(s->sts[i]);
        if (s->process_buf[i])
            av_free(s->process_buf[i]);
    }
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_S16,
        AV_SAMPLE_FMT_NONE,
    };
    int ret;

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats(ctx, formats);
    if (ret < 0)
        return ret;

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_samplerates(ctx, formats);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    RnnoiseContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    int ret = 0, nb_samples;
    float x[FRAME_SIZE];

    if (s->first_pts == AV_NOPTS_VALUE)
        s->first_pts = in->pts;

    nb_samples = (s->buf_offset[0] / 2 + in->nb_samples) / FRAME_SIZE * FRAME_SIZE;

    memset(&x, 0, sizeof(x));

    out = ff_get_audio_buffer(outlink, nb_samples);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    out->pts = in->pts;
    out->nb_samples = nb_samples;

    for (int i=0;i<inlink->channels;i++) {
        int left = in->linesize[i];
        int in_offset = 0;
        int out_offset = 0;
        while ((s->buf_offset[i] + left) / 2 >= FRAME_SIZE) {
            int copy_size = FRAME_SIZE * 2 - s->buf_offset[i];
            memcpy(s->process_buf[i] + s->buf_offset[i], in->data[i] + in_offset, copy_size);
            for (int k=0;k<FRAME_SIZE;k++)
                x[k] = s->process_buf[i][k];
            rnnoise_process_frame(s->sts[i], x, x);
            for (int k=0;k<FRAME_SIZE;k++)
                s->process_buf[i][k] = x[k];
            memcpy(out->data[i] + out_offset, s->process_buf[i], FRAME_SIZE * 2);
            left -= copy_size;
            in_offset += copy_size;
            out_offset += FRAME_SIZE * 2;
            if (s->buf_offset[i] != 0)
                s->buf_offset[i] = 0;
        }

        if (left > 0)
            memcpy(s->process_buf[i], in->data[i] + in_offset, left);
        s->buf_offset[i] = left;
    }
    
    av_frame_free(&in);
    
    //return ret < 0 ? ret : nb_samples;
    return ff_filter_frame(outlink, out);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    RnnoiseContext *s = ctx->priv;

    //if (s->rbs)
    //    rubberband_delete(s->rbs);
    //s->rbs = rubberband_new(inlink->sample_rate, inlink->channels, opts, 1. / s->tempo, s->pitch);
    //if (!s->rbs)
    //    return AVERROR(ENOMEM);

    for (int i=0;i<2;i++) {
        s->sts[i] = NULL;
        s->process_buf[i] = NULL;
        s->buf_offset[i] = 0;
    }

    for (int i=0;i<inlink->channels;i++) {
        s->sts[i] = rnnoise_create(NULL);
        s->process_buf[i] = (uint16_t *)av_malloc(FRAME_SIZE * 2);
    }

    s->nb_samples = FRAME_SIZE;
    s->first_pts = AV_NOPTS_VALUE;

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    RnnoiseContext *s = ctx->priv;
    AVFrame *in = NULL;
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_samples(inlink, s->nb_samples, s->nb_samples, &in);
    if (ret < 0)
        return ret;
    if (ret > 0) {
        ret = filter_frame(inlink, in);
        if (ret != 0)
            return ret;
    }

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static const AVFilterPad rnnoise_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_input,
    },
    { NULL }
};

static const AVFilterPad rnnoise_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_rnnoise = {
    .name          = "rnnoise",
    .description   = NULL_IF_CONFIG_SMALL("Apply denoise."),
    .query_formats = query_formats,
    .priv_size     = sizeof(RnnoiseContext),
    .priv_class    = &rnnoise_class,
    .uninit        = uninit,
    .activate      = activate,
    .inputs        = rnnoise_inputs,
    .outputs       = rnnoise_outputs,
};
