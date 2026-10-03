#include "gl30_demo.h"
#include "gl30_gesture.h"
#include "kk_ui.h"
#include "kk_oled.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef GL30_MENU_CACHE
#define GL30_MENU_CACHE 1
#endif

extern const uint8_t gl30_font_body[],gl30_font_title[];
static gl30_model state,render_view;
static gl30_gesture gesture;
static bool raw_button,display_off,initialized,touch_down,touch_drag;
static bool redraw,known_dirty;
static bool maintenance;
static uint8_t menu_full_refresh,menu_label_refresh;
static gl30_page menu_last_page;
static int menu_last_index;
static gl30_motor_feedback_state menu_last_motor;
static int16_t rotation_queue;
static int last_contrast;
static int display_error;
static float touch_angle,touch_travel,touch_x,touch_y,touch_origin_x,touch_origin_y;
static gl30_demo_metrics metrics;
static gl30_frame_metrics frame_metrics;
static OLED_Metrics update_oled;
static uint64_t update_started,frame_compare_baseline;
static uint32_t update_history[128];
static const KK_UI_PageRoute routes[]={{KK_UI_PAGE_CUSTOM,0}};
static KK_UI_App application={
    .root_page=1,.routes=routes,.route_count=1,.custom_page_count=1,
    .fonts={gl30_font_title,gl30_font_title,gl30_font_body},
    .texts={"Back","Cancel","OK","On","Off","Notice"}
};
static int ui_language=-1;
static void sync_ui_language(void) {
    if(ui_language==(int)state.language) return;
    if(state.language==GL30_LANGUAGE_CHINESE) {
        application.texts=(KK_UI_Texts){"返回","取消","确认","开启","关闭","提示"};
    } else {
        application.texts=(KK_UI_Texts){"Back","Cancel","OK","On","Off","Notice"};
    }
    ui_language=(int)state.language;
}

bool gl30_demo_init(uint32_t now_ms,uint64_t unix_seconds) {
    initialized=false; maintenance=false; raw_button=display_off=touch_down=touch_drag=false;
    rotation_queue=0; last_contrast=-1; display_error=0; redraw=true; known_dirty=true;
    menu_full_refresh=menu_label_refresh=0; menu_last_page=GL30_HOME; menu_last_index=-1; ui_language=-1;
    menu_last_motor=GL30_MOTOR_OFFLINE;
    memset(&metrics,0,sizeof(metrics)); memset(update_history,0,sizeof(update_history));
    memset(&frame_metrics,0,sizeof(frame_metrics));
    gl30_model_init(&state,now_ms,unix_seconds);
    sync_ui_language();
    gl30_gesture_init(&gesture,false,now_ms);
    if(OLED_Init()!=OLED_OK) return false;
    OLED_SetColor(0xffff,0); OLED_SetFont(gl30_font_body);
    if(KK_UI_Init(&application)!=KK_UI_OK) return false;
    /* The first render receives a fresh timestamp from the owner after the
     * hardware init delays; it must not start a transfer with the boot epoch. */
    initialized=true; return true;
}
bool gl30_demo_apply_preferences(const gl30_preferences *preferences) {
    if(!initialized || !gl30_preferences_apply(&state,preferences)) return false;
    sync_ui_language(); redraw=known_dirty=true; KK_UI_Invalidate();
    return true;
}
void gl30_demo_maintenance(bool active) {
    maintenance=active;
    if(!active) { redraw=known_dirty=true; KK_UI_Invalidate(); }
}

/* All moving symbols and their orbit stay within y=56..366. Keep this whole
 * band in the retire-clear region so cached framebuffers cannot retain ghosts.
 * The selected title below the band is refreshed in every physical buffer. */
#define GL30_MENU_DYNAMIC_Y0 56U
#define GL30_MENU_DYNAMIC_Y1 366U
#define GL30_MENU_LABEL_Y1 430U

static void prepare_menu_raster(void) {
#if !GL30_MENU_CACHE
    OLED_SetClearRows(0U,OLED_GetHeight());
    gl30_render_set_menu_mode(0U);
    return;
#endif
    if(state.page!=GL30_MENU) {
        menu_full_refresh=menu_label_refresh=0;
        menu_last_page=state.page; menu_last_index=state.menu_index;
        OLED_SetClearRows(0U,OLED_GetHeight());
        gl30_render_set_menu_mode(0U);
        return;
    }
    if(menu_last_page!=GL30_MENU || menu_last_motor!=state.motor_state) {
        /* Prime every physical framebuffer with persistent top/bottom chrome. */
        menu_full_refresh=OLED_GetFrameBufferCount(); menu_label_refresh=0U;
    } else if(menu_last_index!=state.menu_index) {
        if(menu_full_refresh!=0U) {
            /* One physical buffer may still contain chrome for the previous
             * index. Restart priming so every buffer gets the new title. */
            menu_full_refresh=OLED_GetFrameBufferCount(); menu_label_refresh=0U;
        } else {
            /* The selected title lives below the carousel band. Refresh it
             * once in each framebuffer, then preserve it like other chrome. */
            menu_label_refresh=OLED_GetFrameBufferCount();
        }
    }
    menu_last_page=state.page; menu_last_index=state.menu_index;
    menu_last_motor=state.motor_state;
    if(menu_full_refresh!=0U) {
        OLED_SetClearRows(0U,OLED_GetHeight());
        gl30_render_set_menu_mode(0U);
    } else if(menu_label_refresh!=0U) {
        OLED_SetClearRows(GL30_MENU_DYNAMIC_Y0,GL30_MENU_LABEL_Y1);
        gl30_render_set_menu_mode(1U);
    } else {
        OLED_SetClearRows(GL30_MENU_DYNAMIC_Y0,GL30_MENU_DYNAMIC_Y1);
        gl30_render_set_menu_mode(2U);
    }
}

static void finish_menu_raster(void) {
#if !GL30_MENU_CACHE
    OLED_SetRetireClearRows(0U,OLED_GetHeight());
    OLED_SetClearRows(0U,OLED_GetHeight());
    return;
#endif
    if(render_view.page!=GL30_MENU) {
        OLED_SetRetireClearRows(0U,OLED_GetHeight());
        OLED_SetClearRows(0U,OLED_GetHeight());
        return;
    }
    /* Every menu framebuffer owns persistent chrome. Regardless of whether
     * this particular pass was a full prime or label refresh, retirement only
     * erases the animated carousel band. A later pre-draw policy widens the
     * clear if the selected label or whole page must change. */
    OLED_SetRetireClearRows(GL30_MENU_DYNAMIC_Y0,GL30_MENU_DYNAMIC_Y1);
    if(menu_full_refresh!=0U) --menu_full_refresh;
    else if(menu_label_refresh!=0U) --menu_label_refresh;

    /* This policy is consumed immediately if the same KK_UI_Update submits
     * and swaps buffers after CustomOnDraw. It therefore prepares the next
     * draw buffer, not the frame just rendered. */
    if(menu_full_refresh!=0U) OLED_SetClearRows(0U,OLED_GetHeight());
    else if(menu_label_refresh!=0U) OLED_SetClearRows(GL30_MENU_DYNAMIC_Y0,GL30_MENU_LABEL_Y1);
    else OLED_SetClearRows(GL30_MENU_DYNAMIC_Y0,GL30_MENU_DYNAMIC_Y1);
}
static bool sample_input(uint32_t now_ms) {
    if(!initialized || now_ms-state.now_ms>INT32_MAX) return false;
    gl30_phase old_phase=state.phase;
    int old_endstop=state.endstop;
    float old_menu_visual=state.menu_visual;
    uint64_t old_second=state.elapsed_ms/1000;
    uint32_t old_remaining_second=(state.remaining_ms+999)/1000;
    gl30_model_tick(&state,now_ms);
    if(old_phase!=state.phase || old_endstop!=state.endstop ||
       (state.page==GL30_MENU && old_menu_visual!=state.menu_visual) ||
       ((state.page==GL30_HOME || (state.page==GL30_APP && state.app==GL30_CALENDAR)) &&
        old_second!=state.elapsed_ms/1000) ||
       (state.page==GL30_HOME && old_remaining_second!=(state.remaining_ms+999)/1000))
        redraw=true;
    if(old_phase==GL30_RUNNING && state.phase==GL30_DONE) {
        gl30_gesture_cancel(&gesture); rotation_queue=0;
    }
    /* Drain each input event in timestamp order before a later click can
     * change pages. Rendering is scheduled separately from input sampling. */
    if(rotation_queue) {
        gl30_model_rotate(&state,rotation_queue);
        rotation_queue=0; redraw=true;
    }
    int event=gl30_gesture_sample(&gesture,raw_button,now_ms);
    if(event==GL30_GESTURE_SINGLE) gl30_model_primary(&state);
    if(event==GL30_GESTURE_DOUBLE) gl30_model_back(&state);
    if(event==GL30_GESTURE_LONG) gl30_model_quick_settings(&state);
    if(event==GL30_GESTURE_SINGLE || event==GL30_GESTURE_DOUBLE || event==GL30_GESTURE_LONG) redraw=true;
    return true;
}
void gl30_demo_sample(uint32_t now_ms) { (void)sample_input(now_ms); }
bool gl30_demo_animation_active(void) {
    return (state.page==GL30_MENU && state.menu_animating) || (state.page==GL30_APP &&
        ((state.app==GL30_TIMER && state.phase==GL30_RUNNING) ||
         (state.app==GL30_STOPWATCH && state.stopwatch_running) ||
         (state.app==GL30_LIGHTING && state.light_effect>=2) ||
         (state.app==GL30_SETTINGS && state.settings_page==GL30_SETTINGS_LIGHT && state.light_effect>=2)));
}
void gl30_demo_service_display(uint32_t now_ms) {
    if(!initialized) return;
    gl30_platform_display_poll();
    /* A failed transfer must not be overwritten by a later control's OK. */
    OLED_Status transfer_status=OLED_GetLastStatus();
    if(transfer_status!=OLED_OK && transfer_status!=OLED_BUSY) {
        display_error=transfer_status; return;
    }
    if(maintenance) return;
    /* Display controls have priority over a prepared raster. Otherwise a DMA
     * completion wake could immediately launch another frame and make a
     * pending power/contrast command BUSY for an extra cycle. */
    if(state.off || display_off!=state.off || last_contrast!=state.screen_brightness) return;
    KK_UI_Status result=KK_UI_ServiceDisplay(now_ms);
    if(result!=KK_UI_OK && result!=KK_UI_BUSY) display_error=result;
    transfer_status=OLED_GetLastStatus();
    if(transfer_status!=OLED_OK && transfer_status!=OLED_BUSY) display_error=transfer_status;
}
void gl30_demo_render(uint32_t now_ms) {
    if(!initialized) return;
    gl30_demo_service_display(now_ms);
    if(maintenance) return;
    if(display_error) return;
    sync_ui_language();
    gl30_model_view(&state,now_ms,&render_view);
    prepare_menu_raster();
    if(display_off!=state.off) {
        OLED_Status result=OLED_SetPowerSave(state.off);
        if(result!=OLED_OK && result!=OLED_BUSY) display_error=result;
        else if(result==OLED_OK) { display_off=state.off; if(!display_off) KK_UI_Invalidate(); }
    }
    if(state.off) { rotation_queue=0; return; }
    if(last_contrast!=state.screen_brightness) {
        OLED_Status result=OLED_SetContrast((uint8_t)(state.screen_brightness*255/100));
        if(result==OLED_OK) last_contrast=state.screen_brightness;
        else if(result!=OLED_BUSY) display_error=result;
    }
    if(display_error) return;
    KK_UI_Input input={0,0};
    uint32_t before=metrics.frames;
    uint64_t started=gl30_platform_time_us();
    update_started=started; OLED_GetMetrics(&update_oled);
    KK_UI_Status result=KK_UI_Update(now_ms,input);
    if(metrics.frames!=before) {
        metrics.last_update_us=(uint32_t)(gl30_platform_time_us()-started);
        update_history[(metrics.frames-1)%128]=metrics.last_update_us;
        if(metrics.last_update_us>metrics.max_update_us) metrics.max_update_us=metrics.last_update_us;
    }
    if(result!=KK_UI_OK && result!=KK_UI_BUSY) display_error=result;
    OLED_Status transfer_status=OLED_GetLastStatus();
    if(transfer_status!=OLED_OK && transfer_status!=OLED_BUSY) display_error=transfer_status;
}
void gl30_demo_motor_menu(uint32_t epoch,uint32_t session,int32_t q,float fraction,bool valid,uint32_t now_ms) {
    if(!initialized || now_ms-state.now_ms>INT32_MAX) return;
    int before=state.menu_index;
    if(gl30_model_motor_menu(&state,epoch,session,q,fraction,valid)) redraw=true;
    if(state.menu_index!=before) gl30_gesture_cancel(&gesture);
}
void gl30_demo_motor_feedback(gl30_motor_feedback_state motor_state,uint32_t fault_bits) {
    if(!initialized) return;
    if((unsigned)motor_state>GL30_MOTOR_INVALID) motor_state=GL30_MOTOR_INVALID;
    state.motor_fault_bits=fault_bits;
    if(state.motor_state==motor_state) return;
    state.motor_state=motor_state;
    known_dirty=true; KK_UI_Invalidate();
}
void gl30_demo_rotate(int16_t steps) {
    if(!steps) return;
    gl30_gesture_cancel(&gesture);
    int32_t total=(int32_t)rotation_queue+steps;
    rotation_queue=(int16_t)(total>32767?32767:total< -32768?-32768:total);
}
void gl30_demo_button(bool pressed,uint32_t now_ms) {
    if(!initialized || now_ms-state.now_ms>INT32_MAX) return;
    /* Publish the new raw sample before testing the inclusive double window. */
    raw_button=pressed; sample_input(now_ms);
}
void gl30_demo_cancel_input(void) {
    raw_button=false; rotation_queue=0; touch_down=false; touch_drag=false;
    gl30_gesture_init(&gesture,false,state.now_ms);
}
void gl30_demo_touch(int16_t x,int16_t y,bool pressed,uint32_t now_ms) {
    if(!initialized || now_ms-state.now_ms>INT32_MAX) return;
    float angle=atan2f((float)x-233,233.0f-y)*57.2957795f;
    if(pressed && !touch_down) {
        touch_down=true; touch_drag=false; touch_angle=angle; touch_travel=0;
        touch_x=touch_origin_x=(float)x; touch_y=touch_origin_y=(float)y;
        gl30_demo_button(true,now_ms); return;
    }
    if(pressed && touch_down) {
        float dx=x-touch_origin_x,dy=y-touch_origin_y;
        if(dx*dx+dy*dy>144) { touch_drag=true; gl30_gesture_cancel(&gesture); }
        if(touch_drag) {
            float radius2=(touch_origin_x-233)*(touch_origin_x-233)+(touch_origin_y-233)*(touch_origin_y-233);
            float delta=angle-touch_angle;
            if(delta>180) delta-=360;
            if(delta< -180) delta+=360;
            if(radius2<145*145) delta=(x-touch_x)*0.7f;
            float step=state.page==GL30_MENU?45.0f:state.app==GL30_TIMER?6.0f:state.app==GL30_VOLUME?3.6f:30.0f;
            if(state.app==GL30_SETTINGS && state.settings_page==GL30_SETTINGS_ROOT) step=72.0f;
            if(state.app==GL30_SETTINGS && state.settings_page==GL30_SETTINGS_LANGUAGE) step=180.0f;
            if(state.app==GL30_SETTINGS && state.settings_page==GL30_SETTINGS_DISPLAY) step=3.6f;
            touch_travel+=delta;
            int count=(int)(touch_travel/step);
            if(count) { gl30_demo_rotate((int16_t)count); touch_travel-=count*step; }
        }
        touch_x=(float)x; touch_y=(float)y; touch_angle=angle;
    } else if(!pressed && touch_down) {
        touch_down=false; if(touch_drag) gl30_gesture_cancel(&gesture);
        gl30_demo_button(false,now_ms);
    }
    sample_input(now_ms);
}
void gl30_demo_shortcut(int command) {
    gl30_gesture_cancel(&gesture); rotation_queue=0;
    if(command==0) gl30_model_open(&state,GL30_TIMER);
    else if(command==1) gl30_model_open(&state,GL30_VOLUME);
    else if(command==2) gl30_model_reset(&state);
    else if(command==3) gl30_model_power(&state);
    /* Power control already restores the stable frame. Its wake repaint may
     * be pixel-identical, so keep default deduplication for that request. */
    if(command>=0 && command<=2) known_dirty=true;
    KK_UI_Invalidate();
}
void gl30_demo_set_clock(uint64_t unix_seconds) {
    if(unix_seconds>4102444800ULL) return;
    state.epoch_seconds=unix_seconds; state.elapsed_ms=0; known_dirty=true; KK_UI_Invalidate();
}
void gl30_demo_set_fault(bool fault) {
    gl30_gesture_cancel(&gesture); gl30_model_fault(&state,fault); redraw=true;
}
const gl30_model *gl30_demo_state(void) { return &state; }
void KK_UI_CustomOnEnter(KK_UI_PageId page) { (void)page; }
void KK_UI_CustomOnLeave(KK_UI_PageId page) { (void)page; gl30_gesture_cancel(&gesture); }
void KK_UI_CustomOnInput(KK_UI_PageId page,KK_UI_InputEvent input) {
    (void)page;
    if(input.action!=KK_UI_INPUT_OK)
        gl30_model_rotate(&state,input.action==KK_UI_INPUT_UP?-(int)input.steps:(int)input.steps);
}
bool KK_UI_CustomOnTick(KK_UI_PageId page,uint32_t now_ms) {
    (void)page; (void)now_ms;
    bool changed=redraw || gl30_demo_animation_active(); redraw=false;
    /* Preserve the reason until an actual draw, not merely this 100 Hz tick. */
    if(!state.off && changed) known_dirty=true;
    return !state.off && changed;
}
void KK_UI_CustomOnDraw(KK_UI_PageId page,int16_t offset,int16_t clip_x,uint16_t width) {
    (void)page; uint64_t started=gl30_platform_time_us();
    OLED_Metrics oled; OLED_GetMetrics(&oled);
    frame_metrics=(gl30_frame_metrics){.frame_id=metrics.frames+1,
        .capture_ms=state.now_ms,.render_ms=render_view.now_ms,
        .begin_us=update_started,.draw_begin_us=started,
        .clear_us=(uint32_t)(oled.clear_us-update_oled.clear_us)};
    frame_compare_baseline=update_oled.compare_us;
    gl30_draw_scene(&render_view,offset,clip_x,width);
    finish_menu_raster();
    if(known_dirty) OLED_MarkFrameChanged();
    known_dirty=false;
    frame_metrics.draw_end_us=gl30_platform_time_us();
    metrics.last_draw_us=(uint32_t)(frame_metrics.draw_end_us-started); metrics.frames++;
}
void gl30_demo_get_frame_metrics(gl30_frame_metrics *out) {
    if(!out) return;
    OLED_Metrics oled; OLED_GetMetrics(&oled); *out=frame_metrics;
    out->compare_us=(uint32_t)(oled.compare_us-frame_compare_baseline);
}
void gl30_demo_get_metrics(gl30_demo_metrics *out) {
    if(!out) return;
    *out=metrics;
    /* Called only by the UI owner; keep the diagnostic sort off its stack. */
    static uint32_t sorted[128];
    uint32_t n=metrics.frames<128?metrics.frames:128;
    for(uint32_t i=0;i<n;i++) {
        uint32_t v=update_history[i],j=i;
        while(j && sorted[j-1]>v) { sorted[j]=sorted[j-1]; j--; }
        sorted[j]=v;
    }
    out->p95_update_us=n?sorted[(n*95+99)/100-1]:0;
}
bool gl30_demo_display_ok(void) { return initialized && display_error==0; }
const char *gl30_demo_json(void) {
    static char out[2048]; char time_text[32],stopwatch[32],led_text[700];
    uint8_t leds[24][3]; gl30_model_leds(&state,leds);
    gl30_format_time(state.remaining_ms,true,time_text,sizeof(time_text));
    gl30_format_time(state.stopwatch_ms,false,stopwatch,sizeof(stopwatch));
    int cursor=0;
    for(int i=0;i<24;i++) cursor+=snprintf(led_text+cursor,sizeof(led_text)-(unsigned)cursor,
        "%s[%u,%u,%u]",i?",":"",leds[i][0],leds[i][1],leds[i][2]);
    snprintf(out,sizeof(out),
        "{\"page\":%d,\"app\":%d,\"menu\":%d,\"menu_position\":%ld,\"phase\":%d,"
        "\"remaining_ms\":%u,\"time\":\"%s\",\"volume\":%d,\"muted\":%s,\"angle\":%.4f,"
        "\"stopwatch_ms\":%llu,\"stopwatch\":\"%s.%02u\",\"stopwatch_running\":%s,"
        "\"off\":%s,\"fault\":%s,\"endstop\":%d,\"light_effect\":%d,\"light_color\":%d,"
        "\"light_brightness\":%d,\"light_field\":%d,\"light_editing\":%s,"
        "\"screen_brightness\":%d,\"language\":\"%s\",\"settings_page\":%d,\"settings_item\":%d,"
        "\"settings_function\":%d,\"settings_editing\":%s,"
        "\"timer_step_minutes\":%d,\"volume_step_percent\":%d,"
        "\"stopwatch_show_centis\":%s,\"alarm_enabled\":%s,\"weather_fahrenheit\":%s,"
        "\"feel_profile\":%d,\"calendar_monday_first\":%s,\"display_error\":%d,"
        "\"motor_state\":%d,\"motor_fault_bits\":%u,"
        "\"clock_valid\":%s,\"clock_unix\":%llu,\"leds\":[%s]}",
        state.page,state.app,state.menu_index,(long)state.menu_position,state.phase,
        (unsigned)state.remaining_ms,time_text,state.volume,state.muted?"true":"false",gl30_model_angle(&state),
        (unsigned long long)state.stopwatch_ms,stopwatch,(unsigned)(state.stopwatch_ms/10%100),state.stopwatch_running?"true":"false",
        state.off?"true":"false",state.fault?"true":"false",state.endstop,state.light_effect,state.light_color,
        state.light_brightness,state.light_field,state.light_editing?"true":"false",state.screen_brightness,
        state.language==GL30_LANGUAGE_CHINESE?"zh":"en",
        state.settings_page,state.settings_item,state.settings_function,state.settings_editing?"true":"false",
        state.timer_step_minutes,state.volume_step_percent,state.stopwatch_show_centis?"true":"false",
        state.alarm_enabled?"true":"false",state.weather_fahrenheit?"true":"false",state.feel_profile,
        state.calendar_monday_first?"true":"false",display_error,
        state.motor_state,(unsigned)state.motor_fault_bits,
        state.epoch_seconds?"true":"false",
        (unsigned long long)(state.epoch_seconds?state.epoch_seconds+state.elapsed_ms/1000:0),led_text);
    return out;
}
