#include "libavutil/opt.h"
#include "internal.h"
#include "glutil.h"

// sudo apt install libglew-dev
// wget https://github.com/glfw/glfw/releases/download/3.3.2/glfw-3.3.2.zip
// sudo apt install cmake libxrandr libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
// 
// --enable-gpl --enable-opengl --extra-libs='-lGLEW -lglfw3'

// with EGL:
// --enable-gpl --enable-opengl --extra-libs='-lGLEW -lEGL'

// export DISPLAY=:0.0
// ffmpeg_g -i ~/work/media/astroboy.mp4 -vf scale=640x480,glnone -c:v libx264 -b:v 512k -c:a copy -t 10 -y out.mp4

#ifndef __APPLE__
# define GL_TRANSITION_USING_EGL //remove this line if you don't want to use EGL
#endif

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#include <GL/glx.h>
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
    //"  texCoord = vec2(_uv.x, 1.0 - _uv.y);\n"
    "  texCoord = _uv;\n"
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
    GLuint pos_buf;
    GLuint pbo_ids[2];

    int no_window;
    int is_pbo;

#ifdef GL_TRANSITION_USING_EGL
    EGLDisplay      eglDpy;
    EGLConfig       eglCfg;
    EGLSurface      eglSurf;
    EGLContext      eglCtx;
#else
    GLFWwindow*     window;
#endif
} GlNoneContext;

#define OFFSET(x) offsetof(GlNoneContext, x)

#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption glnone_options[] = {
    {"nowindow", "ssh mode, no window init open gl context", OFFSET(no_window), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, .flags = FLAGS},
    {"pbo", "use PBO mode to read image data", OFFSET(is_pbo), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, .flags = FLAGS},
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

static void pbo_setup(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    GlNoneContext *gs = ctx->priv;

    int imgByteSize = inlink->w * inlink->h * 3;//RGB

    glGenBuffers(2, &gs->pbo_ids[0]);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, gs->pbo_ids[0]);
    glBufferData(GL_PIXEL_PACK_BUFFER, imgByteSize, 0, GL_STREAM_DRAW);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, gs->pbo_ids[1]);
    glBufferData(GL_PIXEL_PACK_BUFFER, imgByteSize, 0, GL_STREAM_DRAW);
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
    
#ifndef GL_TRANSITION_USING_EGL
    if (gs->no_window) {
        av_log(NULL, AV_LOG_ERROR, "open gl no window init ON\n");
        no_window_init();
    }

    return glfwInit() ? 0 : -1;
#endif

    return 0;
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    GlNoneContext *gs = ctx->priv;

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
    if (gs->is_pbo) {
        av_log(NULL, AV_LOG_WARNING, "enable PBO\n");
        pbo_setup(inlink);
    }
    tex_setup(inlink);
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    GlNoneContext *gs = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

#ifdef GL_TRANSITION_USING_EGL
    //eglMakeCurrent(c->eglDpy, c->eglSurf, c->eglSurf, c->eglCtx);
#else
    //glfwMakeContextCurrent(c->window);
#endif

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
    {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, inlink->w, inlink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, in->data[0]);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    if (gs->is_pbo) {
        int index = inlink->frame_count_out % 2;
        int nextIndex = (index + 1) % 2;
        //av_log(NULL, AV_LOG_WARNING, "%" PRId64 ", %d, %d\n", inlink->frame_count_out, index, nextIndex);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, gs->pbo_ids[index]);
        glReadPixels(0, 0, outlink->w, outlink->h, PIXEL_FORMAT, GL_UNSIGNED_BYTE, NULL);

        glBindBuffer(GL_PIXEL_PACK_BUFFER, gs->pbo_ids[nextIndex]);
        GLubyte *bufPtr = (GLubyte *)(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
                                                            outlink->w * outlink->h * 3,
                                                            GL_MAP_READ_BIT));
        if (bufPtr) {
            out->data[0] = bufPtr;
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }
    else {
        glReadPixels(0, 0, outlink->w, outlink->h, PIXEL_FORMAT, GL_UNSIGNED_BYTE, (GLvoid *)out->data[0]);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    GlNoneContext *gs = ctx->priv;
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
