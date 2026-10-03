#include "gl30_gesture.h"
#include <string.h>
void gl30_gesture_init(gl30_gesture *g,bool held,uint32_t now) {
    memset(g,0,sizeof(*g)); g->raw=g->stable=held; g->armed=!held;
    g->changed=g->last=now;
}
void gl30_gesture_cancel(gl30_gesture *g) {
    g->pending=false; g->second=false; g->has_down=false;
    /* An already held key must be released before a new click is accepted. */
    if(g->raw || g->stable) g->armed=false;
}
int gl30_gesture_sample(gl30_gesture *g,bool down,uint32_t now) {
    if(now-g->last>INT32_MAX) return GL30_GESTURE_NONE;
    g->last=now;
    if(down!=g->raw) { g->raw=down; g->changed=now; }
    if(g->raw!=g->stable && now-g->changed>=20) {
        g->stable=g->raw;
        if(g->stable) {
            g->has_down=true; g->down_at=g->changed; g->long_press=false;
            g->second=g->pending && g->down_at-g->released<=300;
        } else {
            if(!g->armed) { g->armed=true; g->has_down=false; g->pending=false; return 0; }
            if(g->has_down && !g->long_press) {
                if(g->changed-g->down_at>=650) { g->pending=g->has_down=g->second=false; return GL30_GESTURE_LONG; }
                if(g->second) { g->pending=g->has_down=g->second=false; return GL30_GESTURE_DOUBLE; }
                g->pending=true; g->released=g->changed;
            }
            g->has_down=false; g->second=false;
        }
    }
    if(g->armed && g->raw && g->stable && g->has_down && !g->long_press && now-g->down_at>=650) {
        g->long_press=true; g->pending=false; return GL30_GESTURE_LONG;
    }
    bool candidate=g->raw && g->pending && g->changed-g->released<=300;
    if(g->pending && !g->second && !candidate && now-g->released>=300) {
        g->pending=false; return GL30_GESTURE_SINGLE;
    }
    return GL30_GESTURE_NONE;
}
