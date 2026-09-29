#include "gl30_model.h"
#include "kk_ui_draw.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int cycle(int value,int count) { int r=value%count; return r<0?r+count:r; }
static int clamp(int value,int lo,int hi) { return value<lo?lo:value>hi?hi:value; }
static void stop(gl30_model *s,int side) { s->endstop=side; s->endstop_until=s->now_ms+450U; }
static const gl30_app setting_apps[]={GL30_TIMER,GL30_VOLUME,GL30_STOPWATCH,GL30_ALARM,GL30_WEATHER,GL30_FEEL,GL30_CALENDAR};
static int setting_app_index(gl30_app app) {
    for(int i=0;i<(int)(sizeof(setting_apps)/sizeof(setting_apps[0]));i++) if(setting_apps[i]==app) return i;
    return 0;
}
static gl30_app cycle_setting_app(gl30_app app,int steps) {
    int count=(int)(sizeof(setting_apps)/sizeof(setting_apps[0]));
    return setting_apps[cycle(setting_app_index(app)+steps,count)];
}
static int cycle_choice(int current,const int *values,int count,int steps) {
    int index=0;
    for(int i=0;i<count;i++) if(values[i]==current) { index=i; break; }
    return values[cycle(index+steps,count)];
}
static void menu_enter(gl30_model *s) {
    s->page=GL30_MENU; s->menu_index=s->app<GL30_MENU_APP_COUNT?s->app:GL30_SETTINGS;
    s->menu_position=s->menu_index; s->menu_visual=(float)s->menu_index;
    s->menu_animating=false; s->motor_menu_valid=false;
    if(++s->menu_epoch==0) ++s->menu_epoch;
}
static void done(gl30_model *s) {
    s->remaining_ms=0; s->phase=GL30_DONE; s->page=GL30_APP;
    s->app=GL30_TIMER; s->menu_index=0; s->light_editing=false; stop(s,-1);
}
void gl30_model_init(gl30_model *s,uint32_t now_ms,uint64_t epoch_seconds) {
    memset(s,0,sizeof(*s)); s->now_ms=now_ms;
    s->epoch_seconds=epoch_seconds<=4102444800ULL?epoch_seconds:0;
    s->volume=42; s->light_brightness=100; s->screen_brightness=75;
    s->language=GL30_LANGUAGE_ENGLISH;
    s->settings_function=GL30_TIMER; s->settings_return_app=GL30_TIMER;
    s->timer_step_minutes=1; s->volume_step_percent=1; s->stopwatch_show_centis=true;
}
static void animate_menu(gl30_model *s,uint32_t now_ms) {
    if(s->menu_animating) {
        uint32_t elapsed=now_ms-s->menu_anim_started;
        uint16_t progress=KK_UI_EaseQ12(elapsed,200);
        s->menu_visual=KK_UI_LerpQ12(s->menu_anim_from_q8,s->menu_anim_to_q8,progress)/256.0f;
        if(elapsed>=200) { s->menu_visual=(float)s->menu_position; s->menu_animating=false; }
    }
}
void gl30_model_tick(gl30_model *s,uint32_t now_ms) {
    uint32_t dt=now_ms-s->now_ms;
    if(dt>INT32_MAX) return; /* Reject backwards timestamps; allow uint32 wrap. */
    s->now_ms=now_ms; s->elapsed_ms+=dt;
    if(s->stopwatch_running) s->stopwatch_ms+=dt;
    if(s->phase==GL30_RUNNING) {
        if(dt>=s->remaining_ms) done(s); else s->remaining_ms-=dt;
    }
    if(s->endstop && (int32_t)(now_ms-s->endstop_until)>=0) s->endstop=0;
    animate_menu(s,now_ms);
}
void gl30_model_view(const gl30_model *s,uint32_t display_ms,gl30_model *view) {
    uint32_t dt=display_ms-s->now_ms;
    *view=*s;
    if(dt>INT32_MAX) return;
    view->now_ms=display_ms; view->elapsed_ms+=dt;
    if(view->stopwatch_running) view->stopwatch_ms+=dt;
    if(view->phase==GL30_RUNNING)
        view->remaining_ms=dt>=view->remaining_ms?0:view->remaining_ms-dt;
    /* Only software easing advances. Motor q/f remains the captured sample. */
    animate_menu(view,display_ms);
}
bool gl30_model_motor_menu(gl30_model *s,uint32_t epoch,uint32_t session,int32_t q,float fraction,bool valid) {
    if(s->page!=GL30_MENU || s->off || s->fault || epoch!=s->menu_epoch) return false;
    if(!valid) { s->motor_menu_valid=false; return false; }
    if(!isfinite(fraction) || fabsf(fraction)>0.56f) return false;
    if(!session) return false;
    if(s->motor_menu_session!=session) {
        s->motor_menu_origin=s->menu_index; s->motor_menu_session=session;
    }
    int index=cycle(s->motor_menu_origin+cycle(q,GL30_MENU_APP_COUNT),GL30_MENU_APP_COUNT);
    float visual=(float)index+fraction;
    bool changed=!s->motor_menu_valid || index!=s->menu_index || visual!=s->menu_visual;
    s->motor_menu_valid=true; s->menu_animating=false;
    s->menu_index=index; s->menu_position=index; s->menu_visual=visual;
    return changed;
}
void gl30_model_rotate(gl30_model *s,int steps) {
    if(!steps || s->off || s->fault || s->page==GL30_HOME) return;
    steps=clamp(steps,-32768,32767);
    if(s->page==GL30_MENU) {
        if(s->motor_menu_valid) return;
        s->menu_index=cycle(s->menu_index+steps,GL30_MENU_APP_COUNT);
        s->menu_position+=steps;
        /* Rebase together before float loses fractional animation precision. */
        if(s->menu_position>9000 || s->menu_position< -9000) {
            int32_t shift=s->menu_position-cycle(s->menu_position,GL30_MENU_APP_COUNT);
            s->menu_position-=shift; s->menu_visual-=(float)shift;
        }
        /* Coalesced input may skip complete revolutions; never interpolate a
         * huge historical backlog or convert an unbounded angle to Q8. */
        float gap=(float)s->menu_position-s->menu_visual;
        if(fabsf(gap)>2*GL30_MENU_APP_COUNT) s->menu_visual+=GL30_MENU_APP_COUNT*truncf(gap/GL30_MENU_APP_COUNT);
        s->menu_anim_from_q8=(int32_t)lroundf(s->menu_visual*256);
        s->menu_anim_to_q8=s->menu_position*256;
        s->menu_anim_started=s->now_ms; s->menu_animating=true;
        return;
    }
    if(s->app==GL30_SETTINGS) {
        if(s->settings_page==GL30_SETTINGS_ROOT) {
            s->settings_item=cycle(s->settings_item+steps,5); return;
        }
        if(s->settings_page==GL30_SETTINGS_DISPLAY) {
            s->screen_brightness=clamp(s->screen_brightness+steps*5,10,100); return;
        }
        if(s->settings_page==GL30_SETTINGS_LANGUAGE) {
            if(steps&1) s->language=s->language==GL30_LANGUAGE_ENGLISH?GL30_LANGUAGE_CHINESE:GL30_LANGUAGE_ENGLISH;
            return;
        }
        if(s->settings_page==GL30_SETTINGS_LIGHT) {
            if(!s->light_editing) s->light_field=cycle(s->light_field+steps,3);
            else if(s->light_field==0) s->light_effect=cycle(s->light_effect+steps,4);
            else if(s->light_field==1) s->light_color=cycle(s->light_color+steps,6);
            else s->light_brightness=clamp(s->light_brightness+steps*5,0,100);
            return;
        }
        if(s->settings_page==GL30_SETTINGS_HELP) {
            s->settings_item=cycle(s->settings_item+steps,4); return;
        }
        if(s->settings_page==GL30_SETTINGS_FUNCTION) {
            if(!s->settings_editing) { s->settings_function=cycle_setting_app(s->settings_function,steps); return; }
            static const int steps3[]={1,5,10};
            switch(s->settings_function) {
            case GL30_TIMER: s->timer_step_minutes=cycle_choice(s->timer_step_minutes,steps3,3,steps); break;
            case GL30_VOLUME: s->volume_step_percent=cycle_choice(s->volume_step_percent,steps3,3,steps); break;
            case GL30_STOPWATCH: if(steps&1) s->stopwatch_show_centis=!s->stopwatch_show_centis; break;
            case GL30_ALARM: if(steps&1) s->alarm_enabled=!s->alarm_enabled; break;
            case GL30_WEATHER: if(steps&1) s->weather_fahrenheit=!s->weather_fahrenheit; break;
            case GL30_FEEL: s->feel_profile=cycle(s->feel_profile+steps,3); break;
            case GL30_CALENDAR: if(steps&1) s->calendar_monday_first=!s->calendar_monday_first; break;
            default: break;
            }
            return;
        }
    }
    if(s->app==GL30_LIGHTING) {
        if(!s->light_editing) s->light_field=cycle(s->light_field+steps,3);
        else if(s->light_field==0) s->light_effect=cycle(s->light_effect+steps,4);
        else if(s->light_field==1) s->light_color=cycle(s->light_color+steps,6);
        else s->light_brightness=clamp(s->light_brightness+steps*5,0,100);
        return;
    }
    if(s->app==GL30_VOLUME) {
        int before=s->volume, wanted=before+steps*s->volume_step_percent;
        s->volume=clamp(wanted,0,100); s->muted=false;
        if(wanted<0 || (wanted==0 && before>0)) stop(s,-1);
        else if(wanted>100 || (wanted==100 && before<100)) stop(s,1);
        else s->endstop=0;
        return;
    }
    if(s->app!=GL30_TIMER) return;
    int64_t requested=(int64_t)s->remaining_ms+(int64_t)steps*s->timer_step_minutes*60000;
    uint32_t value=(uint32_t)(requested<0?0:requested>GL30_TIMER_MAX_MS?GL30_TIMER_MAX_MS:requested);
    if(requested<=0) stop(s,-1);
    else if(requested>=GL30_TIMER_MAX_MS) stop(s,1);
    else s->endstop=0;
    if(s->phase==GL30_RUNNING || s->phase==GL30_PAUSED) {
        s->remaining_ms=value;
        if(value>s->duration_ms) s->duration_ms=value;
        if(value==0) done(s);
    } else if(value!=s->remaining_ms) {
        s->duration_ms=s->remaining_ms=value; s->phase=GL30_SETTING;
    }
}
void gl30_model_open(gl30_model *s,gl30_app app) {
    if(s->off) { s->off=false; return; }
    if((unsigned)app>=GL30_APP_COUNT) return;
    s->app=app; s->page=GL30_APP; s->menu_index=app;
    s->motor_menu_valid=false; s->menu_animating=false;
    s->light_editing=false; s->settings_editing=false; s->endstop=0;
    if(app==GL30_SETTINGS) {
        s->settings_page=GL30_SETTINGS_ROOT; s->settings_item=0; s->settings_from_app=false;
    }
}
void gl30_model_quick_settings(gl30_model *s) {
    if(s->off || s->fault) return;
    if(s->page!=GL30_APP) {
        gl30_model_open(s,GL30_SETTINGS); return;
    }
    if(s->app==GL30_SETTINGS) return;
    gl30_app target=s->app;
    if(target==GL30_LIGHTING) target=GL30_TIMER;
    s->settings_return_app=s->app;
    s->settings_function=cycle_setting_app(target,0);
    s->settings_page=GL30_SETTINGS_FUNCTION; s->settings_editing=false;
    s->settings_from_app=true; s->app=GL30_SETTINGS; s->page=GL30_APP;
    s->motor_menu_valid=false; s->menu_animating=false; s->endstop=0;
}
void gl30_model_primary(gl30_model *s) {
    if(s->off) { s->off=false; return; }
    if(s->page==GL30_HOME) {
        menu_enter(s); return;
    }
    if(s->page==GL30_MENU) { gl30_model_open(s,(gl30_app)s->menu_index); return; }
    if(s->app==GL30_TIMER) {
        if(s->phase==GL30_RUNNING) s->phase=GL30_PAUSED;
        else if(s->phase==GL30_DONE) gl30_model_reset(s);
        else if(!s->fault && s->remaining_ms) s->phase=GL30_RUNNING;
    } else if(s->app==GL30_STOPWATCH) {
        if(s->stopwatch_running) s->stopwatch_running=false;
        else if(!s->fault) s->stopwatch_running=true;
    } else if(s->app==GL30_VOLUME && !s->fault) s->muted=!s->muted;
    else if(s->app==GL30_LIGHTING && !s->fault) s->light_editing=!s->light_editing;
    else if(s->app==GL30_SETTINGS) {
        if(s->settings_page==GL30_SETTINGS_ROOT) {
            static const gl30_settings_page pages[]={GL30_SETTINGS_DISPLAY,GL30_SETTINGS_LIGHT,GL30_SETTINGS_LANGUAGE,GL30_SETTINGS_HELP,GL30_SETTINGS_FUNCTION};
            s->settings_page=pages[s->settings_item];
            s->settings_editing=false; s->light_editing=false;
            if(s->settings_page==GL30_SETTINGS_FUNCTION) s->settings_function=GL30_TIMER;
        } else if(s->settings_page==GL30_SETTINGS_LIGHT) s->light_editing=!s->light_editing;
        else if(s->settings_page==GL30_SETTINGS_FUNCTION) s->settings_editing=!s->settings_editing;
        else if(s->settings_page==GL30_SETTINGS_DISPLAY || s->settings_page==GL30_SETTINGS_LANGUAGE || s->settings_page==GL30_SETTINGS_HELP) {
            s->settings_page=GL30_SETTINGS_ROOT; s->settings_item=0;
        }
    }
}
void gl30_model_back(gl30_model *s) {
    if(s->off) { s->off=false; return; }
    if(s->page==GL30_APP && s->app==GL30_LIGHTING && s->light_editing) { s->light_editing=false; return; }
    if(s->page==GL30_APP && s->app==GL30_SETTINGS) {
        if(s->settings_page==GL30_SETTINGS_LIGHT && s->light_editing) { s->light_editing=false; return; }
        if(s->settings_page==GL30_SETTINGS_FUNCTION && s->settings_editing) { s->settings_editing=false; return; }
        if(s->settings_page!=GL30_SETTINGS_ROOT) {
            if(s->settings_from_app && s->settings_page==GL30_SETTINGS_FUNCTION) {
                gl30_app back=s->settings_return_app; s->settings_from_app=false; gl30_model_open(s,back); return;
            }
            s->settings_page=GL30_SETTINGS_ROOT; s->settings_item=0; return;
        }
    }
    if(s->page==GL30_APP) {
        menu_enter(s);
    } else { s->page=GL30_HOME; s->motor_menu_valid=false; s->menu_animating=false; }
    s->endstop=0;
}
void gl30_model_power(gl30_model *s) { s->off=!s->off; s->endstop=0; }
void gl30_model_fault(gl30_model *s,bool fault) {
    s->fault=fault; s->endstop=0;
    if(fault) { if(s->phase==GL30_RUNNING) s->phase=GL30_PAUSED; s->stopwatch_running=false; }
}
void gl30_model_reset(gl30_model *s) {
    if(s->app==GL30_STOPWATCH) { s->stopwatch_ms=0; s->stopwatch_running=false; }
    else if(s->app==GL30_TIMER) { s->phase=GL30_SETTING; s->remaining_ms=s->duration_ms=0; s->endstop=0; }
}
float gl30_model_angle(const gl30_model *s) {
    if(s->page==GL30_HOME) return 0;
    if(s->page==GL30_MENU) return s->menu_visual*45.0f;
    if(s->app==GL30_TIMER) return s->remaining_ms/10000.0f;
    if(s->app==GL30_VOLUME) return s->volume*3.6f;
    if(s->app==GL30_SETTINGS) {
        if(s->settings_page==GL30_SETTINGS_ROOT) return s->settings_item*72.0f;
        if(s->settings_page==GL30_SETTINGS_LANGUAGE) return s->language==GL30_LANGUAGE_ENGLISH?0.0f:180.0f;
        if(s->settings_page==GL30_SETTINGS_LIGHT) return s->light_editing?
            (s->light_field==0?s->light_effect*90.0f:s->light_field==1?s->light_color*60.0f:s->light_brightness*3.6f):s->light_field*120.0f;
        if(s->settings_page==GL30_SETTINGS_FUNCTION) return setting_app_index(s->settings_function)*51.4f;
    }
    if(s->app==GL30_LIGHTING) return s->light_editing?
        (s->light_field==0?s->light_effect*90.0f:s->light_field==1?s->light_color*60.0f:s->light_brightness*3.6f):s->light_field*120.0f;
    return s->app*40.0f;
}
void gl30_format_time(uint64_t ms,bool ceil_seconds,char *out,unsigned capacity) {
    uint64_t sec=ms/1000+(ceil_seconds && ms%1000!=0);
    snprintf(out,capacity,"%02llu:%02u:%02u",(unsigned long long)(sec/3600),(unsigned)(sec/60%60),(unsigned)(sec%60));
}
void gl30_model_leds(const gl30_model *s,uint8_t rgb[24][3]) {
    static const uint8_t colors[6][3]={{255,155,81},{255,209,102},{145,219,159},{194,155,255},{243,144,182},{229,233,154}};
    memset(rgb,0,24*3);
    if(s->off || s->fault || !s->light_brightness) return;
    float angle=gl30_model_angle(s), wrapped=fmodf(angle+36000,360);
    unsigned head=(unsigned)lroundf(wrapped/15.0f)%24;
    float intensity=s->light_brightness/100.0f;
    bool adjustable=s->page==GL30_APP && (s->app==GL30_TIMER || s->app==GL30_VOLUME);
    if(s->page!=GL30_MENU && !adjustable && s->light_effect!=0) {
        if(s->light_effect==2) intensity*=0.25f+0.375f*(1-cosf((s->now_ms%4000)*6.2831853f/4000));
        unsigned flow=(s->now_ms/125)%24; head=flow;
        for(unsigned i=0;i<24;i++) if(s->light_effect!=3 || (flow+24-i)%24<6)
            for(unsigned c=0;c<3;c++) rgb[i][c]=(uint8_t)(colors[s->light_color][c]*intensity);
    } else if(adjustable) {
        float turns=angle/360.0f;
        int ring=clamp((int)ceilf(turns)-1,0,5);
        float fill=turns-ring;
        for(unsigned i=0;i<24;i++) if(i/24.0f<fill)
            for(unsigned c=0;c<3;c++) rgb[i][c]=(uint8_t)(colors[ring][c]*intensity);
    }
    /* Exactly one leading emitter; diffuser appearance is a separate optical issue. */
    rgb[head][0]=(uint8_t)(85*intensity); rgb[head][1]=(uint8_t)(201*intensity); rgb[head][2]=(uint8_t)(255*intensity);
}
