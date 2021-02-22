#include "libavutil/opt.h"
#include "internal.h"
#include "glutil.h"

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

static const GLchar *f_shader_source =
    "#version 130\n"
    "precision mediump float;\n"
    "\n"
    "uniform sampler2D tex;\n"
    "varying vec2 texCoord;\n"
    "uniform float time;\n"
    "\n"
    "float Noise(float t) {\n"
    "    return fract(sin(t * 3456.0)*6543.0);\n"
    "}\n"
    "\n"
    "vec4 Noise4(float t) {\n"
    "    return fract(sin(t * vec4(123.0, 1024.0, 3456.0, 9564.0))*vec4(6543.0, 345.0, 8799.0, 1564.0));\n"
    "}\n"
    "\n"
    "struct ray {\n"
    "    vec3 o, d;\n"
    "};\n"
    "\n"
    "ray GetRay(vec2 uv, vec3 camPos, vec3 lookat, float zoom) {\n"
    "    ray a;\n"
    "    a.o = camPos;\n"
    "    vec3 forward = normalize(lookat - camPos);\n"
    "    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), forward));\n"
    "    vec3 up = normalize(cross(forward, right));\n"
    "    vec3 center = a.o + forward * zoom;\n"
    "    vec3 i = center + uv.x*right + uv.y*up;\n"
    "    a.d = normalize(i - a.o);\n"
    "\n"
    "    return a;\n"
    "}\n"
    "\n"
    "float DistRay(ray r, vec3 p) {\n"
    "    return length(cross(p - r.o, r.d))/length(r.d);\n"
    "}\n"
    "\n"
    "float Bokeh(ray r, vec3 p, float size, float blur) {\n"
    "    float dist = DistRay(r , p);\n"
    "    size *= length(p);\n"
    "    float light = smoothstep(size, size * (1.0 - blur), dist);\n"
    "    light *= mix(0.7, 1.0, smoothstep(size* 0.8, size, dist));\n"
    "\n"
    "    return light;\n"
    "}\n"
    "\n"
    "vec3 StreetLights(ray r, float t) {\n"
    "    float side = step(r.d.x, 0.0);\n"
    "    r.d.x = abs(r.d.x);\n"
    "\n"
    "    float s = 0.1;\n"
    "    float mask = 0.0;\n"
    "\n"
    "    for (float i = 0.0; i<1.0; i+=s) {\n"
    "        float ti = fract(t + i + side*s*0.5);\n"
    "        vec3 p = vec3(2.0, 2.0, 100.0 -ti*100.0);\n"
    "        mask += Bokeh(r, p, 0.05, 0.1) *ti *ti *ti;\n"
    "    }\n"
    "    return vec3(1.0, 0.7, 0.3) * mask;\n"
    "  }\n"
    "\n"
    "vec3 EnviromentLights(ray r, float t) {\n"
    "    float side = step(r.d.x, 0.0);\n"
    "    r.d.x = abs(r.d.x);\n"
    "\n"
    "    float s = 0.1;\n"
    "    vec3 color = vec3(0.0);\n"
    "\n"
    "    for (float i = 0.0; i<1.0; i+=s) {\n"
    "        float ti = fract(t + i + side*s*0.5);\n"
    "        vec4 n = Noise4(i+side*100.0);\n"
    "\n"
    "        float fade = ti *ti *ti;\n"
    "        float occlusion = sin(ti*6.28*10.0*n.x) * 0.5 + 0.5;\n"
    "\n"
    "        fade += occlusion;\n"
    "        float x = mix(2.5, 10.0, n.x);\n"
    "        float y = mix(0.1, 1.5, n.y);\n"
    "\n"
    "        vec3 p = vec3(x , y, 50.0 - ti*50.0);\n"
    "        vec3 col = n.wzy;\n"
    "        color += Bokeh(r, p, 0.05, 0.1)* fade *col *0.4;\n"
    "    }\n"
    "    return color;\n"
    "}\n"
    "\n"
    "vec3 HeadLights(ray r, float t) {\n"
    "    t *= 2.0;\n"
    "    float w1 = 0.25;\n"
    "    float w2 = w1*1.2;\n"
    "    float s = 1.0/30.0;\n"
    "    float mask = 0.0;\n"
    "\n"
    "    for (float i = 0.0; i<1.0; i+=s) {\n"
    "        float n = Noise(i);\n"
    "\n"
    "        if(n >0.1) continue;\n"
    "\n"
    "       float ti = fract(t + i);\n"
    "       float z = 100.0 -ti*100.0;\n"
    "       float fade = ti* ti* ti * ti* ti;\n"
    "       float focus = smoothstep(0.9, 1.0, ti);\n"
    "\n"
    "       float size = mix(0.06, 0.03, ti);\n"
    "       mask += Bokeh(r, vec3(-1.0 -w1, 0.15 , z) , size, 0.1) *fade;\n"
    "       mask += Bokeh(r, vec3(-1.0 +w1, 0.15 , z) , size, 0.1) *fade;\n"
    "\n"
    "       mask += Bokeh(r, vec3(-1.0 -w2, 0.15 , z) , size, 0.1) *fade;\n"
    "       mask += Bokeh(r, vec3(-1.0 +w2, 0.15 , z) , size, 0.1) *fade;\n"
    "\n"
    "       float ref = 0.0;\n"
    "       ref += Bokeh(r, vec3(-1.0 -w2, -0.15 , z) , size*3.0, 1.0) *fade;\n"
    "       ref += Bokeh(r, vec3(-1.0 +w2, -0.15 , z) , size*3.0, 1.0) *fade;\n"
    "\n"
    "       mask += ref;\n"
    "    }\n"
    "    return vec3(0.9, 0.9, 1.0) *mask;\n"
    "}\n"
    "\n"
    "vec3 TailLights(ray r, float t) {\n"
    "     t *= 0.25;\n"
    "     float w1 = 0.25;\n"
    "     float w2 = w1*1.2;\n"
    "     float s = 1.0/15.0;\n"
    "     float mask = 0.0;\n"
    "\n"
    "    for (float i = 0.0; i<1.0; i+=s) {\n"
    "        float n = Noise(i);\n"
    "\n"
    "        if(n >0.5) continue;\n"
    "\n"
    "        float lane = step(0.25, n);\n"
    "        float ti = fract(t + i);\n"
    "        float z = 100.0 -ti*100.0;\n"
    "        float fade = ti* ti* ti * ti* ti;\n"
    "        float focus = smoothstep(0.9, 1.0, ti);\n"
    "\n"
    "        float size = mix(0.06, 0.03, ti);\n"
    "        float laneShift = smoothstep(1.0, 0.96, ti);\n"
    "        float x = 1.5 -lane * laneShift;\n"
    "        float blink = step(0.0, sin(t * 1000.0)) *7.0* abs(lane -1.0) *step(0.9, ti);\n"
    "\n"
    "        mask += Bokeh(r, vec3(x -w1, 0.15 , z) , size, 0.1) *fade;\n"
    "        mask += Bokeh(r, vec3(x +w1, 0.15 , z) , size, 0.1) *fade;\n"
    "\n"
    "        mask += Bokeh(r, vec3(x -w2, 0.15 , z) , size, 0.1) *fade;\n"
    "        mask += Bokeh(r, vec3(x +w2, 0.15 , z) , size, 0.1) *fade;\n"
    "\n"
    "        float ref = 0.0;\n"
    "        ref += Bokeh(r, vec3(x -w2, -0.15 , z) , size*3.0, 1.0) *fade;\n"
    "        ref += Bokeh(r, vec3(x +w2, -0.15 , z) , size*3.0, 1.0) *fade;\n"
    "\n"
    "        mask += ref;\n"
    "    }\n"
    "    return vec3(1.0, 0.1, 0.03) *mask;\n"
    "}\n"
    "\n"
    "vec2 Rain(vec2 uv, float t) {\n"
    "     t *= 40.0;\n"
    "     uv.x += uv.y*0.4;\n"
    "     vec2 a = vec2(3.0, 1.0);\n"
    "     vec2 st = uv *a;\n"
    "\n"
    "     vec2 id = floor(st);\n"
    "     st.y += t*0.22;\n"
    "     float n = fract(sin(id.x * 716.34) *768.34);\n"
    "     uv.x += uv.y*n;\n"
    "     st.x += n*0.1;\n"
    "     st.y += n;\n"
    "     uv.y += n;\n"
    "     id = floor(st);\n"
    "     st = fract(st) - 0.5;\n"
    "\n"
    "     t += fract(cos(id.y * 345.6 + id.x *678.7) *234.34) *6.283;\n"
    "     t += fract(sin(id.x * 76.34 + id.y *1453.7) *768.34) *6.283;\n"
    "\n"
    "     float y = - sin(t+sin(t+sin(t) *0.5)) *0.4;\n"
    "     vec2 p1 = vec2(0.0, y);\n"
    "     vec2 o1 = (st - p1)/a;\n"
    "\n"
    "     float d = length(o1);\n"
    "     float mask1 = smoothstep(0.07, 0.0, d);\n"
    "\n"
    "     vec2 o2 = (fract(uv * a.x* vec2(1.0, 2.0)) - 0.5)/vec2(1.0, 2.0);\n"
    "     d = length(o2);\n"
    "\n"
    "     float mask2 = smoothstep(0.3 * (0.5 - st.y), 0.0, d) * smoothstep(-0.1, 0.1, st.y- p1.y);\n"
    "     return vec2(mask1 * o1*30.0 + mask2 *o2*10.0);\n"
    "  }\n"
    "\n"
    "void main() {\n"
    "    vec2 uv = texCoord;\n"
    "    gl_FragColor = texture2D(tex, vec2(uv.x, 1.0 - uv.y));\n"
    "\n"
    "    float t = time * 0.05;\n"
    "    vec3 camPos = vec3(0.5 + sin(t* 20.0)*0.012, 0.2, 0.0);\n"
    "    vec3 lookat = vec3(0.5, 0.2, 1.0);\n"
    "\n"
    "    float zoom = 2.0;\n"
    "\n"
    "    vec2 rainDistort = Rain(uv * 5.0, t)* 0.5;\n"
    "    rainDistort += Rain(uv * 7.0, t)* 0.5;\n"
    "\n"
    "    uv.x += sin(uv.y *70.0) *0.005;\n"
    "    uv.y += sin(uv.x *170.0) *0.003;\n"
    "\n"
    "    ray r = GetRay(uv, camPos, lookat, zoom);\n"
    "    vec3 col = StreetLights(GetRay(uv - rainDistort*0.5, camPos, lookat, zoom), t);\n"
    "    col += HeadLights(GetRay(uv - rainDistort*0.5, camPos, lookat, zoom), t);\n"
    "    col += TailLights(GetRay(uv - rainDistort*0.5, camPos, lookat, zoom), t);\n"
    "    col += EnviromentLights(GetRay(uv - rainDistort*0.5, camPos, lookat, zoom), t);\n"
    "    col += (r.d.y+ 0.25) * vec3(0.2, 0.1, 0.5);\n"
    "\n"
    "    gl_FragColor += vec4(col, 1.0);\n"
    "}\n";

#define PIXEL_FORMAT GL_RGB

typedef struct
{
    const AVClass *class;
    GLuint program;
    GLuint frame_tex;
    GLuint pos_buf;

    GLint time;
    int no_window;

#ifdef GL_TRANSITION_USING_EGL
    EGLDisplay      eglDpy;
    EGLConfig       eglCfg;
    EGLSurface      eglSurf;
    EGLContext      eglCtx;
#else
    GLFWwindow*     window;
#endif
} GlRainLightContext;

#define OFFSET(x) offsetof(GlRainLightContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption glrainlight_options[] = {
    {"nowindow", "ssh mode, no window init open gl context", OFFSET(no_window), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, .flags = FLAGS},
    {NULL}};

AVFILTER_DEFINE_CLASS(glrainlight);

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

static void vbo_setup(GlRainLightContext *gs)
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
    GlRainLightContext *gs = ctx->priv;

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
    GlRainLightContext *gs = ctx->priv;

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
    GlRainLightContext *gs = ctx->priv;

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
    GlRainLightContext *gs = ctx->priv;

    gs->time = glGetUniformLocation(gs->program, "time");
    glUniform1f(gs->time, 0.0f);
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    GlRainLightContext *gs = ctx->priv;

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
    tex_setup(inlink);
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    GlRainLightContext *gs = ctx->priv;

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
    {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    const float time = in->pts * av_q2d(inlink->time_base);
    glUniform1f(gs->time, time);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, inlink->w, inlink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, in->data[0]);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glReadPixels(0, 0, outlink->w, outlink->h, PIXEL_FORMAT, GL_UNSIGNED_BYTE, (GLvoid *)out->data[0]);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    GlRainLightContext *gs = ctx->priv;

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

static const AVFilterPad glrainlight_inputs[] = {
    {.name = "default",
     .type = AVMEDIA_TYPE_VIDEO,
     .config_props = config_props,
     .filter_frame = filter_frame},
    {NULL}};

static const AVFilterPad glrainlight_outputs[] = {
    {.name = "default", .type = AVMEDIA_TYPE_VIDEO}, {NULL}};

AVFilter ff_vf_glrainlight = {
    .name = "glrainlight",
    .description = NULL_IF_CONFIG_SMALL("OpenGL shader filter rain light"),
    .priv_size = sizeof(GlRainLightContext),
    .init = init,
    .uninit = uninit,
    .query_formats = query_formats,
    .inputs = glrainlight_inputs,
    .outputs = glrainlight_outputs,
    .priv_class = &glrainlight_class,
    .flags = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC};
