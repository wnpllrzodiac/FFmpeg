#ifndef _GLU_TIL_H_
#define _GLU_TIL_H_

int no_window_init();

typedef struct {
  char **strings;
  int len;
} StringArray_t;

StringArray_t parseQueryString (const char *str_query);
int strToInt(char *to_convert, int *i);
int strToFloat(char *to_convert, float *f);

#endif // _GLU_TIL_H_