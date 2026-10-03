#include "gl30_model.h"
#include "gl30_gesture.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
static int checks;
#define CHECK(x) do { checks++; if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
int main(void) {
    gl30_model s; char t[40]; uint8_t leds[24][3];
    gl30_model_init(&s,0,0); CHECK(s.page==GL30_HOME && s.language==GL30_LANGUAGE_ENGLISH);
    gl30_model_primary(&s); CHECK(s.page==GL30_MENU);
    gl30_model_rotate(&s,-1); CHECK(s.menu_index==7 && s.menu_position==-1);
    gl30_model_tick(&s,1000); gl30_model_leds(&s,leds);
    int lit=0; for(int i=0;i<24;i++) if(leds[i][0]||leds[i][1]||leds[i][2]) lit++;
    CHECK(lit==1);
    /* Unlimited reverse carousel motion must retain the same LED phase as
     * the equivalent positive pose, even beyond the old 100-turn offset. */
    for(int turns=-1124;turns<=1124;turns+=281) {
        for(int index=0;index<GL30_MENU_APP_COUNT;index++) {
            s.menu_visual=(float)(turns*GL30_MENU_APP_COUNT+index);
            gl30_model_leds(&s,leds);
            for(int i=0;i<24;i++) {
                bool active=leds[i][0]||leds[i][1]||leds[i][2];
                CHECK(active==(i==index*3));
            }
        }
    }
    s.menu_visual=(float)s.menu_position;
    for(int i=0;i<10000;i++) gl30_model_rotate(&s,1);
    CHECK(s.menu_index==(7+10000)%GL30_MENU_APP_COUNT); CHECK(s.menu_position>-9001 && s.menu_position<9001);
    gl30_model_open(&s,GL30_TIMER); gl30_model_rotate(&s,90);
    CHECK(s.remaining_ms==5400000); gl30_format_time(s.remaining_ms,true,t,sizeof(t)); CHECK(!strcmp(t,"01:30:00"));
    CHECK(fabsf(gl30_model_angle(&s)-540)<0.01f);
    gl30_model_primary(&s); gl30_model_tick(&s,62000); CHECK(s.remaining_ms==5339000);
    gl30_model_rotate(&s,-30); CHECK(s.remaining_ms==3539000 && s.phase==GL30_RUNNING);
    gl30_model_primary(&s); CHECK(s.phase==GL30_PAUSED);
    gl30_model_rotate(&s,1); CHECK(s.remaining_ms==3599000 && s.phase==GL30_PAUSED);
    gl30_model_tick(&s,63000); CHECK(s.remaining_ms==3599000);
    gl30_model_primary(&s); gl30_model_back(&s); gl30_model_back(&s);
    CHECK(s.page==GL30_HOME); gl30_model_tick(&s,64000); CHECK(s.remaining_ms==3598000);
    gl30_model_power(&s); gl30_model_tick(&s,65000); CHECK(s.off && s.remaining_ms==3597000);
    gl30_model_leds(&s,leds); lit=0; for(int i=0;i<24;i++) lit+=leds[i][0]+leds[i][1]+leds[i][2]; CHECK(lit==0);
    gl30_model_primary(&s); CHECK(!s.off && s.page==GL30_HOME);
    gl30_model_open(&s,GL30_TIMER); gl30_model_rotate(&s,-360);
    CHECK(s.phase==GL30_DONE && s.remaining_ms==0 && s.endstop==-1);
    gl30_model_rotate(&s,-1); CHECK(s.phase==GL30_DONE && s.remaining_ms==0);
    gl30_model_rotate(&s,1); CHECK(s.phase==GL30_SETTING && s.remaining_ms==60000);
    gl30_model_rotate(&s,1000); CHECK(s.remaining_ms==GL30_TIMER_MAX_MS && s.endstop==1);
    gl30_model_open(&s,GL30_VOLUME); gl30_model_rotate(&s,-9);
    CHECK(s.volume==33 && fabsf(gl30_model_angle(&s)-118.8f)<0.01f);
    gl30_model_primary(&s); CHECK(s.muted); gl30_model_rotate(&s,1); CHECK(!s.muted && s.volume==34);
    gl30_model_rotate(&s,-34); CHECK(s.volume==0 && s.endstop==-1);
    gl30_model_open(&s,GL30_STOPWATCH); gl30_model_primary(&s); gl30_model_tick(&s,66023);
    CHECK(s.stopwatch_ms==1023); gl30_model_back(&s); gl30_model_tick(&s,67123); CHECK(s.stopwatch_ms==2123);
    gl30_model_fault(&s,true); CHECK(!s.stopwatch_running); gl30_model_tick(&s,68123); CHECK(s.stopwatch_ms==2123);
    gl30_model_fault(&s,false); CHECK(!s.stopwatch_running);
    /* Settings is a five-card hub with language selection. Lighting is no longer
     * a top-level menu item, and long press enters compact function settings. */
    gl30_model_open(&s,GL30_SETTINGS);
    CHECK(s.settings_page==GL30_SETTINGS_ROOT && s.settings_item==0);
    gl30_model_rotate(&s,1); CHECK(s.settings_item==1);
    gl30_model_primary(&s); CHECK(s.settings_page==GL30_SETTINGS_LIGHT && !s.light_editing);
    gl30_model_primary(&s); CHECK(s.light_editing);
    gl30_model_back(&s); CHECK(!s.light_editing && s.settings_page==GL30_SETTINGS_LIGHT);
    gl30_model_back(&s); CHECK(s.settings_page==GL30_SETTINGS_ROOT);
    gl30_model_rotate(&s,2); CHECK(s.settings_item==2);
    gl30_model_primary(&s); CHECK(s.settings_page==GL30_SETTINGS_LANGUAGE && s.language==GL30_LANGUAGE_ENGLISH);
    gl30_model_rotate(&s,1); CHECK(s.language==GL30_LANGUAGE_CHINESE && fabsf(gl30_model_angle(&s)-180.0f)<0.01f);
    gl30_model_primary(&s); CHECK(s.settings_page==GL30_SETTINGS_ROOT && s.settings_item==0);
    gl30_model_rotate(&s,2); gl30_model_primary(&s); gl30_model_rotate(&s,-1);
    CHECK(s.settings_page==GL30_SETTINGS_LANGUAGE && s.language==GL30_LANGUAGE_ENGLISH);
    gl30_model_back(&s); CHECK(s.settings_page==GL30_SETTINGS_ROOT && s.settings_item==0);
    gl30_model_open(&s,GL30_TIMER); gl30_model_reset(&s); gl30_model_quick_settings(&s);
    CHECK(s.app==GL30_SETTINGS && s.settings_page==GL30_SETTINGS_FUNCTION &&
          s.settings_function==GL30_TIMER && s.settings_from_app);
    gl30_model_primary(&s); CHECK(s.settings_editing);
    gl30_model_rotate(&s,1); CHECK(s.timer_step_minutes==5);
    gl30_model_primary(&s); CHECK(!s.settings_editing);
    gl30_model_back(&s); CHECK(s.page==GL30_APP && s.app==GL30_TIMER && !s.settings_from_app);
    gl30_model_rotate(&s,1); CHECK(s.remaining_ms==300000);
    gl30_model_open(&s,GL30_LIGHTING); gl30_model_primary(&s); gl30_model_rotate(&s,-1); CHECK(s.light_effect==3);
    gl30_model_back(&s); CHECK(!s.light_editing && s.page==GL30_APP);
    gl30_model_rotate(&s,2); CHECK(s.light_field==2); gl30_model_primary(&s); gl30_model_rotate(&s,-20);
    CHECK(s.light_brightness==0); gl30_model_leds(&s,leds); lit=0;
    for(int i=0;i<24;i++) lit+=leds[i][0]+leds[i][1]+leds[i][2];
    CHECK(lit==0 && !s.off);
    gl30_model_init(&s,UINT32_MAX-99,0); gl30_model_open(&s,GL30_TIMER); gl30_model_rotate(&s,1); gl30_model_primary(&s);
    gl30_model_tick(&s,100); CHECK(s.remaining_ms==59800); gl30_model_tick(&s,99); CHECK(s.remaining_ms==59800);
    gl30_model_power(&s); gl30_model_tick(&s,60000); CHECK(s.off && s.phase==GL30_DONE);
    gl30_format_time(UINT64_MAX,true,t,sizeof(t)); CHECK(strcmp(t,"00:00:00")!=0);
    gl30_model_init(&s,0,UINT64_MAX); CHECK(s.epoch_seconds==0);

    gl30_gesture g; gl30_gesture_init(&g,false,0);
    CHECK(gl30_gesture_sample(&g,true,10)==0); CHECK(gl30_gesture_sample(&g,true,30)==0);
    CHECK(gl30_gesture_sample(&g,false,60)==0); CHECK(gl30_gesture_sample(&g,false,80)==0);
    CHECK(gl30_gesture_sample(&g,false,359)==0); CHECK(gl30_gesture_sample(&g,false,360)==GL30_GESTURE_SINGLE);
    CHECK(gl30_gesture_sample(&g,false,500)==0);
    gl30_gesture_init(&g,false,0);
    gl30_gesture_sample(&g,true,10); gl30_gesture_sample(&g,true,30);
    gl30_gesture_sample(&g,false,60); gl30_gesture_sample(&g,false,80);
    gl30_gesture_sample(&g,true,200); gl30_gesture_sample(&g,true,220);
    CHECK(gl30_gesture_sample(&g,false,250)==0); CHECK(gl30_gesture_sample(&g,false,270)==GL30_GESTURE_DOUBLE);
    CHECK(gl30_gesture_sample(&g,false,700)==0);
    gl30_gesture_init(&g,false,0);
    gl30_gesture_sample(&g,true,10); gl30_gesture_sample(&g,true,30);
    CHECK(gl30_gesture_sample(&g,true,660)==GL30_GESTURE_LONG);
    gl30_gesture_sample(&g,false,700); CHECK(gl30_gesture_sample(&g,false,720)==0); CHECK(gl30_gesture_sample(&g,false,1100)==0);
    gl30_gesture_init(&g,false,0);
    gl30_gesture_sample(&g,true,10); gl30_gesture_sample(&g,true,30); gl30_gesture_sample(&g,false,60); gl30_gesture_sample(&g,false,80);
    gl30_gesture_sample(&g,true,200); gl30_gesture_sample(&g,true,220); CHECK(gl30_gesture_sample(&g,true,850)==GL30_GESTURE_LONG);
    gl30_gesture_sample(&g,false,900); CHECK(gl30_gesture_sample(&g,false,920)==0); CHECK(gl30_gesture_sample(&g,false,1200)==0);
    gl30_gesture_init(&g,true,0); gl30_gesture_sample(&g,false,40); CHECK(gl30_gesture_sample(&g,false,60)==0); CHECK(gl30_gesture_sample(&g,false,500)==0);
    gl30_gesture_init(&g,false,0); gl30_gesture_sample(&g,true,10); gl30_gesture_sample(&g,true,30); gl30_gesture_cancel(&g);
    gl30_gesture_sample(&g,false,100); CHECK(gl30_gesture_sample(&g,false,120)==0); CHECK(gl30_gesture_sample(&g,false,500)==0);
    printf("model/gesture: %d checks passed\n",checks); return 0;
}
