#ifndef GL30_GESTURE_H
#define GL30_GESTURE_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool raw,stable,armed,has_down,pending,second,long_press;
    uint32_t changed,down_at,released,last;
} gl30_gesture;
enum { GL30_GESTURE_NONE,GL30_GESTURE_SINGLE,GL30_GESTURE_DOUBLE,GL30_GESTURE_LONG };
void gl30_gesture_init(gl30_gesture *g,bool held,uint32_t now);
void gl30_gesture_cancel(gl30_gesture *g);
int gl30_gesture_sample(gl30_gesture *g,bool down,uint32_t now);
#endif
