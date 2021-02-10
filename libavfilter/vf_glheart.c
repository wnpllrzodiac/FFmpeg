#include "libavutil/opt.h"
#include "internal.h"
#include "glutil.h"

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#include <GLFW/glfw3.h>

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
    "attribute vec2 position;\n"
    "varying vec2 texCoord;\n"
    "void main(void) {\n"
    "  gl_Position = vec4(position, 0, 1);\n"
    "  vec2 _uv = position * 0.5 + 0.5;\n"
    "  texCoord = vec2(_uv.x, 1.0 - _uv.y);\n"
    "}\n";

static const GLchar *f_shader_source =
    "#version 130\n"
    "precision highp float;\n"
    "\n"
    "uniform sampler2D tex;\n"
    "varying vec2 texCoord;\n"
    "\n"
    "uniform float u_time;\n"
    "uniform vec2 u_screenSize;\n"
    "uniform vec3 u_bgcolor; // vec3(1.0,0.8,0.8)\n"
    "\n"
    "const float PI = 3.141592653;\n"
    "\n"
    "void main(void) {\n"
    "    // move to center\n"
    "    vec2 fragCoord = vec2(texCoord.x * u_screenSize.x, texCoord.y * u_screenSize.y);\n"
    "    vec2 p = (2.0*fragCoord-u_screenSize.xy)/min(u_screenSize.y,u_screenSize.x);\n"
    "\n"
    "    // background color\n"
    "    vec3 bcol = u_bgcolor * (1.0-0.38*length(p));\n"
    "\n"
    "    // animate\n"
    "    float tt = u_time;\n"
    "    float ss = pow(tt,.2)*0.5 + 0.5;\n"
    "    ss = 1.0 + ss*0.5*sin(tt*6.2831*3.0 + p.y*0.5)*exp(-tt*4.0);\n"
    "    p *= vec2(0.5,1.5) + ss*vec2(0.5,-0.5);\n"
    "\n"
    "    // shape\n"
    "    p.y -= 0.25;\n"
    "    float a = atan(p.x,p.y) / PI;\n"
    "    float r = length(p);\n"
    "    float h = abs(a);\n"
    "    float d = (13.0*h - 22.0*h*h + 10.0*h*h*h)/(6.0-5.0*h);\n"
    "\n"
    "    // color\n"
    "    float s = 0.75 + 0.75*p.x;\n"
    "    s *= 1.0-0.4*r;\n"
    "    s = 0.3 + 0.7*s;\n"
    "    s *= 0.5+0.5*pow( 1.0-clamp(r/d, 0.0, 1.0 ), 0.1 );\n"
    "    vec3 hcol = vec3(1.0,0.5*r,0.3)*s;\n"
    "\n"
    "    vec3 col = mix( bcol, hcol, smoothstep( -0.06, 0.06, d-r) );\n"
    "\n"
    "    gl_FragColor = vec4(col,1.0);\n"
    "}\n"
    ;

#define PIXEL_FORMAT GL_RGB

    typedef struct
{
    const AVClass *class;
    GLuint program;
    GLuint frame_tex;
    GLFWwindow *window;
    GLuint pos_buf;

    GLint time;
    int no_window;
    int bg_r;
    int bg_g;
    int bg_b;
} GlHeartContext;

#define OFFSET(x) offsetof(GlHeartContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption glheart_options[] = {
    {"nowindow", "ssh mode, no window init open gl context", OFFSET(no_window), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, .flags = FLAGS},
    {"r", "background color red", OFFSET(bg_r), AV_OPT_TYPE_INT, {.i64 = 255}, 0, 255, .flags = FLAGS},
    {"g", "background color green", OFFSET(bg_g), AV_OPT_TYPE_INT, {.i64 = 204}, 0, 255, .flags = FLAGS},
    {"b", "background color blue", OFFSET(bg_b), AV_OPT_TYPE_INT, {.i64 = 204}, 0, 255, .flags = FLAGS},
    {NULL}};

AVFILTER_DEFINE_CLASS(glheart);

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

static void vbo_setup(GlHeartContext *gs)
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
    GlHeartContext *gs = ctx->priv;

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
    GlHeartContext *gs = ctx->priv;

    if (!((v_shader = build_shader(ctx, v_shader_source, GL_VERTEX_SHADER)) &&
          (f_shader = build_shader(ctx, f_shader_source, GL_FRAGMENT_SHADER))))
    {
        av_log(NULL, AV_LOG_ERROR, "failed to build shader: vsh: %d, fsh: %d\n", v_shader, f_shader);
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
    GlHeartContext *gs = ctx->priv;
    if (gs->no_window) {
        av_log(NULL, AV_LOG_ERROR, "open gl no window init ON\n");
        no_window_init();
    }

    return glfwInit() ? 0 : -1;
}

static void setup_uniforms(AVFilterLink *fromLink)
{
    GLuint size, color;

    AVFilterContext *ctx = fromLink->dst;
    GlHeartContext *gs = ctx->priv;

    gs->time = glGetUniformLocation(gs->program, "u_time");
    glUniform1f(gs->time, 0.0f);

    size = glGetUniformLocation(gs->program, "u_screenSize");
    glUniform2f(size, fromLink->w, fromLink->h);

    // vec3(1.0,0.8,0.8)
    color = glGetUniformLocation(gs->program, "u_bgcolor");
    glUniform3f(color, gs->bg_r / 255.0f, gs->bg_g / 255.0f, gs->bg_b / 255.0f);
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    GlHeartContext *gs = ctx->priv;

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
    setup_uniforms(inlink);
    tex_setup(inlink);
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    GlHeartContext *gs = ctx->priv;

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
    {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    float time = in->pts * av_q2d(inlink->time_base);
    if (time > 3.0)
        time -= (int)time / 3 * 3;
    glUniform1f(gs->time, time);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, inlink->w, inlink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, in->data[0]);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glReadPixels(0, 0, outlink->w, outlink->h, PIXEL_FORMAT, GL_UNSIGNED_BYTE, (GLvoid *)out->data[0]);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    GlHeartContext *gs = ctx->priv;
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

static const AVFilterPad glheart_inputs[] = {
    {.name = "default",
     .type = AVMEDIA_TYPE_VIDEO,
     .config_props = config_props,
     .filter_frame = filter_frame},
    {NULL}};

static const AVFilterPad glheart_outputs[] = {
    {.name = "default", .type = AVMEDIA_TYPE_VIDEO}, {NULL}};

AVFilter ff_vf_glheart = {
    .name = "glheart",
    .description = NULL_IF_CONFIG_SMALL("OpenGL shader filter heart"),
    .priv_size = sizeof(GlHeartContext),
    .init = init,
    .uninit = uninit,
    .query_formats = query_formats,
    .inputs = glheart_inputs,
    .outputs = glheart_outputs,
    .priv_class = &glheart_class,
    .flags = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC};
