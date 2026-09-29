/* Offline preview exporter. Includes the exact production icon paths with
 * local public-symbol aliases, just like round_layout_tests.c. No board I/O.
 * Usage: gl30_capture_icons EXISTING_OUTPUT_DIRECTORY */
#include "gl30_demo.h"
#include "kk_oled.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define gl30_draw_scene capture_scene
#define gl30_render_get_profile capture_profile
#define gl30_render_get_menu_mode capture_get_mode
#define gl30_render_set_menu_mode capture_set_mode
#include "../../components/gl30_ui/src/gl30_render.c"
extern const uint16_t *gl30_host_frame(void);
static const char *destination;
static void clear_frame(void) {
    OLED_ResetClipWindow(); OLED_SetClearRows(0,466);
    OLED_SetColor(0xffff,0); OLED_Clear(); offset_x=0;
}
static void save_frame(const char *name) {
    char path[1024];
    if(snprintf(path,sizeof(path),"%s/%s.ppm",destination,name)>=(int)sizeof(path)) exit(2);
    if(OLED_Update()!=OLED_OK) exit(3);
    FILE *f=fopen(path,"wb");
    if(!f) {perror(path);exit(4);}
    const uint16_t *p=gl30_host_frame();
    fprintf(f,"P6\n466 466\n255\n");
    for(unsigned n=0;n<466U*466U;n++) {
        unsigned c=p[n];
        unsigned char rgb24[3]={(unsigned char)(((c>>11)&31)*255/31),
            (unsigned char)(((c>>5)&63)*255/63),(unsigned char)((c&31)*255/31)};
        if(fwrite(rgb24,1,3,f)!=3) {fclose(f);exit(5);}
    }
    if(fclose(f)) exit(6);
}
int main(int argc,char **argv) {
    if(argc!=2) {fprintf(stderr,"usage: %s existing-output-directory\n",argv[0]);return 1;}
    destination=argv[1];
    if(OLED_Init()!=OLED_OK) return 2;
    char name[80];
    const float mm[]={14.0f,6.0f,3.2f};
    for(int app=0;app<GL30_APP_COUNT;app++) for(int size=0;size<3;size++) {
        clear_frame(); app_icon(app,233,233,mm[size]*466.0f/(1.32f*25.4f),size==2?0.0f:1.0f);
        snprintf(name,sizeof(name),"icon-%d-%d",app,size); save_frame(name);
    }
    gl30_model s;
    for(int language=0;language<2;language++) {
        gl30_model_init(&s,0,1790665200ULL); s.language=(gl30_language)language;
        clear_frame(); capture_scene(&s,0,0,466);
        snprintf(name,sizeof(name),"home-%d",language); save_frame(name);
        s.page=GL30_MENU;
        for(int app=0;app<GL30_MENU_APP_COUNT;app++) {
            s.menu_index=app; s.menu_visual=(float)app;
            clear_frame();capture_scene(&s,0,0,466);
            snprintf(name,sizeof(name),"menu-%d-%d",language,app);save_frame(name);
        }
        s.page=GL30_APP;
        for(int app=0;app<GL30_APP_COUNT;app++) {
            s.app=(gl30_app)app;clear_frame();capture_scene(&s,0,0,466);
            snprintf(name,sizeof(name),"app-%d-%d",language,app);save_frame(name);
        }
    }
    puts("Captured 63 exact C-renderer frames (software preview, not panel FPS).");
    return 0;
}
