#include "libavutil/opt.h"
#include "internal.h"
#include "glutil.h"

// --enable-gpl --enable-opengl --extra-libs='-lGLEW -lglfw'
// export DISPLAY=:0.0
// ffmpeg_g -i ~/work/media/astroboy.mp4 -vf scale=640x480,glnone -c:v libx264 -b:v 512k -c:a copy -t 10 -y out.mp4

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#include <GL/glx.h>
#endif

#include <GLFW/glfw3.h>

/*
2,3     5

0       1,4
*/
static const float position[12] = {
    -1.0f, -1.0f, 
     1.0f, -1.0f, 
    -1.0f,  1.0f, 
    -1.0f,  1.0f, 
     1.0f, -1.0f,
     1.0f,  1.0f
};

static const GLchar *v_shader_source =
    "attribute vec2 position;\n"
    "varying vec2 texCoord;\n"
    "void main(void) {\n"
    "  gl_Position = vec4(position, 0, 1);\n"
    "  vec2 _uv = position * 0.5 + 0.5;\n"
    "  texCoord = vec2(_uv.x, 1.0 - _uv.y);\n"
    "}\n";

static const GLchar *f_shader_source =
    "uniform sampler2D tex;\n"
    "varying vec2 texCoord;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(tex, texCoord);\n"
    "}\n";

#define PIXEL_FORMAT GL_RGB

typedef struct
{
    const AVClass *class;
    GLuint program;
    GLuint frame_tex;
    GLFWwindow *window;
    GLuint pos_buf;

    int no_window;
} GlNoneContext;

#define OFFSET(x) offsetof(GlNoneContext, x)

#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption glnone_options[] = {
    {"nowindow", "ssh mode, no window init open gl context", OFFSET(no_window), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, .flags = FLAGS},
    {NULL}
};

AVFILTER_DEFINE_CLASS(glnone);

static GLuint build_shader(AVFilterContext *ctx, const GLchar *shader_source, GLenum type)
{
    GLuint shader = glCreateShader(type);
    if (!shader || !glIsShader(shader))
    {
        return 0;
    }
    glShaderSource(shader, 1, &shader_source, 0);
    glCompileShader(shader);
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    return status == GL_TRUE ? shader : 0;
}

static void vbo_setup(GlNoneContext *gs)
{
    glGenBuffers(1, &gs->pos_buf);
    glBindBuffer(GL_ARRAY_BUFFER, gs->pos_buf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(position), position, GL_STATIC_DRAW);

    GLint loc = glGetAttribLocation(gs->program, "position");
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 0, 0);
}

static void tex_setup(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    GlNoneContext *gs = ctx->priv;

    glGenTextures(1, &gs->frame_tex);
    glActiveTexture(GL_TEXTURE0);

    glBindTexture(GL_TEXTURE_2D, gs->frame_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, inlink->w, inlink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, NULL);

    glUniform1i(glGetUniformLocation(gs->program, "tex"), 0);
}

static int build_program(AVFilterContext *ctx)
{
    GLuint v_shader, f_shader;
    GlNoneContext *gs = ctx->priv;

    if (!((v_shader = build_shader(ctx, v_shader_source, GL_VERTEX_SHADER)) &&
          (f_shader = build_shader(ctx, f_shader_source, GL_FRAGMENT_SHADER))))
    {
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

static av_cold int init(AVFilterContext *ctx)
{
    GlNoneContext *gs = ctx->priv;
    if (gs->no_window) {
        av_log(NULL, AV_LOG_ERROR, "open gl no window init ON\n");
        no_window_init();
    }

    return glfwInit() ? 0 : -1;
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    GlNoneContext *gs = ctx->priv;

    glfwWindowHint(GLFW_VISIBLE, 0);
    gs->window = glfwCreateWindow(inlink->w, inlink->h, "", NULL, NULL);

    glfwMakeContextCurrent(gs->window);

#ifndef __APPLE__
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
    tex_setup(inlink);
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
    {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, inlink->w, inlink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, in->data[0]);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glReadPixels(0, 0, outlink->w, outlink->h, PIXEL_FORMAT, GL_UNSIGNED_BYTE, (GLvoid *)out->data[0]);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    GlNoneContext *gs = ctx->priv;
    if (gs->window) {
        glDeleteTextures(1, &gs->frame_tex);
        glDeleteProgram(gs->program);
        glDeleteBuffers(1, &gs->pos_buf);
        glfwDestroyWindow(gs->window);
    }
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat formats[] = {AV_PIX_FMT_RGB24, AV_PIX_FMT_NONE};
    return ff_set_common_formats(ctx, ff_make_format_list(formats));
}

static const AVFilterPad glnone_inputs[] = {
    {.name = "default",
     .type = AVMEDIA_TYPE_VIDEO,
     .config_props = config_props,
     .filter_frame = filter_frame},
    {NULL}};

static const AVFilterPad glnone_outputs[] = {
    {.name = "default", .type = AVMEDIA_TYPE_VIDEO}, {NULL}};

AVFilter ff_vf_glnone = {
    .name = "glnone",
    .description = NULL_IF_CONFIG_SMALL("Generic OpenGL shader filter"),
    .priv_size = sizeof(GlNoneContext),
    .init = init,
    .uninit = uninit,
    .query_formats = query_formats,
    .inputs = glnone_inputs,
    .outputs = glnone_outputs,
    .priv_class = &glnone_class,
    .flags = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC};
