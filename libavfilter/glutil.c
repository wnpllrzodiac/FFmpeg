#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#include <GL/glx.h>
#endif

#include <GLFW/glfw3.h>

// opengl ssh offscreen render
// export DISPLAY=:0.0
// refer to https://blog.csdn.net/defence006/article/details/72674612

typedef GLXContext (*glXCreateContextAttribsARBProc)(
    Display *, GLXFBConfig, GLXContext, Bool, const int *);

#define NUM 1

int no_window_init()
{
    glXCreateContextAttribsARBProc glXCreateContextAttribs = NULL;
    glXCreateContextAttribs = 
        (glXCreateContextAttribsARBProc)glXGetProcAddressARB(
            (const GLubyte *)"glXCreateContextAttribsARB");

    const char *displayName = NULL;
    Display *display;
    display = XOpenDisplay(displayName);

    static int visualAttribs[] = {
        GLX_SAMPLE_BUFFERS, 1, GLX_SAMPLES, 4
    };
    int numberOfFramebufferConfigurations = 0;
    GLXFBConfig *fbConfigs;
    fbConfigs = glXChooseFBConfig(
        display, DefaultScreen(display), visualAttribs, &numberOfFramebufferConfigurations);

    int context_attribs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 0,
        GLX_CONTEXT_FLAGS_ARB, GLX_CONTEXT_DEBUG_BIT_ARB,
        GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        None};
    GLXContext glContext[NUM];
    glContext[0] = glXCreateContextAttribs(display, fbConfigs[0], 0, 1, context_attribs);

    GLXPbuffer pbuffer;
    int pbufferAttribs[] = {
        GLX_PBUFFER_WIDTH, 32,
        GLX_PBUFFER_HEIGHT, 32,
        None
    };
    pbuffer = glXCreatePbuffer(display, fbConfigs[0], pbufferAttribs);
    XFree(fbConfigs);
    XSync(display, False);
    glXMakeContextCurrent(display, pbuffer, pbuffer, glContext[0]);

    return 0;
}