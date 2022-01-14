#ifndef _GL_UTIL_H_
#define _GL_UTIL_H_

#if !defined(__APPLE__) && !defined(_WIN32)
# define GL_TRANSITION_USING_EGL //remove this line if you don't want to use EGL
#endif

#include <libavutil/avutil.h>

#ifdef __ANDROID__
#include <GLES3/gl3.h>
#endif

#ifndef GL_TRANSITION_USING_EGL
int no_window_init(void);
#endif

typedef struct {
  char **strings;
  int len;
} StringArray_t;

StringArray_t parseQueryString (const char *str_query);
int strToInt(char *to_convert, int *i);
int strToFloat(char *to_convert, float *f);

void strSplitInt(const char *strLiteral, const char *delimiter, int *pOutputs, int len);
void strSplitFloat(const char *strLiteral, const char *delimiter, float *pOutputs, int len);

int parseGlSLIntVector(const char* str,int *pOutput,int *pShape);
int parseGlSLFloatVector(const char* str,float *pOutput,int *pShape);

#endif // _GL_UTIL_H_