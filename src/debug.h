#pragma once
#include <stdarg.h>

#if BC_DEBUG > 0
#define BC_LOG(...) bc_debug_log(__VA_ARGS__)
#else
#define BC_LOG(...) ((void)0)
#endif
#if BC_DEBUG > 1
#define BC_LOGV(...) bc_debug_log(__VA_ARGS__)
#else
#define BC_LOGV(...) ((void)0)
#endif

void bc_debug_set_sink(void (*sink)(const char *fmt, ...));
void bc_debug_log(const char *fmt, ...);
