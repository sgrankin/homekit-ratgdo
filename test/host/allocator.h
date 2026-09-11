#pragma once
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
void *test_malloc(size_t);
void *test_calloc(size_t,size_t);
void test_free(void *);
char *test_strdup(const char *);
#ifdef __cplusplus
}
#endif
#define malloc test_malloc
#define calloc test_calloc
#define free test_free
#define strdup test_strdup
