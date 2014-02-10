#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

char *
strnchr(const char *s, unsigned int len, char c)
{
  for (; len && *s && *s != c; --len, ++s) {}
  return (char *)((len > 0 && *s == c) ? s : 0);
}

#define ISWS(c) ((c) == ' ' || (c) == '\n' || (c) == '\r' || (c) == '\t')

int
nargs(char *s)
{
  int n = 0;
  if (!s)
    return 0;
  while (*s) {
    for (;*s && ISWS(*s); ++s) {}
    if (*s && !ISWS(*s)) {
      ++n;
      for (;*s && !ISWS(*s); ++s) {}
    }
  }
  return n;
}

char *
next_arg(char **s)
{
  char *b, *ret = *s;

  if (ret) {
    for (; *ret && ISWS(*ret); ++ret) {}
    if (!*ret) {
      *s = ret;
      return 0;
    }
    for (b = ret; *b && !ISWS(*b); ++b) {}
    *s = (*b) ? b + 1 : b;
    *b = 0;
  }
  return ret;
}

/* TODO: convert \" into " somehow */
char *
next_quoted_arg(char **s)
{
  char *b, *ret = *s;
  int back = 0;

  if (ret) {
    for (; *ret && ISWS(*ret); ++ret) {}
    if (*ret != '"')
      return next_arg(s);
    ++ret;
    for (b = ret; *b && (*b != '"' || back); ++b)
      back = (*b == '\\' && !back);
    *s = (*b) ? b + 1 : b;
    *b = 0;
  }
  return ret;
}

char *
trim_string_right(char *s, char *chars)
{
  int len = strlen(s), i;
  for (i = len; i >= 0 && strchr(chars, s[i]); --i) {}
  if (i >= 0 && i < len)
    s[i + 1] = 0;
  return s;
}

int
parse_args(char *s, int nargs, char **args)
{
  int i, j, n, pquot;
  char *p;

  for (i = 0; i < nargs; ++i)
    if ((args[i] = next_quoted_arg(&s))) {
      p = args[i];
      n = strlen(p);
      for (j = pquot = 0; p[j]; ++j)
        if (pquot) {
          memmove(p + j - 1, p + j, n - j);
          pquot = 0;
          --n;
          --j;
          p[n] = 0;
        } else if (p[j] == '\\')
          pquot = 1;
    } else
      return i;
  return nargs;
}
