#include "libavutil/opt.h"
#include "internal.h"
#include "glutil.h"
#include "lavfutils.h"
#include "libavutil/pixdesc.h"

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#ifndef __ANDROID__
#include <GL/glew.h>
#include <GL/glx.h>
#endif
#endif

#ifdef GL_TRANSITION_USING_EGL
# include <EGL/egl.h>
# include <EGL/eglext.h>
#else
# include <GLFW/glfw3.h>
#endif

// ./ffplay -i /mnt/e/git/testpy/xfade_transition/seg/0.mp4 -vf "glcentercrop=tex_count=60:tex_path_fmt=libavfilter/oglfilter/res/lightning/clipname_%03d.png" -an

// 转场 动漫火焰,动漫闪电
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
    "varying vec2 texCoord;\n"
    "\n"
    "uniform sampler2D tex;\n"
    "uniform sampler2D blendTexture;\n"
    "\n"
    "uniform int baseTexWidth;\n"
    "uniform int baseTexHeight;\n"
    "uniform vec2 fullBlendTexSize;\n"
    "\n"
    "uniform float alphaFactor;\n"
    "\n"
    "// uniform float timer;\n"
    "\n"
    "// normal\n"
    "vec3 blendNormal(vec3 base, vec3 blend) {\n"
    "    return blend;\n"
    "}\n"
    "\n"
    "vec3 blendNormal(vec3 base, vec3 blend, float opacity) {\n"
    "    return (blendNormal(base, blend) * opacity + blend * (1.0 - opacity));\n"
    "}\n"
    "\n"
    "vec3 blendFunc(vec3 base, vec3 blend, float opacity,int blendMode) {\n"
    "    // blendMode == 0)\n"
    "    return (blendNormal(base, blend) * opacity + base * (1.0 - opacity));\n"
    "}\n"
    "\n"
    "vec2 sucaiAlign(vec2 videoUV,vec2 videoSize,vec2 sucaiSize,vec2 anchorImageCoord,float sucaiScale)\n"
    "{\n"
    "    vec2 videoImageCoord = videoUV * videoSize;\n"
    "    vec2 sucaiUV= (videoImageCoord - anchorImageCoord)/(sucaiSize * sucaiScale) + vec2(0.5);\n"
    "    return sucaiUV;\n"
    "}\n"
    "\n"
    "vec4 blendColor(sampler2D sucai, vec4 baseColor,vec2 videoSize,vec2 sucaiSize,vec2 anchorImageCoord,float sucaiScale)\n"
    "{\n"
    "    vec4 resultColor = baseColor;\n"
    "\n"
    "    vec2 sucaiUV = sucaiAlign(texCoord,videoSize,sucaiSize,anchorImageCoord,sucaiScale);\n"
    "\n"
    "    vec4 fgColor = baseColor;\n"
    "\n"
    "     if(sucaiUV.x >= 0.0 && sucaiUV.x <= 1.0 && sucaiUV.y >= 0.0 && sucaiUV.y <= 1.0 ) {\n"
    "        // sucaiUV.y = 1.0 - sucaiUV.y;\n"
    "        fgColor = texture2D(sucai,sucaiUV);\n"
    "    } else {\n"
    "        return baseColor;\n"
    "    }\n"
    "\n"
    "    fgColor = fgColor * alphaFactor;\n"
    "\n"
    "    int newBlendMode = 0;//blendMode; \n"
    "\n"
    "    vec3 color = blendFunc(baseColor.rgb, clamp(fgColor.rgb * (1.0 / fgColor.a), 0.0, 1.0), 1.0,newBlendMode);\n"
    "    resultColor.rgb = baseColor.rgb * (1.0 - fgColor.a) + color.rgb * fgColor.a;  \n"
    "    resultColor.a = 1.0;    \n"
    "  \n"
    "    return resultColor;\n"
    "}\n"
    "\n"
    "void main(void) \n"
    "{\n"
    "    vec2 uv_ = vec2(texCoord.x, 1.0 - texCoord.y);"
    "    vec2 baseTexureSize = vec2(baseTexWidth,baseTexHeight);\n"
    "    vec2 fullBlendAnchor = baseTexureSize * 0.5;\n"
    "    float scale = 1.0;\n"
    "\n"
    "    //外居中对齐\n"
    "    float baseAspectRatio = baseTexureSize.y/baseTexureSize.x;\n"
    "    float blendAspectRatio = fullBlendTexSize.y/fullBlendTexSize.x;\n"
    "    if(baseAspectRatio >= blendAspectRatio) {\n"
    "        scale = baseTexureSize.y / fullBlendTexSize.y;   \n"
    "    } else {\n"
    "        scale = baseTexureSize.x / fullBlendTexSize.x; \n"
    "    }\n"
    "\n"
    "    vec4 baseColor = texture2D(tex,uv_);\n"
    "    vec4 fullblendColor = blendColor(blendTexture,baseColor,baseTexureSize,fullBlendTexSize,\n"
    "        fullBlendAnchor,scale);\n"
    "\n"
    "    gl_FragColor = fullblendColor;\n"
    "}\n";

#define PIXEL_FORMAT GL_RGB

typedef struct
{
    const AVClass *class;
    GLuint program;
    GLuint frame_tex;
    GLuint mask_tex;
    GLuint pos_buf;
    GLint pos_type;

    int no_window;
    int type;

    uint8_t *mask_data;
    int mask_pic_num;
    char *mask_pic_fmt;
    int mask_width;
    int mask_height;
    int mask_channels;
    int mask_pix_fmt;

#ifdef GL_TRANSITION_USING_EGL
    EGLDisplay      eglDpy;
    EGLConfig       eglCfg;
    EGLSurface      eglSurf;
    EGLContext      eglCtx;
#else
    GLFWwindow*     window;
#endif
} GlCenterCropContext;

#define OFFSET(x) offsetof(GlCenterCropContext, x)

#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption glcentercrop_options[] = {
    {"nowindow", "ssh mode, no window init open gl context", OFFSET(no_window), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, .flags = FLAGS},
    {"tex_count", "total texture file count", OFFSET(mask_pic_num), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 200, FLAGS},
    {"tex_path_fmt", "texture file format, e.g.: image/%d.png", OFFSET(mask_pic_fmt), AV_OPT_TYPE_STRING, {.str = "0"}, 0, 0, FLAGS },
    {NULL}};

AVFILTER_DEFINE_CLASS(glcentercrop);

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
static void vbo_setup(GlCenterCropContext *gs)
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
    GlCenterCropContext *gs = ctx->priv;

    glGenTextures(1, &gs->frame_tex);
    glActiveTexture(GL_TEXTURE0);

    glBindTexture(GL_TEXTURE_2D, gs->frame_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, inlink->w, inlink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, NULL);

    glUniform1i(glGetUniformLocation(gs->program, "tex"), 0);

    // mask
    if (gs->mask_pic_fmt == NULL || gs->mask_pic_num == 0 || gs->mask_pic_num > 256) {
        av_log(NULL, AV_LOG_ERROR, "invalid mask pic settings\n");
        return -1;
    }

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
                tex_frame->format != AV_PIX_FMT_GRAY8 && tex_frame->format != AV_PIX_FMT_PAL8) {
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
        case AV_PIX_FMT_PAL8:
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
        if (!gs->mask_data)
            gs->mask_data = av_mallocz(frame_data_size * gs->mask_pic_num);
        
        if (tex_frame->linesize[0] == tex_frame->width * gs->mask_channels) {
            // bunch copy
            memcpy(gs->mask_data + frame_data_size * i, tex_frame->data[0], tex_frame->linesize[0] * tex_frame->height);
        }
        else {
            // line copy
            int data_offset = 0;
            int frame_offset = 0;
            for (int line=0;line<tex_frame->height;line++) { 
                memcpy(gs->mask_data + frame_data_size * i + data_offset, 
                    tex_frame->data[0] + frame_offset, 
                    tex_frame->width * gs->mask_channels);
                data_offset += tex_frame->width * gs->mask_channels;
                frame_offset += tex_frame->linesize[0];
            }
        }
        
        av_frame_free(&tex_frame);
    }

    glGenTextures(1, &gs->mask_tex);
    glActiveTexture(GL_TEXTURE0 + 1);

    glBindTexture(GL_TEXTURE_2D, gs->mask_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, gs->mask_pix_fmt, gs->mask_width, gs->mask_height, 0, gs->mask_pix_fmt, GL_UNSIGNED_BYTE, NULL);

    glUniform1i(glGetUniformLocation(gs->program, "blendTexture"), 1);

    // set other uniforms
    glUniform1i(glGetUniformLocation(gs->program, "baseTexWidth"), gs->mask_width);
    glUniform1i(glGetUniformLocation(gs->program, "baseTexHeight"), gs->mask_height);
    glUniform2f(glGetUniformLocation(gs->program, "fullBlendTexSize"), inlink->w, inlink->h);
    glUniform1f(glGetUniformLocation(gs->program, "alphaFactor"), 1.0f);
    
    return 0;
}

static int build_program(AVFilterContext *ctx)
{
    GLuint v_shader, f_shader;
    GlCenterCropContext *gs = ctx->priv;

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
    GlCenterCropContext *gs = ctx->priv;
    
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
    GlCenterCropContext *gs = ctx->priv;

    gs->pos_type = glGetUniformLocation(gs->program, "type");
    glUniform1i(gs->pos_type, gs->type);
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    GlCenterCropContext *gs = ctx->priv;
    int ret;

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
    ret = eglChooseConfig(gs->eglDpy, configAttribs, &gs->eglCfg, 1, &numConfigs);
    if (!ret) {
        av_log(NULL, AV_LOG_ERROR, "eglChooseConfig error: %d\n", ret);
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

    if ((ret = build_program(ctx)) < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "failed to build ogl program: %d\n", ret);
        return ret;
    }

    glUseProgram(gs->program);
    vbo_setup(gs);
    setup_uniforms(inlink);
    if (tex_setup(inlink) < 0) {
        av_log(NULL, AV_LOG_ERROR, "failed to setup texture\n");
        return -1;
    }
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    GlCenterCropContext *gs = ctx->priv;

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
    {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    float time = in->pts * av_q2d(inlink->time_base);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gs->frame_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 
        outlink->w, outlink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, in->data[0]);

    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, gs->mask_tex);
    int idx = (int)(time * 1000.0 / 40.0) % gs->mask_pic_num;
    int offset = gs->mask_width * gs->mask_height * gs->mask_channels * idx;
    glTexImage2D(GL_TEXTURE_2D, 0, gs->mask_pix_fmt, gs->mask_width, gs->mask_height, 
        0, gs->mask_pix_fmt, GL_UNSIGNED_BYTE, gs->mask_data + offset);

    glDrawArrays(GL_TRIANGLES, 0, 6);
    glReadPixels(0, 0, outlink->w, outlink->h, PIXEL_FORMAT, GL_UNSIGNED_BYTE, (GLvoid *)out->data[0]);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    GlCenterCropContext *gs = ctx->priv;

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

static const AVFilterPad glcentercrop_inputs[] = {
    {.name = "default",
     .type = AVMEDIA_TYPE_VIDEO,
     .config_props = config_props,
     .filter_frame = filter_frame},
    {NULL}};

static const AVFilterPad glcentercrop_outputs[] = {
    {.name = "default", .type = AVMEDIA_TYPE_VIDEO}, {NULL}};

AVFilter ff_vf_glcentercrop = {
    .name = "glcentercrop",
    .description = NULL_IF_CONFIG_SMALL("OpenGL shader filter centercrop"),
    .priv_size = sizeof(GlCenterCropContext),
    .init = init,
    .uninit = uninit,
    .query_formats = query_formats,
    .inputs = glcentercrop_inputs,
    .outputs = glcentercrop_outputs,
    .priv_class = &glcentercrop_class,
    .flags = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC};
