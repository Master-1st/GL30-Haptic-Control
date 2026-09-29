/* Inspect the production renderer's actual glyph ink against the round panel.
 * The renderer is included with test-only symbol aliases so its private text
 * calls can be intercepted without adding callbacks to the firmware hot path.
 * Each label is rasterized at a safe origin, then its ink is projected back
 * onto the requested coordinates. Clipping cannot hide off-panel glyphs.
 * Decorative cards/backgrounds are intentionally not treated as text. */
#include "gl30_demo.h"
#include "kk_oled.h"
#include "kk_oled_internal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int16_t audit_text(int16_t x,int16_t y,const char *s);
#define OLED_DrawUTF8 audit_text
#define gl30_draw_scene audit_scene
#define gl30_render_get_profile audit_profile
#define gl30_render_get_menu_mode audit_get_mode
#define gl30_render_set_menu_mode audit_set_mode
#include "../../../components/gl30_ui/src/gl30_render.c"
#undef OLED_DrawUTF8
static unsigned labels,failures;
static const char *scene;
static int16_t audit_text(int16_t x,int16_t y,const char *s) {
    OLED_ResetClipWindow(); OLED_SetClearRows(0,466); OLED_SetColor(0xffff,0); OLED_Clear();
    int16_t advance=OLED_DrawUTF8(16,16,s);
    const uint16_t *p=OLED_InternalTestGetDrawBuffer();
    unsigned outside=0,ink=0;
    for(int yy=0;yy<160;yy++) for(int xx=0;xx<466;xx++) if(p[yy*466+xx]) {
        int dx=2*(x+xx-16)-465,dy=2*(y+yy-16)-465;
        ++ink;
        if(dx*dx+dy*dy>465*465) ++outside;
    }
    ++labels;
    if(!ink || outside) {++failures; printf("LAYOUT %s (%d,%d) %s: ink=%u outside=%u\n",scene,x,y,s,ink,outside);}
    return advance;
}
static void draw(gl30_model *s,const char *name) { scene=name; audit_scene(s,0,0,466); }
static void icon_silhouettes(void) {
    static uint16_t reference[466U*466U];
    const float diameters_mm[]={3.2f,6.0f,14.0f};
    const uint16_t *pixels=OLED_InternalTestGetDrawBuffer();
    for(int app=0;app<GL30_APP_COUNT;app++) for(unsigned size=0;size<3;size++) {
        float diameter=diameters_mm[size]*(466.0f/(1.32f*25.4f));
        OLED_SetColor(0xffff,0);OLED_Clear();
        app_icon(app,233,233,diameter,1.0f);
        unsigned ink=0;
        for(unsigned n=0;n<466U*466U;n++) {reference[n]=pixels[n];ink+=pixels[n]!=0;}
        OLED_SetColor(0xffff,0);OLED_Clear();
        app_icon(app,233,233,diameter,0.15f);
        unsigned changed=0;
        for(unsigned n=0;n<466U*466U;n++) if((reference[n]!=0)!=(pixels[n]!=0)) {
            /* Dimming a Q4 antialiased edge may round its final RGB565 value
             * to black. Permit only disappearing edge ink within one 5-bit or
             * two 6-bit channel LSBs, never a new pixel or a lost
             * visible stroke/interior. This is not a pixel-equality test for
             * deliberately different colors. */
            /* The DRAW buffer stores high-byte-first wire RGB565, unlike the
             * native-RGB565 completed host frame used by screenshot tests. */
            const unsigned char *wire=(const unsigned char *)&reference[n];
            uint16_t value=(uint16_t)((uint16_t)wire[0]<<8)|wire[1];
            bool faint=(value>>11)<=1 && ((value>>5)&63)<=2 && (value&31)<=1;
            bool edge=(n%466U)>0 && (n%466U)<465 && n>=466U && n<465U*466U &&
                (!reference[n-1] || !reference[n+1] || !reference[n-466] || !reference[n+466]);
            if(pixels[n] || !faint || !edge) ++changed;
        }
        if(!ink || changed) {
            ++failures;
            printf("ICON app=%d diameter_mm=%.1f ink=%u silhouette_changes=%u\n",
                   app,diameters_mm[size],ink,changed);
        }
    }
}
int main(void) {
    if(OLED_Init()!=OLED_OK) return 2;
    icon_silhouettes();
    gl30_model s;
    for(int lang=0;lang<2;lang++) {
        gl30_model_init(&s,0,1788844402ULL); s.language=(gl30_language)lang;
        draw(&s,"home"); s.phase=GL30_RUNNING; s.remaining_ms=GL30_TIMER_MAX_MS; draw(&s,"home-running-long");
        s.phase=GL30_PAUSED; draw(&s,"home-paused-long");s.phase=GL30_SETTING;
        s.page=GL30_MENU;
        for(int i=0;i<GL30_MENU_APP_COUNT;i++) { s.menu_index=i;s.menu_visual=(float)i;draw(&s,"menu"); }
        s.page=GL30_APP;
        for(int i=0;i<GL30_APP_COUNT;i++) { s.app=(gl30_app)i;draw(&s,"app"); }
        s.app=GL30_SETTINGS;
        for(int i=GL30_SETTINGS_ROOT;i<=GL30_SETTINGS_FUNCTION;i++) {
            s.settings_page=i; s.settings_item=0;draw(&s,"settings");
        }
        s.app=GL30_CALENDAR;
        /* Jan 2022 starts Saturday and needs six rows; leap/non-leap February. */
        const uint64_t months[]={1640966400ULL,1706716800ULL,4105094400ULL,1780243200ULL};
        for(unsigned i=0;i<sizeof(months)/sizeof(*months);i++) for(int mon=0;mon<2;mon++) {
            s.epoch_seconds=months[i];s.calendar_monday_first=mon;draw(&s,"calendar");
        }
    }
    printf("Round layout: %u labels + 27 icon silhouettes, %u failures\n",labels,failures);
    return failures?1:0;
}
