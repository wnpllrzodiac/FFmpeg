#include "libavutil/opt.h"
#include "internal.h"
#include "framesync.h"
#include "glutil.h"
#ifdef USE_FREEIMAGE
#include "FreeImage.h"
#else
#include "lavfutils.h"
#include "libavutil/pixdesc.h"
#endif
#include <string.h>

// refer to https://github.com/transitive-bullshit/ffmpeg-gl-transition
// https://github.com/transitive-bullshit/ffmpeg-gl-transition/pull/60/files
// https://github.com/numberwolf/FFmpeg-Plus-OpenGL
// example:
// ./ffmpeg_g -ss 60 -i ~/work/media/astroboy.mp4 -i ~/work/media/TimeCode.mov 
//   -filter_complex "[0:v]scale=640:480[v0];[1:v]scale=640:480[v1];
//   [v0][v1]gltransition=duration=0.5:offset=5:source=crosswarp.glsl:uniforms='direction=vec2(0.0,1.0)'" 
//   -c:v libx264 -b:v 2000k -c:a copy -t 10 -y out.mp4

// jianying lightning
// ./ffmpeg -ss 60 -i /mnt/d/Archive/Media/3DM_WOMAN.mp4 -i /mnt/d/Archive/Media/TimeCode.mov 
// -filter_complex "[0:v]scale=640:480[v0];[1:v]scale=640:480[v1];
// [v0][v1]gltransition=duration=2:offset=5:
// source=/mnt/e/git/FFmpeg/libavfilter/oglfilter/jianying/lightning.glsl:
// extra_texture_count=60:extra_texture_path_fmt=libavfilter/oglfilter/res/lightning/clipname_%03d.png" 
// -c:v libx264 -b:v 2000k -c:a copy -t 10 -y out.mp4

// gltransition=duration=0.5:offset=3:source=wd.glsl:uniforms='amplitude=30.0&speed=30.0'"

// https://gl-transitions.com/gallery

// For any non-trivial concatenation, you'll likely want to make a filter chain comprised of split, 
// trim + setpts, and concat (with the v for video option) filters in addition to the gltransition filter itself. 
// If you want to concat audio streams in the same pass, you'll need to additionally make use of the asplit, 
// atrim + asetpts, and concat (with the a for audio option) filters.

#include <float.h> // for DBL_MAX

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#ifndef __ANDROID__
#include <GL/glew.h>
#endif
#endif

#ifdef GL_TRANSITION_USING_EGL
# include <EGL/egl.h>
# include <EGL/eglext.h>
#else
# include <GLFW/glfw3.h>
#endif

#define FROM (0)
#define TO (1)

#ifdef GL_TRANSITION_USING_EGL
#ifdef __ANDROID__
static const EGLint configAttribs[] = {
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,// very important!
    EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,//EGL_WINDOW_BIT EGL_PBUFFER_BIT we will create a pixelbuffer surface
    EGL_RED_SIZE,   8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE,  8,
    //EGL_ALPHA_SIZE, 8,// if you need the alpha channel
    EGL_DEPTH_SIZE, 8,// if you need the depth buffer
    EGL_STENCIL_SIZE,8,
    EGL_NONE
};
#else
static const EGLint configAttribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_BLUE_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_RED_SIZE, 8,
    EGL_DEPTH_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_NONE};
#endif
#endif

/*
2,3     5

0       1,4
*/
static const float position[12] = {
    -1.0f, -1.0f,
    1.0f, -1.0f,
    -1.0f, 1.0f,
    -1.0f, 1.0f,
    1.0f, -1.0f,
    1.0f, 1.0f};

static const GLchar *v_shader_source =
    //"#version 130\n"
    "attribute vec2 position;\n"
    "varying vec2 texCoord;\n"
    "void main(void) {\n"
    "  gl_Position = vec4(position, 0, 1);\n"
    "  vec2 _uv = position * 0.5 + 0.5;\n"
    "  texCoord = vec2(_uv.x, 1.0 - _uv.y);\n"
    "}\n";

static const GLchar *f_shader_template =
    "varying vec2 texCoord;\n"
    "uniform sampler2D from;\n"
    "uniform sampler2D to;\n"
    "uniform float progress;\n"
    "uniform float ratio;\n"
    "uniform float _fromR;\n"
    "uniform float _toR;\n"
    "uniform vec2 u_screenSize;\n"
    "\n"
    "vec4 getFromColor(vec2 uv) {\n"
    "  return texture2D(from, vec2(uv.x, 1.0 - uv.y));\n"
    "}\n"
    "\n"
    "vec4 getToColor(vec2 uv) {\n"
    "  return texture2D(to, vec2(uv.x, 1.0 - uv.y));\n"
    "}\n"
    "\n"
    "\n%s\n"
    "void main() {\n"
    "  gl_FragColor = transition(texCoord);\n"
    "}\n";

static const GLchar *f_default_transition_source =
    "vec4 transition (vec2 uv) {\n"
    "  return mix(\n"
    "    getFromColor(uv),\n"
    "    getToColor(uv),\n"
    "    progress\n"
    "  );\n"
    "}\n";

//#define PIXEL_FORMAT GL_RGB

typedef struct
{
    const AVClass *class;
    FFFrameSync fs;

    // input options
    double duration;
    double offset;
    char *source;
    char *uniforms;

    char *extra_texture_filepath;
    uint8_t *tex_data;
    int alpha;

    // for mask image seq
    int mask_pic_num;
    char *mask_pic_fmt;
    int mask_width;
    int mask_height;
    int mask_channels;
    int mask_pix_fmt;

    // decided by alpha
    int pix_fmt;
    int channel_num;

    // timestamp of the first frame in the output, in the timebase units
    int64_t first_pts;

    // uniforms
    GLuint from;
    GLuint to;
    GLuint extra_tex;
    GLint progress;
    GLint ratio;
    GLint _fromR;
    GLint _toR;

    GLuint program;
    GLuint pos_buf;

    int no_window;
    int print_shader_src;

    GLchar *f_shader_source;

#ifdef GL_TRANSITION_USING_EGL
    EGLDisplay      eglDpy;
    EGLConfig       eglCfg;
    EGLSurface      eglSurf;
    EGLContext      eglCtx;
#else
    GLFWwindow*     window;
#endif
} GlTransitionContext;

#define OFFSET(x) offsetof(GlTransitionContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM

static const AVOption gltransition_options[] = {
    {"nowindow", "ssh mode, no window init open gl context", OFFSET(no_window), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, .flags = FLAGS},
    {"duration", "transition duration in seconds", OFFSET(duration), AV_OPT_TYPE_DOUBLE, {.dbl = 1.0}, 0, DBL_MAX, FLAGS},
    {"offset", "delay before startingtransition in seconds", OFFSET(offset), AV_OPT_TYPE_DOUBLE, {.dbl = 0.0}, 0, DBL_MAX, FLAGS},
    {"source", "path to the gl-transition source file (defaults to basic fade)", OFFSET(source), AV_OPT_TYPE_STRING, {.str = NULL}, CHAR_MIN, CHAR_MAX, FLAGS},
    {"uniforms", "uniform vars setting, e.g. uniforms='some_var=1.0&other_var=1'", OFFSET(uniforms), AV_OPT_TYPE_STRING, {.str=NULL}, CHAR_MIN, CHAR_MAX, FLAGS },
    {"extra_texture_filepath", "path to the gl-transition extra_texture file", OFFSET(extra_texture_filepath), AV_OPT_TYPE_STRING, {.str = NULL}, CHAR_MIN, CHAR_MAX, FLAGS },
    {"extra_texture_count", "total texture file count", OFFSET(mask_pic_num), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 256, FLAGS},
    {"extra_texture_path_fmt", "texture file format, e.g.: image/%d.png", OFFSET(mask_pic_fmt), AV_OPT_TYPE_STRING, {.str = "0"}, 0, 0, FLAGS },
    {"alpha", "keep alpha channel", OFFSET(alpha), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, FLAGS},
    {"printshadercode", "whether to print shader code to debug", OFFSET(print_shader_src), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, .flags = FLAGS},
    {NULL}};

FRAMESYNC_DEFINE_CLASS(gltransition, GlTransitionContext, fs);

static GLuint build_shader(AVFilterContext *ctx, const GLchar *shader_source, GLenum type)
{
    GLuint shader = glCreateShader(type);
    if (!shader || !glIsShader(shader))
    {
        av_log(NULL, AV_LOG_ERROR, "failed to create shader: %d", shader);
        return 0;
    }
    glShaderSource(shader, 1, &shader_source, 0);
    glCompileShader(shader);

    GLint compileResult = GL_TRUE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileResult);
    if (compileResult == GL_FALSE)
    {
        char szLog[1024] = {0};
        GLsizei logLen = 0;
        glGetShaderInfoLog(shader, 1024, &logLen, szLog);
        av_log(NULL, AV_LOG_ERROR, "Compile Shader fail error log: %s \nshader code:\n%s\n",
               szLog, shader_source);
        glDeleteShader(shader);
        shader = 0;
    }

    return compileResult == GL_TRUE ? shader : 0;
}

static void vbo_setup(GlTransitionContext *gs)
{
    glGenBuffers(1, &gs->pos_buf);
    glBindBuffer(GL_ARRAY_BUFFER, gs->pos_buf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(position), position, GL_STATIC_DRAW);

    GLint loc = glGetAttribLocation(gs->program, "position");
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 0, 0);
}

static int tex_setup(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    GlTransitionContext *gs = ctx->priv;

    { // from
        glGenTextures(1, &gs->from);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gs->from);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        glTexImage2D(GL_TEXTURE_2D, 0, gs->pix_fmt, inlink->w, inlink->h, 0, gs->pix_fmt, GL_UNSIGNED_BYTE, NULL);

        glUniform1i(glGetUniformLocation(gs->program, "from"), 0);
    }

    { // to
        glGenTextures(1, &gs->to);
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, gs->to);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        glTexImage2D(GL_TEXTURE_2D, 0, gs->pix_fmt, inlink->w, inlink->h, 0, gs->pix_fmt, GL_UNSIGNED_BYTE, NULL);

        glUniform1i(glGetUniformLocation(gs->program, "to"), 1);
    }

    { // extra texture
        if (gs->extra_texture_filepath) { // extra_texture
            int channels;
            int pix_fmt;
#ifdef USE_FREEIMAGE
            int width, height;

            FIBITMAP *img = FreeImage_Load(FIF_PNG, gs->extra_texture_filepath, 0);
            if (!img) {
                av_log(NULL, AV_LOG_ERROR, "failed to open image file: %s\n", gs->extra_texture_filepath);
                return -1;
            }

            width = FreeImage_GetWidth(img);
            height = FreeImage_GetHeight(img);
            int bpp = FreeImage_GetBPP(img);
            channels = bpp / 8;
            switch (channels) {
            case 3:
                pix_fmt = GL_RGB;
                break;
            case 4:
                pix_fmt = GL_RGBA;
                break;
            case 1:
                pix_fmt = GL_RED;
                break;
            default:
                return AVERROR(EINVAL);
            }

            //av_log(NULL, AV_LOG_DEBUG, "img res: %d x %d, bpp %d\n", w, h, bpp);
            if (width <= 0 || height <= 0 || bpp <= 0) {
                av_log(NULL, AV_LOG_ERROR, "failed to get image file info: %s\n", gs->extra_texture_filepath);
                return -1;
            }

            if (!gs->tex_data)
                gs->tex_data = av_mallocz(width * height * channels);

            BYTE *data = FreeImage_GetBits(img);
            if (!data) {
                av_log(NULL, AV_LOG_ERROR, "failed to get image data: %s\n", gs->extra_texture_filepath);
                return -1;
            }

            memcpy(gs->tex_data, data, width * height * channels);
            FreeImage_Unload(img);
#else
            int ret;
            AVFrame *tex_frame = av_frame_alloc();
            if (!tex_frame)
                return AVERROR(ENOMEM);

            if ((ret = ff_load_image(tex_frame->data, tex_frame->linesize,
                                    &tex_frame->width, &tex_frame->height,
                                    &tex_frame->format, gs->extra_texture_filepath, gs)) < 0)
            {
                av_log(ctx, AV_LOG_ERROR, "failed to load texture file: %s\n", gs->extra_texture_filepath);
                return ret;
            }
                

            if (tex_frame->format != AV_PIX_FMT_RGB24 && tex_frame->format != AV_PIX_FMT_RGBA && 
                tex_frame->format != AV_PIX_FMT_GRAY8) {
                av_log(ctx, AV_LOG_ERROR, "texture image is not a rgb image: %d(%s)\n", 
                    tex_frame->format, av_get_pix_fmt_name(tex_frame->format));
                return AVERROR(EINVAL);
            }

            int width = tex_frame->width;
            int height = tex_frame->height;
            switch (tex_frame->format) {
            case AV_PIX_FMT_RGB24:
                channels = 3;
                pix_fmt = GL_RGB;
                break;
            case AV_PIX_FMT_RGBA:
                channels = 4;
                pix_fmt = GL_RGBA;
                break;
            case AV_PIX_FMT_GRAY8:
                channels = 1;
                pix_fmt = GL_RED;
                break;
            default:
                av_log(ctx, AV_LOG_ERROR, "unsupported pix format: %d(%s)\n", 
                    tex_frame->format, av_get_pix_fmt_name(tex_frame->format));
                return AVERROR(EINVAL);
            }
            int frame_data_size = width * height * channels;
            if (!gs->tex_data)
                gs->tex_data = av_mallocz(frame_data_size);
            
            if (tex_frame->linesize[0] == width * channels) {
                // bunch copy
                memcpy(gs->tex_data, tex_frame->data[0], tex_frame->linesize[0] * height);
            }
            else {
                // line copy
                int data_offset = 0;
                int frame_offset = 0;
                for (int line=0;line<height;line++) { 
                    memcpy(gs->tex_data, tex_frame->data[0] + frame_offset, width * channels);
                    data_offset += width * channels;
                    frame_offset += tex_frame->linesize[0];
                }
            }

            av_frame_free(&tex_frame);
#endif
            glGenTextures(1, &gs->extra_tex);
            glActiveTexture(GL_TEXTURE0 + 2);
            glBindTexture(GL_TEXTURE_2D, gs->extra_tex);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

            glTexImage2D(GL_TEXTURE_2D, 0, pix_fmt, width, height, 0, pix_fmt, GL_UNSIGNED_BYTE, gs->tex_data);

            glUniform1i(glGetUniformLocation(gs->program, "extra_tex"), 2);
        }

        if (gs->mask_pic_fmt && gs->mask_pic_num > 0) { // extra_texture
            for (int i=0 ; i < gs->mask_pic_num ; i++) {
                char filename[256] = {0};
                sprintf(filename, gs->mask_pic_fmt, i);
                int ret;
                AVFrame *tex_frame = av_frame_alloc();
                if (!tex_frame)
                    return AVERROR(ENOMEM);

                if ((ret = ff_load_image(tex_frame->data, tex_frame->linesize,
                                        &tex_frame->width, &tex_frame->height,
                                        &tex_frame->format, filename, gs)) < 0)
                {
                    av_log(ctx, AV_LOG_ERROR, "failed to load texture file: %s\n", filename);
                    return ret;
                } 

                if (tex_frame->format != AV_PIX_FMT_RGB24 && tex_frame->format != AV_PIX_FMT_RGBA && 
                        tex_frame->format != AV_PIX_FMT_GRAY8) {
                    av_log(ctx, AV_LOG_ERROR, "texture image is not a rgb image: %d(%s)\n", 
                        tex_frame->format, av_get_pix_fmt_name(tex_frame->format));
                    return AVERROR(EINVAL);
                }

                gs->mask_width = tex_frame->width;
                gs->mask_height = tex_frame->height;
                switch (tex_frame->format) {
                case AV_PIX_FMT_RGB24:
                    gs->mask_channels = 3;
                    gs->mask_pix_fmt = GL_RGB;
                    break;
                case AV_PIX_FMT_RGBA:
                    gs->mask_channels = 4;
                    gs->mask_pix_fmt = GL_RGBA;
                    break;
                case AV_PIX_FMT_GRAY8:
                    gs->mask_channels = 1;
                    gs->mask_pix_fmt = GL_RED;
                    break;
                default:
                    av_log(ctx, AV_LOG_ERROR, "unsupported pix format: %d(%s)\n", 
                        tex_frame->format, av_get_pix_fmt_name(tex_frame->format));
                    return AVERROR(EINVAL);
                }
                // linesize[0] > width * channels, e.g. 2016 vs 500 * 4
                int frame_data_size = tex_frame->width * tex_frame->height * gs->mask_channels;
                if (!gs->tex_data)
                    gs->tex_data = av_mallocz(frame_data_size * gs->mask_pic_num);
                
                if (tex_frame->linesize[0] == tex_frame->width * gs->mask_channels) {
                    // bunch copy
                    memcpy(gs->tex_data + frame_data_size * i, tex_frame->data[0], tex_frame->linesize[0] * tex_frame->height);
                }
                else {
                    // line copy
                    int data_offset = 0;
                    int frame_offset = 0;
                    for (int line=0;line<tex_frame->height;line++) { 
                        memcpy(gs->tex_data + frame_data_size * i + data_offset, 
                            tex_frame->data[0] + frame_offset, 
                            tex_frame->width * gs->mask_channels);
                        data_offset += tex_frame->width * gs->mask_channels;
                        frame_offset += tex_frame->linesize[0];
                    }
                }
                
                av_frame_free(&tex_frame);
            }

            glGenTextures(1, &gs->extra_tex);
            glActiveTexture(GL_TEXTURE0 + 2);

            glBindTexture(GL_TEXTURE_2D, gs->extra_tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

            glTexImage2D(GL_TEXTURE_2D, 0, gs->mask_pix_fmt, gs->mask_width, gs->mask_height, 
                0, gs->mask_pix_fmt, GL_UNSIGNED_BYTE, NULL);

            glUniform1i(glGetUniformLocation(gs->program, "extra_tex"), 2);

            // set other uniforms
            float alphaFactor = 1.0f;
            glUniform1i(glGetUniformLocation(gs->program, "baseTexWidth"), gs->mask_width);
            glUniform1i(glGetUniformLocation(gs->program, "baseTexHeight"), gs->mask_height);
            glUniform2f(glGetUniformLocation(gs->program, "fullBlendTexSize"), 
                inlink->w, inlink->h);
            glUniform1f(glGetUniformLocation(gs->program, "alphaFactor"), alphaFactor);
            av_log(ctx, AV_LOG_INFO, "set uniforms: tex %d x %d -> out %d x %d, alpha %.2f\n",
                gs->mask_width, gs->mask_height, inlink->w, inlink->h, alphaFactor);
        }
    }

    return 0;
}

static int build_program(AVFilterContext *ctx)
{
    GLuint v_shader, f_shader;
    GlTransitionContext *gs = ctx->priv;

    if (!(v_shader = build_shader(ctx, v_shader_source, GL_VERTEX_SHADER))) {
        av_log(ctx, AV_LOG_ERROR, "invalid vertex shader\n");
        return -1;
    }

    char *source = NULL;
    if (gs->source) {
        FILE *f = fopen(gs->source, "rb");

        if (!f)
        {
            av_log(ctx, AV_LOG_ERROR, "transition source file NOT found: \"%s\"\n", gs->source);
            return -1;
        }

        fseek(f, 0, SEEK_END);
        unsigned long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        source = malloc(fsize + 1);
        fread(source, fsize, 1, f);
        fclose(f);

        source[fsize] = 0;
    }

    const char *transition_source = source ? source : f_default_transition_source;

    int len = strlen(f_shader_template) + strlen(transition_source);
    gs->f_shader_source = av_calloc(len, sizeof(*gs->f_shader_source));
    if (!gs->f_shader_source) {
        return AVERROR(ENOMEM);
    }

    snprintf(gs->f_shader_source, len * sizeof(*gs->f_shader_source), f_shader_template, transition_source);
    av_log(ctx, gs->print_shader_src ? AV_LOG_INFO : AV_LOG_DEBUG, "shader source:\n%s\n%s\n", gs->source, gs->f_shader_source);

    if (source) {
        free(source);
        source = NULL;
    }

    if (!(f_shader = build_shader(ctx, gs->f_shader_source, GL_FRAGMENT_SHADER)))
    {
        av_log(ctx, AV_LOG_ERROR, "invalid fragment shader\n");
        return -1;
    }

    gs->program = glCreateProgram();
    glAttachShader(gs->program, v_shader);
    glAttachShader(gs->program, f_shader);
    glLinkProgram(gs->program);

    GLint status;
    glGetProgramiv(gs->program, GL_LINK_STATUS, &status);
    return status == GL_TRUE ? 0 : -1;
}

static AVFrame *apply_transition(FFFrameSync *fs,
                                 AVFilterContext *ctx,
                                 AVFrame *fromFrame,
                                 const AVFrame *toFrame)
{
    GlTransitionContext *c = ctx->priv;
    AVFilterLink *fromLink = ctx->inputs[FROM];
    AVFilterLink *toLink = ctx->inputs[TO];
    AVFilterLink *outLink = ctx->outputs[0];
    AVFrame *outFrame;

    outFrame = ff_get_video_buffer(outLink, outLink->w, outLink->h);
    if (!outFrame)
    {
        return NULL;
    }

    av_frame_copy_props(outFrame, fromFrame);

#ifdef GL_TRANSITION_USING_EGL
    eglMakeCurrent(c->eglDpy, c->eglSurf, c->eglSurf, c->eglCtx);
#else
    glfwMakeContextCurrent(c->window);
#endif

    glUseProgram(c->program);

    const float ts = ((fs->pts - c->first_pts) / (float)fs->time_base.den) - c->offset;
    const float progress = FFMAX(0.0f, FFMIN(1.0f, ts / c->duration));
    // av_log(ctx, AV_LOG_ERROR, "transition '%s' %llu %f %f\n", c->source, fs->pts - c->first_pts, ts, progress);
    glUniform1f(c->progress, progress);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, c->from);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, fromFrame->linesize[0] / c->channel_num);
    glTexImage2D(GL_TEXTURE_2D, 0, c->pix_fmt, fromLink->w, fromLink->h, 0, c->pix_fmt, GL_UNSIGNED_BYTE, fromFrame->data[0]);

    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, c->to);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, toFrame->linesize[0] / c->channel_num);
    glTexImage2D(GL_TEXTURE_2D, 0, c->pix_fmt, toLink->w, toLink->h, 0, c->pix_fmt, GL_UNSIGNED_BYTE, toFrame->data[0]);

    if (c->mask_pic_fmt && c->mask_pic_num > 0) {
        glActiveTexture(GL_TEXTURE0 + 2);
        glBindTexture(GL_TEXTURE_2D, c->extra_tex);
        int idx = (int)((float)c->mask_pic_num * progress / 1.0f);
        if (idx > c->mask_pic_num - 1)
            idx = c->mask_pic_num - 1;
        int offset = c->mask_width * c->mask_height * c->mask_channels * idx;
        glPixelStorei(GL_UNPACK_ROW_LENGTH, c->mask_width);
        glTexImage2D(GL_TEXTURE_2D, 0, c->mask_pix_fmt, c->mask_width, c->mask_height, 
            0, c->mask_pix_fmt, GL_UNSIGNED_BYTE, c->tex_data + offset);
    }

    glDrawArrays(GL_TRIANGLES, 0, 6);
    glPixelStorei(GL_PACK_ROW_LENGTH, outFrame->linesize[0] / c->channel_num);
    glReadPixels(0, 0, outLink->w, outLink->h, c->pix_fmt, GL_UNSIGNED_BYTE, (GLvoid *)outFrame->data[0]);

    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    av_frame_free(&fromFrame);

    return outFrame;
}

static int blend_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    GlTransitionContext *c = ctx->priv;

    AVFrame *fromFrame, *toFrame, *outFrame;
    int ret;

    ret = ff_framesync_dualinput_get(fs, &fromFrame, &toFrame);
    if (ret < 0)
    {
        return ret;
    }

    if (c->first_pts == AV_NOPTS_VALUE && fromFrame && fromFrame->pts != AV_NOPTS_VALUE)
    {
        c->first_pts = fromFrame->pts;
    }

    if (!toFrame)
    {
        return ff_filter_frame(ctx->outputs[0], fromFrame);
    }

    outFrame = apply_transition(fs, ctx, fromFrame, toFrame);
    if (!outFrame)
    {
        return AVERROR(ENOMEM);
    }

    return ff_filter_frame(ctx->outputs[0], outFrame);
}

static av_cold int init(AVFilterContext *ctx)
{
    GlTransitionContext *gs = ctx->priv;
    gs->fs.on_event = blend_frame;
    gs->first_pts = AV_NOPTS_VALUE;

#ifndef GL_TRANSITION_USING_EGL
    if (gs->no_window) {
        av_log(NULL, AV_LOG_ERROR, "open gl no window init ON\n");
        no_window_init();
    }

    return glfwInit() ? 0 : -1;
#endif

    return 0;
}

static void setup_uniforms(AVFilterLink *fromLink)
{
    AVFilterContext *ctx = fromLink->dst;
    GlTransitionContext *gs = ctx->priv;

    gs->progress = glGetUniformLocation(gs->program, "progress");
    glUniform1f(gs->progress, 0.0f);

    glUniform2f(glGetUniformLocation(gs->program, "u_screenSize"), 
        (float)fromLink->w, (float)fromLink->h);

    // TODO: this should be output ratio
    gs->ratio = glGetUniformLocation(gs->program, "ratio");
    glUniform1f(gs->ratio, fromLink->w / (float)fromLink->h);

    gs->_fromR = glGetUniformLocation(gs->program, "_fromR");
    glUniform1f(gs->_fromR, fromLink->w / (float)fromLink->h);

    // TODO: initialize this in config_props for "to" input
    gs->_toR = glGetUniformLocation(gs->program, "_toR");
    glUniform1f(gs->_toR, fromLink->w / (float)fromLink->h);

    if(gs->uniforms) {
        StringArray_t sa = parseQueryString(gs->uniforms);
        if (sa.len > 0) {
            for (int i = 0;i<sa.len;i+=2) {
                GLint location = glGetUniformLocation(gs->program, sa.strings[i]);
                if (location >= 0) {
                    int intVar;
                    float floatVar;
                    int vecShape = 0;
                    int intVec[4] = {0};
                    float floatVec[4] = {0.0};
                    if (parseGlSLFloatVector(sa.strings[i + 1], floatVec, &vecShape)) {
                        switch (vecShape) {
                        case 2:
                            glUniform2f(location, floatVec[0], floatVec[1]);
                            av_log(ctx, AV_LOG_INFO, "glUniform2f(%s, %.3f, %.3f)\n", 
                                sa.strings[i], floatVec[0], floatVec[1]);
                            break;
                        case 3:
                            glUniform3f(location, floatVec[0], floatVec[1], floatVec[2]);
                            av_log(ctx, AV_LOG_INFO, "glUniform3f(%s, %.3f, %.3f, %.3f)\n", 
                                sa.strings[i], floatVec[0], floatVec[1], floatVec[2]);
                            break;
                        case 4:
                            glUniform4f(location, floatVec[0], floatVec[1], floatVec[2], floatVec[3]);
                            av_log(ctx, AV_LOG_INFO, "glUniform4f(%s, %.3f, %.3f, %.3f, %.3f)\n", 
                                sa.strings[i], floatVec[0], floatVec[1], floatVec[2], floatVec[3]);
                            break;
                        default:
                            break;
                        }
                    } else if (parseGlSLIntVector(sa.strings[i + 1], intVec, &vecShape)) {
                        switch (vecShape) {
                        case 2:
                            glUniform2i(location, intVec[0], intVec[1]);
                            av_log(ctx, AV_LOG_INFO, "glUniform2i(%s, %d, %d)\n", sa.strings[i], intVec[0], intVec[1]);
                            break;
                        case 3:
                            glUniform3i(location, intVec[0], intVec[1], intVec[2]);
                            av_log(ctx, AV_LOG_INFO, "glUniform3i(%s, %d, %d, %d)\n", sa.strings[i], intVec[0], intVec[1], intVec[2]);
                            break;
                        case 4:
                            glUniform4i(location, intVec[0], intVec[1], intVec[2], intVec[3]);
                            av_log(ctx, AV_LOG_INFO, "glUniform4i(%s, %d, %d, %d, %d)\n", 
                                sa.strings[i], intVec[0], intVec[1], intVec[2], intVec[3]);
                        default:
                            break;
                        }
                    } else if (strToInt(sa.strings[i + 1], &intVar)) {
                        glUniform1i(location, intVar);
                        av_log(ctx, AV_LOG_INFO, "glUniform1i(%s, %d)\n", sa.strings[i], intVar);
                    } else if (strToFloat(sa.strings[i + 1], &floatVar)) {
                        glUniform1f(location, floatVar);
                        av_log(ctx, AV_LOG_INFO, "glUniform1f(%s, %.3f)\n", sa.strings[i], floatVar);

                    } else {
                        av_log(ctx, AV_LOG_ERROR, "value %s not supported, supported: int float ivec vec)", sa.strings[i + 1]);
                    }
                } else {
                av_log(ctx, AV_LOG_ERROR, "can't get location of uniform: %s, fail set value: %s\n", sa.strings[i],
                        sa.strings[i + 1]);
                }
            } // for

            //free strings;
            for (int i = 0; i < sa.len; i++) {
                free(sa.strings[i]);
            }
        }
    }
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    GlTransitionContext *gs = ctx->priv;

#ifdef GL_TRANSITION_USING_EGL
    //init EGL
    // 1. Initialize EGL
    // c->eglDpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);

#ifdef __ANDROID__
    gs->eglDpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (gs->eglDpy == EGL_NO_DISPLAY) {
        av_log(NULL, AV_LOG_ERROR, "eglGetDisplay error\n");
        return -1;
    }
#else
#define MAX_DEVICES 4
    EGLDeviceEXT eglDevs[MAX_DEVICES];
    EGLint numDevices;

    PFNEGLQUERYDEVICESEXTPROC eglQueryDevicesEXT =(PFNEGLQUERYDEVICESEXTPROC)
    eglGetProcAddress("eglQueryDevicesEXT");

    eglQueryDevicesEXT(MAX_DEVICES, eglDevs, &numDevices);

    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =  (PFNEGLGETPLATFORMDISPLAYEXTPROC)
    eglGetProcAddress("eglGetPlatformDisplayEXT");

    gs->eglDpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, eglDevs[0], 0);
#endif

    EGLint major, minor;
    eglInitialize(gs->eglDpy, &major, &minor);
    av_log(ctx, AV_LOG_DEBUG, "%d.%d", major, minor);
    
    // 2. Select an appropriate configuration
    EGLint numConfigs;
    EGLint pbufferAttribs[] = {
        EGL_WIDTH,
        inlink->w,
        EGL_HEIGHT,
        inlink->h,
        EGL_NONE,
    };
    //3.1 根据所需的参数获取符合该参数的config_size，主要是解决有些手机eglChooseConfig失败的兼容性问题
    if (!eglChooseConfig(gs->eglDpy, configAttribs, &gs->eglCfg, 1, &numConfigs)) {
        av_log(NULL, AV_LOG_ERROR, "eglChooseConfig error\n");
        return -1;
    }
#ifdef __ANDROID__
    //3.2 根据获取到的config_size得到eglConfig
    av_log(NULL, AV_LOG_ERROR, "numConfigs: %d\n", numConfigs);
    if (!eglChooseConfig(gs->eglDpy, configAttribs, &gs->eglCfg, numConfigs, &numConfigs)) {
        av_log(NULL, AV_LOG_ERROR, "eglChooseConfig error\n");
        return -1;
    }
#endif
    // 3. Create a surface
    gs->eglSurf = eglCreatePbufferSurface(gs->eglDpy, gs->eglCfg, pbufferAttribs);
    if(gs->eglSurf == EGL_NO_SURFACE) {
        switch(eglGetError())
        {
            case EGL_BAD_ALLOC:
                // Not enough resources available. Handle and recover
                av_log(NULL, AV_LOG_ERROR, "Not enough resources available\n");
                break;
            case EGL_BAD_CONFIG:
                // Verify that provided EGLConfig is valid
                av_log(NULL, AV_LOG_ERROR, "provided EGLConfig is invalid\n");
                break;
            case EGL_BAD_PARAMETER:
                // Verify that the EGL_WIDTH and EGL_HEIGHT are
                // non-negative values
                av_log(NULL, AV_LOG_ERROR, "provided EGL_WIDTH and EGL_HEIGHT is invalid\n");
                break;
            case EGL_BAD_MATCH:
                // Check window and EGLConfig attributes to determine
                // compatibility and pbuffer-texture parameters
                av_log(NULL, AV_LOG_ERROR, "Check window and EGLConfig attributes\n");
                break;
        }

        return -1;
    }
    // 4. Bind the API
    eglBindAPI(EGL_OPENGL_API);
    // 5. Create a context and make it current
#ifdef __ANDROID__
    const EGLint attrib_ctx_list[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
    };
    gs->eglCtx = eglCreateContext(gs->eglDpy, gs->eglCfg, NULL, attrib_ctx_list);
#else
    gs->eglCtx = eglCreateContext(gs->eglDpy, gs->eglCfg, EGL_NO_CONTEXT, NULL);
#endif
    if (gs->eglCtx == EGL_NO_CONTEXT) {
        av_log(NULL, AV_LOG_ERROR, "eglCreateContext  error\n");
        return -1;
    }
    if (!eglMakeCurrent(gs->eglDpy, gs->eglSurf, gs->eglSurf, gs->eglCtx)) {
        av_log(NULL, AV_LOG_ERROR, "eglMakeCurrent  error\n");
        return -1;
    }
#else
    //glfw
    glfwWindowHint(GLFW_VISIBLE, 0);
    gs->window = glfwCreateWindow(inlink->w, inlink->h, "", NULL, NULL);

    glfwMakeContextCurrent(gs->window);
#endif

#if !defined(__APPLE__) && !defined(__ANDROID__)
    glewExperimental = GL_TRUE;
    glewInit();
#endif

    glViewport(0, 0, inlink->w, inlink->h);

    int ret;
    if ((ret = build_program(ctx)) < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "failed to build ogl program: %d\n", ret);
        return ret;
    }

    glUseProgram(gs->program);
    vbo_setup(gs);
    setup_uniforms(inlink);
    return tex_setup(inlink);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    GlTransitionContext *gs = ctx->priv;
    ff_framesync_uninit(&gs->fs);

#ifdef GL_TRANSITION_USING_EGL
    if (gs->eglDpy) {
        glDeleteTextures(1, &gs->from);
        glDeleteTextures(1, &gs->to);
        if (gs->extra_texture_filepath || gs->mask_pic_fmt)
            glDeleteTextures(1, &gs->extra_tex);
        glDeleteProgram(gs->program);
        glDeleteBuffers(1, &gs->pos_buf);
        eglTerminate(gs->eglDpy);
    }
#else
    if (gs->window) {
        glDeleteTextures(1, &gs->from);
        glDeleteTextures(1, &gs->to);
        if (gs->extra_texture_filepath)
            glDeleteTextures(1, &gs->extra_tex);
        glDeleteProgram(gs->program);
        glDeleteBuffers(1, &gs->pos_buf);
        glfwDestroyWindow(gs->window);
    }
#endif

    if (gs->tex_data) {
        av_free(gs->tex_data);
        gs->tex_data = NULL;
    }
}

static int query_formats(AVFilterContext *ctx)
{
    //static const enum AVPixelFormat formats[] = {AV_PIX_FMT_RGB24, AV_PIX_FMT_NONE};
    //return ff_set_common_formats(ctx, ff_make_format_list(formats));

    GlTransitionContext *c = ctx->priv;
    static const enum AVPixelFormat pix_fmts_rgb[] = {
        AV_PIX_FMT_RGB24,    AV_PIX_FMT_BGR24,
        AV_PIX_FMT_ARGB,     AV_PIX_FMT_ABGR,
        AV_PIX_FMT_RGBA,     AV_PIX_FMT_BGRA,
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat pix_fmts_rgba[] = {
        AV_PIX_FMT_ARGB,     AV_PIX_FMT_ABGR,
        AV_PIX_FMT_RGBA,     AV_PIX_FMT_BGRA,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list;

    if (c->alpha) {
        c->pix_fmt = GL_RGBA;
        c->channel_num = 4;
        fmts_list = ff_make_format_list(pix_fmts_rgba);
    } else {
        c->pix_fmt = GL_RGB;
        c->channel_num = 3;
        fmts_list = ff_make_format_list(pix_fmts_rgb);
    }
    if (!fmts_list) {
      return AVERROR(ENOMEM);
    }
    return ff_set_common_formats(ctx, fmts_list);
}

static int activate(AVFilterContext *ctx)
{
    GlTransitionContext *c = ctx->priv;
    return ff_framesync_activate(&c->fs);
}

static int config_output(AVFilterLink *outLink)
{
    AVFilterContext *ctx = outLink->src;
    GlTransitionContext *c = ctx->priv;
    AVFilterLink *fromLink = ctx->inputs[FROM];
    AVFilterLink *toLink = ctx->inputs[TO];
    int ret;

    if (fromLink->format != toLink->format)
    {
        av_log(ctx, AV_LOG_ERROR, "inputs must be of same pixel format\n");
        return AVERROR(EINVAL);
    }

    if (fromLink->w != toLink->w || fromLink->h != toLink->h)
    {
        av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
                                  "(size %dx%d) do not match the corresponding "
                                  "second input link %s parameters (size %dx%d)\n",
               ctx->input_pads[FROM].name, fromLink->w, fromLink->h,
               ctx->input_pads[TO].name, toLink->w, toLink->h);
        return AVERROR(EINVAL);
    }

    outLink->w = fromLink->w;
    outLink->h = fromLink->h;
    // outLink->time_base = fromLink->time_base;
    outLink->frame_rate = fromLink->frame_rate;

    if ((ret = ff_framesync_init_dualinput(&c->fs, ctx)) < 0)
    {
        return ret;
    }

    return ff_framesync_configure(&c->fs);
}

static const AVFilterPad gltransition_inputs[] = {
    {.name = "from",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props},
    {
        .name = "to",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    {NULL}};

static const AVFilterPad gltransition_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    {NULL}};

AVFilter ff_vf_gltransition = {
    .name = "gltransition",
    .description = NULL_IF_CONFIG_SMALL("OpenGL shader filter transition"),
    .priv_size = sizeof(GlTransitionContext),
    .preinit = gltransition_framesync_preinit,
    .init = init,
    .uninit = uninit,
    .query_formats = query_formats,
    .activate = activate,
    .inputs = gltransition_inputs,
    .outputs = gltransition_outputs,
    .priv_class = &gltransition_class,
    .flags = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC
};
