#ifndef GL30_DISPLAY_TRACE_H
#define GL30_DISPLAY_TRACE_H
#include <stdbool.h>
/* UI-owner commands. Recording is silent; dump only after stopping a run. */
void gl30_display_trace_start(void);
void gl30_display_trace_stop(void);
void gl30_display_trace_dump(void);
bool gl30_display_trace_service(void);
#endif
