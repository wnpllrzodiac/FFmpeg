#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#ifndef __ANDROID__
#include <GL/glew.h>
#include <GLFW/glfw3.h>
    #ifndef _WIN32
    #include <GL/glx.h>
    #endif
#endif
#endif

#include <string.h>
#include "glutil.h"

#ifndef __ANDROID__
static char *strsep(char **stringp, const char *delim) {
    char *rv = *stringp;
    if (rv) {
        *stringp += strcspn(*stringp, delim);
        if (**stringp)
            *(*stringp)++ = '\0';
        else
            *stringp = 0; }
    return rv;
}

#ifndef GL_TRANSITION_USING_EGL
#ifdef _WIN32
int no_window_init(void)
{
    return 0;
}
#else
typedef GLXContext (*glXCreateContextAttribsARBProc)(
    Display *, GLXFBConfig, GLXContext, Bool, const int *);

#define NUM 1

int no_window_init(void)
{
    glXCreateContextAttribsARBProc glXCreateContextAttribs = NULL;
    glXCreateContextAttribs = 
        (glXCreateContextAttribsARBProc)glXGetProcAddressARB(
            (const GLubyte *)"glXCreateContextAttribsARB");

    const char *displayName = NULL;
    Display *display;
    display = XOpenDisplay(displayName);
    if (!display) {
        av_log(NULL, AV_LOG_ERROR, "failed to open x display\n");
        return -1;
    }

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
#endif // not win32
#endif
#endif

StringArray_t parseQueryString (const char *str_query)  {
  StringArray_t sa;
  int offset = 0;
  int offset_seg = 0;
  int cursor_start = 0;
  int cursor_end= 0;
  int cnt_seg = 1;
  auto len = (int) (strlen(str_query));

  for (int i = 0; i < len; i++) {
    if (str_query[i] == '=' || str_query[i] == '&') {
      cnt_seg++;
    }
  }
  char **strings = (char **) av_malloc(sizeof(char *) * cnt_seg);
  sa.len = cnt_seg;

  for (; offset <= len; offset++) {
    if (str_query[offset] == '=' || str_query[offset] == '&' || offset == len) {
      int word_len = cursor_end - cursor_start;
      char *seg = (char *) malloc(sizeof(char) * (word_len+ 1));
      strncpy(seg, str_query+cursor_start, (size_t)(word_len));
      seg[word_len] = '\0';
      strings [offset_seg] = seg;
      offset_seg++;
      cursor_end++;
      cursor_start = cursor_end;
    } else {
      cursor_end++;
    }
  }
  sa.strings = strings ;
  return sa;
}

int strToInt(char *to_convert, int *i) {
  char *p = to_convert;
  errno = 0;
  *i = (int) strtol(to_convert, &p, 10);
  if (errno != 0 || to_convert == p || *p != 0) {
    return 0;
  }
  return 1;
}

int strToFloat(char *to_convert, float *f) {
  char *p = to_convert;
  errno = 0;
  *f = strtof(to_convert, &p);
  if (errno != 0 || to_convert == p || *p != 0) {
    return 0;
  }
  return 1;
}

#define STRING_SPLIT_DEFINE(type, Type) \
void strSplit##Type(const char *strLiteral, const char *delimiter, type *pOutputs, int len) { \
  char *token, *str, *tofree;                                                           \
  int offset = 0;                                                                       \
  tofree = str = strdup(strLiteral);                                                    \
  while ((token = strsep(&str, delimiter)) && offset < len) {                           \
    type t;                                                                             \
    if (!strTo##Type(token, &t)) {                                                      \
      t = (type)0;                                                                      \
    }                                                                                   \
    pOutputs[offset] = t;                                                                \
    offset++;                                                                           \
  }                                                                                     \
  free(tofree);                                                                         \
}

STRING_SPLIT_DEFINE(int, Int)
STRING_SPLIT_DEFINE(float, Float)

//assume that the string is compact
#define PARSE_GLSL_VECTOR(glType, typeLen, type, Type)                      \
int parseGlSL##Type##Vector(const char* str,type *pOutput,int *pShape){      \
  int len = (int)strlen(str);                                               \
  int bodyPos = typeLen + 2;                                                \
  if (memcmp(str,glType, typeLen) != 0) {                                   \
    return 0;                                                               \
  }                                                                         \
  *pShape = (int) (str[typeLen]  - '0');                                    \
  int bodyLen = len - bodyPos;                                              \
  char *body = (char *) av_malloc(sizeof(char) * bodyLen);                     \
  strncpy(body, str + bodyPos, (size_t) bodyLen - 1);                       \
  body[bodyLen-1] = '\0';                                                   \
  strSplit##Type(body, ",", pOutput, *pShape);                               \
  av_free(body);                                                               \
  return 1;                                                                 \
}

PARSE_GLSL_VECTOR("vec", 3, float, Float)
PARSE_GLSL_VECTOR("ivec", 4, int, Int)