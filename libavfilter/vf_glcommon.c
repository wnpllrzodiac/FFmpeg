#include "libavutil/opt.h"
#include "internal.h"
#include "glutil.h"

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#ifdef GL_TRANSITION_USING_EGL
# include <EGL/egl.h>
# include <EGL/eglext.h>
#else
# include <GLFW/glfw3.h>
#endif

#ifdef GL_TRANSITION_USING_EGL
static const EGLint configAttribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_BLUE_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_RED_SIZE, 8,
    EGL_DEPTH_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_NONE};
#endif

// -vf "glcommon=source=libavfilter/oglfilter/heart_shader.frag:
//   precision=highp:uniforms='u_duration=5.0&u_blur=0.06
//    &u_heartcolor=vec3(1.0,0.5,0.3)&u_eccentricity=0.38&u_bgcolor=vec3(1.0,0.8,0.8)'"

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
    //"  texCoord = vec2(_uv.x, 1.0 - _uv.y);\n"
    "  texCoord = _uv;\n"
    "}\n";

static const GLchar *f_shader_template =
    "#version 130\n"
    "precision %s float;\n"
    "\n"
    "uniform sampler2D tex;\n"
    "varying vec2 texCoord;\n"
    "uniform vec2 u_screenSize;\n"
    "uniform float u_time;\n"
    "\n%s\n";

static const GLchar *f_default_frag_source =
    "void main() {\n"
    "  gl_FragColor = texture2D(tex, texCoord);\n"
    "}\n";

static const GLchar *f_default_shader_precision =
    "mediump";

#define PIXEL_FORMAT GL_RGB

typedef struct
{
    const AVClass *class;
    GLuint program;
    GLuint frame_tex;
    GLuint pos_buf;

    int no_window;
    int repeat;

    char *source;
    char *precision;
    char *uniforms;

    GLint time;

    GLchar *f_shader_source;

#ifdef GL_TRANSITION_USING_EGL
    EGLDisplay      eglDpy;
    EGLConfig       eglCfg;
    EGLSurface      eglSurf;
    EGLContext      eglCtx;
#else
    GLFWwindow*     window;
#endif
} GlCommonContext;

#define OFFSET(x) offsetof(GlCommonContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM

static const AVOption glcommon_options[] = {
    {"nowindow", "ssh mode, no window init open gl context", OFFSET(no_window), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, .flags = FLAGS},
    {"repeat", "repeat render in secs", OFFSET(repeat), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, .flags = FLAGS},
    {"source", "path to the gl-transition source file (defaults to basic fade)", OFFSET(source), AV_OPT_TYPE_STRING, {.str = NULL}, CHAR_MIN, CHAR_MAX, FLAGS},
    {"precision", "precision settings: lowp, mediump, highp", OFFSET(precision), AV_OPT_TYPE_STRING, {.str = NULL}, CHAR_MIN, 16, FLAGS},
    { "uniforms", "uniform vars setting, e.g. uniforms='some_var=1.0&other_var=1&direction=vec2(0.0,1.0)'", OFFSET(uniforms), AV_OPT_TYPE_STRING, {.str=NULL}, CHAR_MIN, CHAR_MAX, FLAGS },
    {NULL}};

AVFILTER_DEFINE_CLASS(glcommon);

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

    const char *frag_source = source ? source : f_default_frag_source;
    const char *prec = gs->precision ? gs->precision : f_default_shader_precision;
    int len = strlen(f_shader_template) + strlen(frag_source) + 16/*precision*/;
    gs->f_shader_source = av_calloc(len, sizeof(*gs->f_shader_source));
    if (!gs->f_shader_source) {
        return AVERROR(ENOMEM);
    }

    snprintf(gs->f_shader_source, len * sizeof(*gs->f_shader_source), 
        f_shader_template, prec, frag_source);
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
    
#ifndef GL_TRANSITION_USING_EGL
    if (gs->no_window)
    {
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
    GlCommonContext *gs = ctx->priv;

    gs->time = glGetUniformLocation(gs->program, "u_time");
    glUniform1f(gs->time, 0.0f);

    glUniform2f(glGetUniformLocation(gs->program, "u_screenSize"), 
        (float)fromLink->w, (float)fromLink->h);

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
    GlCommonContext *gs = ctx->priv;

#ifdef GL_TRANSITION_USING_EGL
    //init EGL
    // 1. Initialize EGL
    // c->eglDpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);

 #define MAX_DEVICES 4
    EGLDeviceEXT eglDevs[MAX_DEVICES];
    EGLint numDevices;

    PFNEGLQUERYDEVICESEXTPROC eglQueryDevicesEXT =(PFNEGLQUERYDEVICESEXTPROC)
    eglGetProcAddress("eglQueryDevicesEXT");

    eglQueryDevicesEXT(MAX_DEVICES, eglDevs, &numDevices);

    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =  (PFNEGLGETPLATFORMDISPLAYEXTPROC)
    eglGetProcAddress("eglGetPlatformDisplayEXT");

    gs->eglDpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, eglDevs[0], 0);

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
    eglChooseConfig(gs->eglDpy, configAttribs, &gs->eglCfg, 1, &numConfigs);
    // 3. Create a surface
    gs->eglSurf = eglCreatePbufferSurface(gs->eglDpy, gs->eglCfg, pbufferAttribs);
    // 4. Bind the API
    eglBindAPI(EGL_OPENGL_API);
    // 5. Create a context and make it current
    gs->eglCtx = eglCreateContext(gs->eglDpy, gs->eglCfg, EGL_NO_CONTEXT, NULL);
    eglMakeCurrent(gs->eglDpy, gs->eglSurf, gs->eglSurf, gs->eglCtx);
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
    tex_setup(inlink);
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    GlCommonContext *gs = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    float time = in->pts * av_q2d(inlink->time_base);
    if (gs->repeat && time > 3.0)
        time -= (int)time / 3 * 3;
    glUniform1f(gs->time, time);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, inlink->w, inlink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, in->data[0]);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    if (out->linesize[0] != outlink->w * 3/*rgb*/) {
        av_frame_free(&in);
        av_log(NULL, AV_LOG_ERROR, "image has padding: linesize %d, w %d\n", 
            out->linesize[0], outlink->w);
        return AVERROR(ENOMEM);
    }

    glReadPixels(0, 0, outlink->w, outlink->h, 
        PIXEL_FORMAT, GL_UNSIGNED_BYTE, (GLvoid *)out->data[0]);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    GlCommonContext *gs = ctx->priv;

#ifdef GL_TRANSITION_USING_EGL
    if (gs->eglDpy) {
        glDeleteTextures(1, &gs->frame_tex);
        glDeleteProgram(gs->program);
        glDeleteBuffers(1, &gs->pos_buf);
        eglTerminate(gs->eglDpy);
    }
#else
    if (gs->window) {
        glDeleteTextures(1, &gs->frame_tex);
        glDeleteProgram(gs->program);
        glDeleteBuffers(1, &gs->pos_buf);
        glfwDestroyWindow(gs->window);
    }
#endif
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
    {.name = "default", .type = AVMEDIA_TYPE_VIDEO}, {NULL}};

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
