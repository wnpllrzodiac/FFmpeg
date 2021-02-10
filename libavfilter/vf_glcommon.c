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
    //"#version 130\n"
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
    "uniform float ratio;\n"
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

    int no_window;
    int is_pbo;

    GLchar *f_shader_source;
} GlCommonContext;

#define OFFSET(x) offsetof(GlCommonContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM

static const AVOption glcommon_options[] = {
    {"nowindow", "ssh mode, no window init open gl context", OFFSET(no_window), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, .flags = FLAGS},
    {"source", "path to the gl-transition source file (defaults to basic fade)", OFFSET(source), AV_OPT_TYPE_STRING, {.str = NULL}, CHAR_MIN, CHAR_MAX, FLAGS},
    { "uniforms", "uniform vars setting, e.g. uniforms='some_var=1.0&other_var=1&direction=vec2(0.0,1.0)'", OFFSET(uniforms), AV_OPT_TYPE_STRING, {.str=NULL}, CHAR_MIN, CHAR_MAX, FLAGS },
    {NULL}};

FRAMESYNC_DEFINE_CLASS(glcommon, GlCommonContext, fs);

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

static void vbo_setup(GlCommonContext *gs)
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
    GlCommonContext *gs = ctx->priv;

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
    GlCommonContext *gs = ctx->priv;

    if (!(v_shader = build_shader(ctx, v_shader_source, GL_VERTEX_SHADER))) {
        av_log(ctx, AV_LOG_ERROR, "invalid vertex shader\n");
        return -1;
    }

    char *source = NULL;
    if (gs->source) {
        FILE *f = fopen(gs->source, "rb");

        if (!f)
        {
            av_log(ctx, AV_LOG_ERROR, "invalid transition source file \"%s\"\n", gs->source);
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

    const char *frag_source = source ? source : f_default_source;
    int len = strlen(frag_source);
    gs->f_shader_source = av_calloc(len + 1, sizeof(*gs->f_shader_source));
    if (!gs->f_shader_source) {
        return AVERROR(ENOMEM);
    }

    strncpy(gs->f_shader_source, len * sizeof(*gs->f_shader_source), frag_source);
    av_log(ctx, AV_LOG_ERROR, "\n%s\n", gs->f_shader_source);

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

static av_cold int init(AVFilterContext *ctx)
{
    GlCommonContext *gs = ctx->priv;
    gs->fs.on_event = blend_frame;
    gs->first_pts = AV_NOPTS_VALUE;

    if (gs->no_window) {
        av_log(NULL, AV_LOG_ERROR, "open gl no window init ON\n");
        no_window_init();
    }

    return glfwInit() ? 0 : -1;
}

static void setup_uniforms(AVFilterLink *fromLink)
{
    AVFilterContext *ctx = fromLink->dst;
    GlCommonContext *gs = ctx->priv;

    // TODO: this should be output ratio
    GLuint ratio = glGetUniformLocation(gs->program, "ratio");
    glUniform1f(ratio, fromLink->w / (float)fromLink->h);

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
                            break;
                        case 3:
                            glUniform3f(location, floatVec[0], floatVec[1], floatVec[2]);
                            break;
                        case 4:
                            glUniform4f(location, floatVec[0], floatVec[1], floatVec[2], floatVec[3]);
                            break;
                        default:
                            break;
                        }
                    } else if (parseGlSLIntVector(sa.strings[i + 1], intVec, &vecShape)) {
                        switch (vecShape) {
                        case 2:
                            glUniform2i(location, intVec[0], intVec[1]);
                            break;
                        case 3:
                            glUniform3i(location, intVec[0], intVec[1], intVec[2]);
                            break;
                        case 4:
                            glUniform4i(location, intVec[0], intVec[1], intVec[2], intVec[3]);
                        default:
                            break;
                        }
                    } else if (strToInt(sa.strings[i + 1], &intVar)) {
                        glUniform1i(location, intVar);
                    } else if (strToFloat(sa.strings[i + 1], &floatVar)) {
                        glUniform1f(location, floatVar);
                    } else {
                        av_log(ctx, AV_LOG_ERROR, "value %s not supported, supported: int float ivec vec)", sa.strings[i + 1]);
                    }
                } else {
                av_log(ctx, AV_LOG_ERROR, "can't get location of uniform: %s,fail set value: %s\n", sa.strings[i],
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
    GlCommonContext *gs = ctx->priv;

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
    GlCommonContext *gs = ctx->priv;
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

    glReadPixels(0, 0, outlink->w, outlink->h, 
        PIXEL_FORMAT, GL_UNSIGNED_BYTE, (GLvoid *)out->data[0]);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    GlCommonContext *gs = ctx->priv;
    ff_framesync_uninit(&gs->fs);

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

static const AVFilterPad glcommon_inputs[] = {
    {.name = "default",
     .type = AVMEDIA_TYPE_VIDEO,
     .config_props = config_props,
     .filter_frame = filter_frame},
    {NULL}};

static const AVFilterPad glcommon_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    {NULL}};

AVFilter ff_vf_glcommon = {
    .name = "glcommon",
    .description = NULL_IF_CONFIG_SMALL("OpenGL shader filter common, can set customized frag shader"),
    .priv_size = sizeof(GlCommonContext),
    .init = init,
    .uninit = uninit,
    .query_formats = query_formats,
    .inputs = glcommon_inputs,
    .outputs = glcommon_outputs,
    .priv_class = &glcommon_class,
    .flags = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC};
