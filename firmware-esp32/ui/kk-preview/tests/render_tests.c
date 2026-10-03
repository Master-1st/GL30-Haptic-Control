#include "gl30_demo.h"
#include "gl30_type.h"
#include "kk_oled.h"
#include "kk_ui.h"
#include "kk_oled_driver.h"
#include "kk_oled_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const uint16_t *gl30_host_frame(void);
extern unsigned gl30_host_frame_id(void);
extern bool gl30_host_power(void);
extern void gl30_host_fail_next(int fail);
extern void gl30_host_async_mode(int mode);
extern void gl30_host_complete_on_poll(bool fail);
static void tick(uint32_t at) { gl30_demo_sample(at); gl30_demo_render(at); }
static int checks;
#define CHECK(x) do { checks++; if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static unsigned checksum(void) {
    const uint16_t *p=gl30_host_frame(); unsigned sum=2166136261u;
    for(unsigned i=0;i<466*466;i++) sum=(sum^p[i])*16777619u;
    return sum;
}
static unsigned footer_checksum(void) {
    const uint16_t *p=gl30_host_frame(); unsigned sum=2166136261u;
    for(unsigned y=432;y<466;y++) for(unsigned x=0;x<466;x++)
        sum=(sum^p[y*466+x])*16777619u;
    return sum;
}
static unsigned footer_ink(void) {
    const uint16_t *p=gl30_host_frame(); unsigned count=0;
    for(unsigned y=432;y<466;y++) for(unsigned x=0;x<466;x++) count+=p[y*466+x]!=0;
    return count;
}
static void click_at(uint32_t down_at) {
    gl30_demo_button(true,down_at); gl30_demo_sample(down_at+20);
    gl30_demo_button(false,down_at+50); gl30_demo_sample(down_at+70);
    gl30_demo_sample(down_at+350);
}
static void enter_menu(void) {
    click_at(10);
    CHECK(gl30_demo_state()->page==GL30_MENU);
}
static void span_matches_pixels(void) {
    static uint16_t expected[466*466];
    const int spans[][3]={{-20,10,52},{0,11,466},{464,12,20},{20,13,0},
        {-32768,14,32767},{-6,15,32767},{32760,16,20},{5,17,32768}};
    for(int rotation=0;rotation<4;rotation++) for(int mode=0;mode<3;mode++) {
        for(int pass=0;pass<2;pass++) {
            CHECK(OLED_Init()==OLED_OK);
            OLED_SetRotation((OLED_Rotation)rotation);
            OLED_SetColor(0x17bf,0x1234); OLED_Clear();
            OLED_SetClipWindow(3,9,461,12); OLED_SetDrawMode((OLED_DrawMode)mode);
            for(unsigned i=0;i<sizeof(spans)/sizeof(*spans);i++) {
                int x=spans[i][0],y=spans[i][1],width=spans[i][2];
                if(pass) OLED_DrawHLine((int16_t)x,(int16_t)y,(uint16_t)width);
                else if(width<=32767) for(int p=0;p<width;p++) {
                    int column=x+p;
                    if(column>=0 && column<466) OLED_DrawPixel((int16_t)column,(int16_t)y);
                }
            }
            CHECK(OLED_Update()==OLED_OK);
            if(!pass) memcpy(expected,gl30_host_frame(),sizeof(expected));
            else CHECK(memcmp(expected,gl30_host_frame(),sizeof(expected))==0);
        }
    }
}
static void repeated_clear_contract(void) {
    const uint16_t colors[]={0,0x1111,0x1234};
    const uint16_t patch[]={0xffff,0x00ff};
    for(unsigned c=0;c<3;c++) for(int operation=0;operation<6;operation++) {
        CHECK(OLED_Init()==OLED_OK); OLED_SetColor(0x789a,colors[c]);
        OLED_Clear(); OLED_Clear();
        switch(operation) {
        case 0: OLED_DrawPixel(15,15); break;
        case 1: OLED_DrawHLine(15,15,12); break;
        case 2: OLED_DrawVLine(15,15,12); break;
        case 3: OLED_BlendPixelRGB565(15,15,0xffff,128); break;
        case 4: OLED_BlitRGB565(15,15,2,1,patch); break;
        case 5: OLED_Fill(); break;
        }
        OLED_Clear(); CHECK(OLED_Update()==OLED_OK);
        const uint16_t *p=gl30_host_frame();
        for(unsigned i=0;i<466*466;i++) CHECK(p[i]==colors[c]);
        /* Clearing a buffer filled with another color must still write it. */
        OLED_SetColor(0x789a,(uint16_t)~colors[c]); OLED_Clear();
        CHECK(OLED_Update()==OLED_OK && p[0]==(uint16_t)~colors[c]);
    }
}
static void sparse_history_contract(void) {
    const uint16_t patch[]={0x0123,0x4567,0x89ab,0xcdef};
    for(int rotation=0;rotation<4;rotation++) for(int mode=0;mode<3;mode++) {
        CHECK(OLED_Init()==OLED_OK);
        OLED_SetRotation((OLED_Rotation)rotation);
        for(unsigned pass=0;pass<6;pass++) {
            uint16_t bg=pass<3?0x1234:0x1111;
            OLED_SetColor(0x6789,bg); OLED_Clear();
            OLED_SetDrawMode((OLED_DrawMode)mode);
            OLED_DrawHLine(-3,19,30); OLED_DrawVLine(459,3,120);
            OLED_BlitRGB565(70,80,2,2,patch);
            OLED_BlendPixelRGB565(120,121,0xffcc,137);
            OLED_DrawPixel(0,465); OLED_DrawPixel(465,0);
            CHECK(OLED_Update()==OLED_OK);
            /* Restoring either physical buffer must erase all its old writes. */
            OLED_Clear(); CHECK(OLED_Update()==OLED_OK);
            const uint16_t *p=gl30_host_frame();
            for(unsigned i=0;i<466*466;i++) CHECK(p[i]==bg);
        }
    }
}
static void asynchronous_ui_contract(void) {
    static uint16_t frozen_copy[466*466];
    CHECK(gl30_demo_init(10,0)); gl30_host_async_mode(1);
    tick(800); gl30_demo_render(801);
    CHECK(gl30_demo_display_ok() && OLED_IsBusy()); /* Boot delay is not a DMA timeout. */
    OLED_DriverHandleMemTxComplete(); gl30_demo_render(802);
    gl30_model model,view,original;
    gl30_model_init(&model,10,1800000000);
    model.page=GL30_HOME; model.phase=GL30_RUNNING; model.remaining_ms=20;
    original=model; gl30_model_view(&model,1000,&view);
    CHECK(memcmp(&model,&original,sizeof(model))==0);
    CHECK(view.remaining_ms==0 && view.phase==GL30_RUNNING && view.page==GL30_HOME);
    gl30_model_init(&model,UINT32_MAX-10,0); model.stopwatch_running=true;
    gl30_model_view(&model,20,&view); CHECK(view.stopwatch_ms==31);
    model.page=GL30_MENU; model.menu_epoch=1;
    CHECK(gl30_model_motor_menu(&model,1,2,3,0.25f,true));
    gl30_model_view(&model,100,&view);
    CHECK(view.menu_index==model.menu_index && view.menu_visual==model.menu_visual);

    CHECK(gl30_demo_init(0,0));
    gl30_demo_button(true,10); gl30_demo_sample(30);
    gl30_demo_button(false,60); gl30_demo_sample(80);
    gl30_demo_render(400); /* Display time cannot confirm the pending single. */
    CHECK(gl30_demo_state()->now_ms==80 && gl30_demo_state()->page==GL30_HOME);
    gl30_demo_button(true,360); gl30_demo_sample(380);
    gl30_demo_button(false,410); tick(430); tick(800);
    CHECK(gl30_demo_state()->page==GL30_HOME); /* Captured double still accepted. */

    CHECK(gl30_demo_init(0,0)); gl30_host_async_mode(1);
    gl30_demo_shortcut(1); tick(40); CHECK(OLED_IsBusy());
    const uint16_t *frozen=OLED_InternalGetTransferBuffer();
    memcpy(frozen_copy,frozen,sizeof(frozen_copy));
    unsigned before=gl30_host_frame_id();
    gl30_demo_rotate(10); gl30_demo_sample(50); gl30_demo_render(80);
    CHECK(memcmp(frozen_copy,frozen,sizeof(frozen_copy))==0);
    CHECK(gl30_host_frame_id()==before && OLED_IsBusy());
    CHECK(OLED_SetContrast(20)==OLED_BUSY && OLED_SetPowerSave(true)==OLED_BUSY);
    gl30_demo_shortcut(3); gl30_demo_render(90);
    CHECK(gl30_demo_display_ok() && gl30_host_power());
    OLED_DriverHandleMemTxComplete(); gl30_demo_render(100);
    CHECK(gl30_demo_display_ok() && !gl30_host_power());
    gl30_demo_shortcut(3); gl30_demo_render(110);
    CHECK(gl30_demo_display_ok() && gl30_host_power());
    OLED_DriverHandleMemTxComplete(); gl30_demo_render(120);
    before=gl30_host_frame_id(); gl30_demo_render(200); gl30_demo_render(230);
    CHECK(gl30_host_frame_id()==before); /* No duplicate accepted frame. */

    CHECK(OLED_Init()==OLED_OK); gl30_host_async_mode(1);
    OLED_SetColor(0x1234,0); OLED_DrawPixel(19,20); CHECK(OLED_UpdateDMA()==OLED_OK);
    OLED_DriverHandleMemTxComplete(); before=gl30_host_frame_id();
    OLED_DrawPixel(19,20); CHECK(OLED_UpdateDMA()==OLED_OK);
    CHECK(!OLED_IsBusy() && gl30_host_frame_id()==before); /* Identical: no BSP job required. */

    CHECK(gl30_demo_init(0,0)); gl30_host_async_mode(1);
    gl30_demo_shortcut(1); tick(40); CHECK(OLED_IsBusy());
    frozen=OLED_InternalGetTransferBuffer(); memcpy(frozen_copy,frozen,sizeof(frozen_copy));
    gl30_demo_render(400);
    CHECK(!gl30_demo_display_ok() && OLED_IsBusy());
    CHECK(memcmp(frozen_copy,frozen,sizeof(frozen_copy))==0);
    OLED_DriverHandleMemTxComplete(); gl30_demo_render(450);
    CHECK(!gl30_demo_display_ok()); /* Late completion cannot clear the fault. */

    CHECK(gl30_demo_init(0,0)); gl30_host_async_mode(1); gl30_demo_shortcut(1);
    for(unsigned cycle=0;cycle<100;cycle++) {
        uint32_t at=40+cycle*256;
        gl30_demo_rotate(cycle%2?-1:1); tick(at); CHECK(OLED_IsBusy());
        CHECK(OLED_SetContrast((uint8_t)cycle)==OLED_BUSY);
        gl30_demo_shortcut(3); gl30_demo_render(at+1);
        CHECK(gl30_demo_display_ok() && gl30_host_power());
        OLED_DriverHandleMemTxComplete(); gl30_demo_render(at+2);
        CHECK(gl30_demo_display_ok() && !gl30_host_power());
        CHECK(OLED_SetContrast((uint8_t)cycle)==OLED_OK);
        gl30_demo_shortcut(3); gl30_demo_render(at+3);
        CHECK(gl30_demo_display_ok() && gl30_host_power());
        gl30_demo_render(at+64); OLED_DriverHandleMemTxComplete();
        gl30_demo_render(at+65); CHECK(gl30_demo_display_ok());
    }
}

/* Independently inspect wire bytes so matching encode/decode bugs cannot hide. */
static void wire_bytes_contract(void) {
    static uint16_t colors[256*256];
    CHECK(OLED_Init()==OLED_OK);
    for(unsigned i=0;i<256*256;i++) colors[i]=(uint16_t)i;
    OLED_BlitRGB565(0,0,256,256,colors); CHECK(OLED_Update()==OLED_OK);
    const uint8_t *wire=(const uint8_t *)OLED_InternalGetTransferBuffer();
    for(unsigned y=0;y<256;y++) for(unsigned x=0;x<256;x++) {
        unsigned value=y*256+x,index=y*466+x;
        CHECK(wire[2*index]==(value>>8) && wire[2*index+1]==(value&255));
        CHECK(gl30_host_frame()[index]==value);
    }
    uintptr_t first=(uintptr_t)wire;
    const unsigned background=0x2aa2,source=0xd67b;
    OLED_SetColor((uint16_t)background,0); OLED_Fill();
    for(unsigned a=0;a<256;a++) OLED_BlendPixelRGB565((int16_t)a,2,(uint16_t)source,(uint8_t)a);
    CHECK(OLED_Update()==OLED_OK);
    wire=(const uint8_t *)OLED_InternalGetTransferBuffer();
    uintptr_t second=(uintptr_t)wire;
    CHECK((first>second?first-second:second-first)==434368U); /* 64-byte isolated frames */
    for(unsigned a=0;a<256;a++) {
        unsigned r=(((source>>11)*a+(background>>11)*(255-a)+127)/255);
        unsigned g=((((source>>5)&63)*a+((background>>5)&63)*(255-a)+127)/255);
        unsigned b=(((source&31)*a+(background&31)*(255-a)+127)/255);
        unsigned value=(r<<11)|(g<<5)|b,index=2*466+a;
        CHECK(gl30_host_frame()[index]==value);
        CHECK(wire[2*index]==(value>>8) && wire[2*index+1]==(value&255));
    }
    OLED_SetColor(0x1234,0); OLED_Fill(); OLED_SetDrawMode(OLED_DRAW_XOR);
    OLED_DrawPixel(5,6); CHECK(OLED_Update()==OLED_OK);
    wire=(const uint8_t *)OLED_InternalGetTransferBuffer();
    CHECK(wire[2*(6*466+5)]==0xed && wire[2*(6*466+5)+1]==0xcb);
}
static void changed_frame_contract(void) {
    OLED_Metrics a,b; unsigned sent;
    CHECK(OLED_Init()==OLED_OK);
    CHECK(OLED_Update()==OLED_OK && gl30_host_frame_id()==1); /* first black clears unknown GRAM */
    OLED_SetColor(0x1234,0); OLED_DrawPixel(2,3); CHECK(OLED_Update()==OLED_OK);
    OLED_DrawPixel(2,3); OLED_GetMetrics(&a); sent=gl30_host_frame_id();
    CHECK(OLED_Update()==OLED_OK); OLED_GetMetrics(&b);
    CHECK(b.compares==a.compares+1 && b.identical_frames==a.identical_frames+1);
    CHECK(gl30_host_frame_id()==sent); /* default still deduplicates */
    OLED_DrawPixel(2,3); OLED_MarkFrameChanged(); OLED_GetMetrics(&a);
    CHECK(OLED_Update()==OLED_OK); OLED_GetMetrics(&b);
    CHECK(b.compares==a.compares && b.skipped_compares==a.skipped_compares+1);
    CHECK(gl30_host_frame_id()==sent+1);
    OLED_DrawPixel(2,3); OLED_GetMetrics(&a);
    CHECK(OLED_Update()==OLED_OK); OLED_GetMetrics(&b);
    CHECK(b.compares==a.compares+1 && gl30_host_frame_id()==sent+1); /* non-sticky */
    OLED_MarkFrameChanged(); OLED_Clear(); OLED_DrawPixel(2,3); OLED_GetMetrics(&a);
    CHECK(OLED_Update()==OLED_OK); OLED_GetMetrics(&b);
    CHECK(b.compares==a.compares+1 && b.skipped_compares==a.skipped_compares);

    gl30_host_async_mode(1);
    OLED_DrawPixel(5,6); OLED_MarkFrameChanged(); CHECK(OLED_UpdateDMA()==OLED_OK);
    const uint16_t *frozen=OLED_InternalGetTransferBuffer();
    uint16_t saved=frozen[6*466+5];
    OLED_SetColor(0x07e0,0); OLED_DrawPixel(8,9); OLED_MarkFrameChanged();
    OLED_GetMetrics(&a); CHECK(OLED_UpdateDMA()==OLED_BUSY); OLED_GetMetrics(&b);
    CHECK(a.compares==b.compares && a.skipped_compares==b.skipped_compares);
    CHECK(frozen[6*466+5]==saved);
    OLED_DriverHandleMemTxComplete(); CHECK(OLED_UpdateDMA()==OLED_OK);
    OLED_GetMetrics(&b); CHECK(b.skipped_compares==a.skipped_compares+1);
    OLED_DriverHandleMemTxComplete(); CHECK(gl30_host_frame()[9*466+8]==0x07e0);

    /* Failure recovery is forced full independently of the optional hint. */
    OLED_DrawPixel(8,9); OLED_MarkFrameChanged(); CHECK(OLED_UpdateDMA()==OLED_OK);
    OLED_DriverHandleError(); CHECK(OLED_GetLastStatus()==OLED_ERROR);
    OLED_Clear(); OLED_DrawPixel(8,9); OLED_GetMetrics(&a);
    CHECK(OLED_Update()==OLED_OK); OLED_GetMetrics(&b);
    CHECK(a.compares==b.compares && gl30_host_frame()[9*466+8]==0x07e0);
    OLED_DrawPixel(8,9); gl30_host_fail_next(1); OLED_MarkFrameChanged();
    CHECK(OLED_Update()==OLED_ERROR); OLED_Clear(); OLED_DrawPixel(8,9);
    OLED_GetMetrics(&a); CHECK(OLED_Update()==OLED_OK); OLED_GetMetrics(&b);
    CHECK(a.compares==b.compares);
    OLED_DrawPixel(8,9); sent=gl30_host_frame_id(); CHECK(OLED_Update()==OLED_OK);
    CHECK(sent==gl30_host_frame_id());
    CHECK(OLED_SetPowerSave(true)==OLED_OK && OLED_SetPowerSave(false)==OLED_OK);
    CHECK(gl30_host_frame()[9*466+8]==0x07e0 && gl30_host_frame_id()==sent+1);
}
static void known_dirty_ui_contract(void) {
    OLED_Metrics a,b;
    CHECK(gl30_demo_init(0,0)); tick(40); tick(90);
    OLED_GetMetrics(&a); unsigned id=gl30_host_frame_id();
    KK_UI_Invalidate(); gl30_demo_render(100); OLED_GetMetrics(&b);
    CHECK(b.compares==a.compares+1 && gl30_host_frame_id()==id); /* unknown repaint */
    OLED_GetMetrics(&a); gl30_demo_shortcut(1); tick(200); OLED_GetMetrics(&b);
    CHECK(b.compares==a.compares && b.skipped_compares==a.skipped_compares+1);
    id=gl30_host_frame_id(); tick(240); tick(270); CHECK(id==gl30_host_frame_id());
    gl30_demo_shortcut(0); gl30_demo_rotate(2); tick(280);
    gl30_demo_button(true,300); tick(330); gl30_demo_button(false,360); tick(390); tick(700);
    CHECK(gl30_demo_state()->phase==GL30_RUNNING);
    OLED_GetMetrics(&a); tick(750); OLED_GetMetrics(&b);
    CHECK(b.compares==a.compares && b.skipped_compares==a.skipped_compares+1);
}

static void completion_during_raster_contract(void) {
    CHECK(gl30_demo_init(0,0)); gl30_host_async_mode(1);
    gl30_demo_shortcut(1); tick(40); CHECK(OLED_IsBusy());
    gl30_demo_rotate(2); gl30_demo_sample(50);
    gl30_host_complete_on_poll(false); gl30_demo_render(80);
    CHECK(gl30_host_frame_id()==1 && OLED_IsBusy()); /* new frame already submitted */
    OLED_DriverHandleMemTxComplete();
    CHECK(gl30_host_frame_id()==2 && !OLED_IsBusy());

    /* Ready frame + a late wake on the next deadline must submit before draw. */
    CHECK(gl30_demo_init(0,0)); gl30_host_async_mode(1);
    gl30_demo_shortcut(1); tick(40);
    gl30_demo_rotate(2); tick(80); /* prepare volume 44 while frame 42 is frozen */
    gl30_demo_metrics prepared,waiting;
    gl30_demo_get_metrics(&prepared);
    gl30_demo_rotate(3); tick(110); gl30_demo_get_metrics(&waiting);
    CHECK(prepared.frames==waiting.frames && OLED_IsBusy()); /* do not overwrite READY */
    OLED_DriverHandleMemTxComplete();
    gl30_demo_render(120);
    /* The frozen transfer now belongs to already-rendered volume 44, not a
     * newly rasterized volume 47. A later pass transmits the latter. */
    const uint16_t *frozen=OLED_InternalGetTransferBuffer();
    static uint16_t saved[466*466];memcpy(saved,frozen,sizeof(saved));
    OLED_DriverHandleMemTxComplete();unsigned id=gl30_host_frame_id();
    gl30_demo_render(121);CHECK(OLED_IsBusy());
    CHECK(memcmp(saved,OLED_InternalGetTransferBuffer(),sizeof(saved))!=0);
    OLED_DriverHandleMemTxComplete();CHECK(gl30_host_frame_id()==id+1);

    CHECK(gl30_demo_init(0,0)); gl30_host_async_mode(1);
    gl30_demo_shortcut(1); tick(40);gl30_demo_rotate(2);gl30_demo_sample(50);
    gl30_host_complete_on_poll(true);gl30_demo_render(80);
    CHECK(!gl30_demo_display_ok() && !OLED_IsBusy() && gl30_host_frame_id()==0);
    gl30_demo_render(120); CHECK(!gl30_demo_display_ok() && gl30_host_frame_id()==0);
}

static void typography_contract(void) {
    CHECK(gl30_type_width(GL30_TYPE_TITLE,"手感")>0);
    CHECK(gl30_type_width(GL30_TYPE_TITLE,"准备")>0);
    CHECK(gl30_type_width(GL30_TYPE_DIGITS,"23:59:59")<340);
    CHECK(gl30_type_width(GL30_TYPE_WATCH,"23:59")<330);
    CHECK(OLED_Init()==OLED_OK);
    OLED_SetColor(0xffff,0); OLED_Clear();
    gl30_type_draw(GL30_TYPE_TITLE,20,20,"手感",0xffff);
    CHECK(OLED_Update()==OLED_OK);
    static uint16_t complete[466*466];
    memcpy(complete,gl30_host_frame(),sizeof(complete));
    unsigned partial=0,ink=0;
    for(unsigned i=0;i<466*466;i++) {
        if(complete[i]) ink++;
        if(complete[i] && complete[i]!=0xffff) partial++;
    }
    CHECK(ink>100 && partial>50); /* Real CJK glyphs with antialiased edges. */
    CHECK(OLED_Init()==OLED_OK); OLED_SetColor(0xffff,0); OLED_Clear();
    OLED_SetClipWindow(31,25,27,25);
    gl30_type_draw(GL30_TYPE_TITLE,20,20,"手感",0xffff);
    CHECK(OLED_Update()==OLED_OK);
    for(unsigned y=0;y<466;y++) for(unsigned x=0;x<466;x++)
        CHECK(gl30_host_frame()[y*466+x]==
              (x>=31 && x<58 && y>=25 && y<50?complete[y*466+x]:0));
}
extern void gl30_draw_scene(const gl30_model *s,int16_t x_offset,int16_t clip_x,uint16_t clip_width);
extern void gl30_render_set_menu_mode(uint8_t mode);
static void menu_footer_offset_clip_contract(const gl30_model *view) {
    enum { FOOTER_Y=432, FOOTER_HEIGHT=34, CLIP_X=40, CLIP_WIDTH=386, OFFSET_X=9 };
    static uint16_t reference[FOOTER_HEIGHT*466];

    OLED_SetClearRows(0,466); OLED_SetRetireClearRows(0,466);
    OLED_SetColor(0xffff,0); OLED_Clear(); gl30_render_set_menu_mode(0);
    gl30_draw_scene(view,0,0,466); CHECK(OLED_Update()==OLED_OK);
    const uint16_t *full=gl30_host_frame();
    for(unsigned y=0;y<FOOTER_HEIGHT;y++)
        memcpy(&reference[y*466],&full[(FOOTER_Y+y)*466],466u*sizeof(uint16_t));

    OLED_SetClearRows(0,466); OLED_SetRetireClearRows(0,466);
    OLED_SetColor(0xffff,0); OLED_Clear();
    gl30_draw_scene(view,OFFSET_X,CLIP_X,CLIP_WIDTH); CHECK(OLED_Update()==OLED_OK);
    const uint16_t *clipped=gl30_host_frame();
    for(unsigned y=0;y<FOOTER_HEIGHT;y++)
        for(unsigned x=CLIP_X;x<CLIP_X+CLIP_WIDTH;x++)
            CHECK(clipped[(FOOTER_Y+y)*466+x]==reference[y*466+x-OFFSET_X]);
}
static void menu_cache_visual_contract(void) {
    static uint16_t cached[466*466];
    for(int step=-8;step<=8;step++) {
        CHECK(gl30_demo_init(0,1790490600));
        gl30_demo_button(true,10); tick(30);
        gl30_demo_button(false,70); tick(400);
        gl30_demo_rotate((int16_t)step);
        for(uint32_t at=420;at<=920;at+=20) tick(at);
        CHECK(gl30_demo_state()->page==GL30_MENU);
        memcpy(cached,gl30_host_frame(),sizeof(cached));
        gl30_model view; gl30_model_view(gl30_demo_state(),920,&view);
        OLED_SetClearRows(0,466); OLED_SetRetireClearRows(0,466);
        OLED_SetColor(0xffff,0); OLED_Clear(); gl30_render_set_menu_mode(0);
        gl30_draw_scene(&view,0,0,466); CHECK(OLED_Update()==OLED_OK);
        CHECK(memcmp(cached,gl30_host_frame(),sizeof(cached))==0);
        if(step==0) menu_footer_offset_clip_contract(&view);
    }
}
static void motor_feedback_render_contract(void) {
    CHECK(gl30_demo_init(0,0)); tick(40); tick(90);
    const gl30_model *state=gl30_demo_state();
    CHECK(state->motor_state==GL30_MOTOR_OFFLINE && footer_ink()>0);
    unsigned offline_footer=footer_checksum(),id=gl30_host_frame_id();

    gl30_model before=*state,expected=before;
    expected.motor_fault_bits=0x11u;
    gl30_demo_motor_feedback(GL30_MOTOR_OFFLINE,0x11u);
    CHECK(memcmp(gl30_demo_state(),&expected,sizeof(expected))==0);
    gl30_demo_render(91);
    CHECK(gl30_host_frame_id()==id && footer_checksum()==offline_footer);

    before=*state; expected=before;
    expected.motor_state=GL30_MOTOR_CHECKING; expected.motor_fault_bits=0x22u;
    gl30_demo_motor_feedback(GL30_MOTOR_CHECKING,0x22u);
    CHECK(memcmp(gl30_demo_state(),&expected,sizeof(expected))==0);
    gl30_demo_render(100);
    unsigned checking_footer=footer_checksum();
    CHECK(gl30_host_frame_id()>id && checking_footer!=offline_footer && footer_ink()>0);

    id=gl30_host_frame_id();
    gl30_demo_motor_feedback(GL30_MOTOR_CHECKING,0x33u);
    gl30_demo_render(101);
    CHECK(gl30_demo_state()->motor_fault_bits==0x33u &&
          gl30_host_frame_id()==id && footer_checksum()==checking_footer);

    before=*state; expected=before;
    expected.motor_state=GL30_MOTOR_INVALID;
    gl30_demo_motor_feedback((gl30_motor_feedback_state)-1,0x44u);
    expected.motor_fault_bits=0x44u;
    CHECK(memcmp(gl30_demo_state(),&expected,sizeof(expected))==0);
    gl30_demo_render(110);
    CHECK(footer_checksum()!=checking_footer && footer_ink()>0);

    gl30_demo_shortcut(0); gl30_demo_render(120);
    CHECK(gl30_demo_state()->page==GL30_APP && footer_ink()>0);
    CHECK(gl30_demo_init(0,0)); enter_menu(); tick(400);
    CHECK(gl30_demo_state()->page==GL30_MENU && footer_ink()>0);
}

static void motor_feedback_preserves_timer_stopwatch_and_gesture(void) {
    CHECK(gl30_demo_init(0,0));
    gl30_demo_shortcut(0); gl30_demo_rotate(2); gl30_demo_sample(100);
    gl30_demo_button(true,120); gl30_demo_sample(140);
    const gl30_model *state=gl30_demo_state();
    gl30_model before=*state,expected=before;
    expected.motor_state=GL30_MOTOR_READY; expected.motor_fault_bits=0x01u;
    gl30_demo_motor_feedback(GL30_MOTOR_READY,0x01u);
    CHECK(memcmp(state,&expected,sizeof(expected))==0);
    gl30_demo_button(false,170); gl30_demo_sample(190); gl30_demo_sample(490);
    CHECK(state->page==GL30_APP && state->app==GL30_TIMER &&
          state->phase==GL30_RUNNING);

    before=*state; expected=before;
    expected.motor_state=GL30_MOTOR_ACTIVE; expected.motor_fault_bits=0x02u;
    gl30_demo_motor_feedback(GL30_MOTOR_ACTIVE,0x02u);
    CHECK(memcmp(state,&expected,sizeof(expected))==0 && state->phase==GL30_RUNNING);
    uint32_t remaining=state->remaining_ms; uint64_t elapsed=state->elapsed_ms;
    gl30_demo_sample(1490);
    CHECK(state->phase==GL30_RUNNING && state->remaining_ms==remaining-1000u &&
          state->elapsed_ms==elapsed+1000u);

    CHECK(gl30_demo_init(0,0)); enter_menu();
    gl30_demo_rotate(2); gl30_demo_sample(400);
    click_at(410);
    CHECK(state->page==GL30_APP && state->app==GL30_STOPWATCH &&
          !state->stopwatch_running);
    click_at(790);
    CHECK(state->stopwatch_running && !state->fault);
    before=*state; expected=before;
    expected.motor_state=GL30_MOTOR_FAULT; expected.motor_fault_bits=0x55u;
    gl30_demo_motor_feedback(GL30_MOTOR_FAULT,0x55u);
    CHECK(memcmp(state,&expected,sizeof(expected))==0 &&
          state->stopwatch_running && !state->fault);
    uint64_t stopwatch=state->stopwatch_ms;
    gl30_demo_sample(2140);
    CHECK(state->stopwatch_ms==stopwatch+1000u && state->stopwatch_running);
}

static void motor_feedback_menu_cache_contract(void) {
    CHECK(gl30_demo_init(0,0)); enter_menu();
    unsigned buffers=OLED_GetFrameBufferCount(); CHECK(buffers>=1u);
    uint32_t at=400u;
    for(unsigned i=0;i<buffers+1u;i++,at+=20u) tick(at);
    unsigned offline=footer_checksum(); CHECK(footer_ink()>0);

    gl30_demo_motor_feedback(GL30_MOTOR_CHECKING,0);
    tick(at); at+=20u;
    unsigned checking=footer_checksum();
    CHECK(checking!=offline && footer_ink()>0);
    /* Let every physical buffer receive the new persistent footer. */
    for(unsigned i=0;i<buffers+1u;i++,at+=20u) {
        tick(at); CHECK(footer_checksum()==checking);
    }
    /* Later cached rotations must not resurrect the old footer from history. */
    for(unsigned i=0;i<buffers+2u;i++,at+=20u) {
        gl30_demo_rotate(1); tick(at);
        CHECK(footer_checksum()==checking);
    }
}

static void motor_feedback_pending_dma_ownership_contract(void) {
    static uint16_t frozen_copy[466*466];
    CHECK(gl30_demo_init(0,0)); gl30_host_async_mode(1);
    tick(40); CHECK(OLED_IsBusy());
    const uint16_t *frozen=OLED_InternalGetTransferBuffer();
    memcpy(frozen_copy,frozen,sizeof(frozen_copy));
    unsigned submitted=gl30_host_frame_id();

    gl30_demo_motor_feedback(GL30_MOTOR_READY,0x80u);
    gl30_demo_render(50);
    CHECK(OLED_IsBusy() && gl30_host_frame_id()==submitted &&
          memcmp(frozen_copy,frozen,sizeof(frozen_copy))==0);

    OLED_DriverHandleMemTxComplete();
    CHECK(gl30_host_frame_id()==submitted+1u && !OLED_IsBusy());
    unsigned old_footer=footer_checksum();
    uint32_t at=60;
    for(unsigned tries=0;tries<8u && !OLED_IsBusy();tries++,at+=10u)
        gl30_demo_render(at);
    CHECK(OLED_IsBusy());
    const uint16_t *updated=OLED_InternalGetTransferBuffer();
    CHECK(memcmp(frozen_copy,updated,sizeof(frozen_copy))!=0);
    OLED_DriverHandleMemTxComplete();
    CHECK(gl30_host_frame_id()==submitted+2u &&
          footer_checksum()!=old_footer && footer_ink()>0);
}
int main(void) {
    motor_feedback_render_contract();
    motor_feedback_preserves_timer_stopwatch_and_gesture();
    motor_feedback_menu_cache_contract();
    motor_feedback_pending_dma_ownership_contract();
    typography_contract();
    menu_cache_visual_contract();
    completion_during_raster_contract();
    wire_bytes_contract();
    changed_frame_contract();
    known_dirty_ui_contract();
    span_matches_pixels();
    repeated_clear_contract();
    sparse_history_contract();
    asynchronous_ui_contract();
    CHECK(OLED_Init()==OLED_OK); CHECK(OLED_GetWidth()==466 && OLED_GetHeight()==466);
    OLED_SetColor(0xf800,0); OLED_DrawPixel(0,0); OLED_DrawPixel(465,465);
    OLED_DrawPixel(466,466); CHECK(OLED_Update()==OLED_OK);
    const uint16_t *p=gl30_host_frame(); CHECK(p[0]==0xf800 && p[466*466-1]==0xf800 && p[466]==0);
    OLED_SetColor(0x07e0,0); OLED_SetClipWindow(10,10,2,2); OLED_DrawBox(0,0,466,466);
    CHECK(OLED_Update()==OLED_OK); CHECK(p[10*466+10]==0x07e0 && p[12*466+10]==0 && p[10*466+9]==0);
    OLED_ResetClipWindow(); OLED_SetRotation(OLED_ROTATION_90); OLED_SetColor(0x001f,0); OLED_DrawPixel(0,0);
    CHECK(OLED_Update()==OLED_OK); CHECK(p[465]==0x001f);
    OLED_SetRotation(OLED_ROTATION_0);
    uint16_t source[]={0xf800,0x07e0,0x001f,0xffff}; OLED_BlitRGB565(-1,-1,2,2,source);
    CHECK(OLED_Update()==OLED_OK); CHECK(p[0]==0xffff && p[1]==0);
    OLED_BlendPixelRGB565(5,5,0xffff,128); CHECK(OLED_Update()==OLED_OK);
    CHECK(((p[5*466+5]>>11)&31)>=15 && (p[5*466+5]&31)>=15);
    gl30_host_async_mode(0); OLED_SetColor(0xf800,0); OLED_DrawPixel(4,4); CHECK(OLED_UpdateDMA()==OLED_UNSUPPORTED);
    CHECK(OLED_Update()==OLED_OK && p[4*466+4]==0xf800);
    OLED_SetColor(0x07e0,0); OLED_DrawPixel(8,8); gl30_host_fail_next(1);
    CHECK(OLED_Update()==OLED_ERROR); CHECK(OLED_Update()==OLED_OK && p[8*466+8]==0x07e0);
    gl30_host_async_mode(1); OLED_SetColor(0x001f,0); OLED_DrawPixel(10,10);
    CHECK(OLED_UpdateDMA()==OLED_OK && OLED_IsBusy());
    OLED_SetColor(0xf800,0); OLED_DrawPixel(20,20); CHECK(OLED_Update()==OLED_BUSY);
    OLED_DriverHandleMemTxComplete(); CHECK(!OLED_IsBusy() && OLED_GetLastStatus()==OLED_OK);
    CHECK(p[10*466+10]==0x001f && p[20*466+20]==0);
    CHECK(OLED_Update()==OLED_OK && p[20*466+20]==0xf800);
    OLED_SetColor(0x001f,0); OLED_DrawPixel(11,11);
    CHECK(OLED_UpdateDMA()==OLED_OK);
    OLED_SetColor(0xf800,0); OLED_DrawPixel(21,21); OLED_DriverHandleError();
    CHECK(!OLED_IsBusy() && OLED_GetLastStatus()==OLED_ERROR);
    CHECK(OLED_Update()==OLED_OK && p[21*466+21]==0xf800 && p[11*466+11]==0);
    gl30_host_async_mode(2); OLED_SetColor(0x07e0,0); OLED_DrawPixel(30,30);
    CHECK(OLED_UpdateDMA()==OLED_OK && !OLED_IsBusy() && p[30*466+30]==0x07e0);
    gl30_host_async_mode(0); CHECK(OLED_SetPowerSave(true)==OLED_OK && !gl30_host_power());
    CHECK(OLED_SetPowerSave(false)==OLED_OK && gl30_host_power() && p[30*466+30]==0x07e0);
    CHECK(gl30_demo_init(0,1789426800)); tick(40); unsigned home=checksum();
    CHECK(gl30_host_frame_id()>0 && gl30_demo_state()->page==GL30_HOME);
    gl30_demo_button(true,50); tick(75); gl30_demo_button(false,100); tick(125);
    CHECK(gl30_demo_state()->page==GL30_HOME); tick(401); CHECK(gl30_demo_state()->page==GL30_MENU);
    CHECK(home!=checksum());
    gl30_demo_rotate(GL30_MENU_APP_COUNT); tick(450); CHECK(gl30_demo_state()->menu_index==0);
    gl30_demo_button(true,500); tick(525); gl30_demo_button(false,550); tick(575); tick(851);
    CHECK(gl30_demo_state()->page==GL30_APP && gl30_demo_state()->app==GL30_TIMER);
    gl30_demo_rotate(90); tick(900); CHECK(gl30_demo_state()->remaining_ms==5400000);
    CHECK(strstr(gl30_demo_json(),"01:30:00")!=NULL);
    /* A complete double gesture returns to menu without starting the timer. */
    gl30_demo_button(true,1000); tick(1025); gl30_demo_button(false,1050); tick(1075);
    gl30_demo_button(true,1150); tick(1175); gl30_demo_button(false,1200); tick(1225);
    CHECK(gl30_demo_state()->page==GL30_MENU && gl30_demo_state()->phase==GL30_SETTING);
    for(int i=0;i<GL30_MENU_APP_COUNT;i++) {
        gl30_demo_rotate((int16_t)(i-gl30_demo_state()->menu_index)); tick(1300+i*1000);
        uint32_t at=1350+i*1000;
        gl30_demo_button(true,at); tick(at+25); gl30_demo_button(false,at+50); tick(at+75); tick(at+351);
        CHECK(gl30_demo_state()->page==GL30_APP && gl30_demo_state()->app==(gl30_app)i);
        CHECK(checksum()!=0);
        gl30_demo_button(true,at+400); tick(at+425); gl30_demo_button(false,at+450); tick(at+475);
        gl30_demo_button(true,at+550); tick(at+575); gl30_demo_button(false,at+600); tick(at+625);
        CHECK(gl30_demo_state()->page==GL30_MENU);
    }
    gl30_demo_shortcut(3); tick(11000); CHECK(!gl30_host_power() && gl30_demo_state()->off);
    CHECK(strstr(gl30_demo_json(),"\"display_error\":0")!=NULL);
    CHECK(gl30_demo_init(0,0)); gl30_demo_shortcut(0); gl30_demo_rotate(1); tick(40);
    gl30_demo_button(true,100); tick(125); gl30_demo_button(false,150); tick(175); tick(451);
    CHECK(gl30_demo_state()->phase==GL30_RUNNING);
    gl30_demo_button(true,60250); tick(60275); gl30_demo_button(false,60300); tick(60325);
    tick(60460); tick(60700);
    CHECK(gl30_demo_state()->phase==GL30_DONE && gl30_demo_state()->remaining_ms==0);
    CHECK(gl30_demo_init(0,0)); gl30_demo_button(true,50); tick(75);
    gl30_demo_cancel_input(); tick(1000); CHECK(gl30_demo_state()->page==GL30_HOME);
    CHECK(gl30_demo_init(0,0));
    gl30_demo_button(true,10); tick(30); gl30_demo_button(false,60); tick(80);
    gl30_demo_button(true,360); CHECK(gl30_demo_state()->page==GL30_HOME);
    tick(380); gl30_demo_button(false,410); tick(430); tick(800);
    CHECK(gl30_demo_state()->page==GL30_HOME); /* One double-back at root, no single-confirm. */
    CHECK(OLED_Init()==OLED_OK); OLED_SetColor(0xffff,0);
    OLED_DrawEllipse(233,233,233,233); CHECK(OLED_Update()==OLED_OK);
    CHECK(p[233]==0xffff && p[233*466]==0xffff);
    OLED_DrawFilledEllipse(233,233,233,233); CHECK(OLED_Update()==OLED_OK);
    CHECK(p[233*466+233]==0xffff);
    /* A clipped photo row keeps its source stride and leaves neighbours alone. */
    const uint16_t patch[]={0x1111,0x2222,0x3333,0x4444,0x5555,0x6666};
    OLED_Clear(); OLED_SetClipWindow(21,31,2,1);
    OLED_BlitRGB565(20,30,3,2,patch); CHECK(OLED_Update()==OLED_OK);
    CHECK(p[31*466+21]==0x5555 && p[31*466+22]==0x6666);
    CHECK(p[31*466+20]==0 && p[30*466+21]==0 && p[32*466+21]==0);
    /* Optimizing unrotated rows must preserve all other orientations. */
    const unsigned first_pixel[]={2*466+1,1*466+463,463*466+464,464*466+2};
    const unsigned last_pixel[]={3*466+3,3*466+462,462*466+462,462*466+3};
    for(unsigned rotation=0;rotation<4;rotation++) {
        OLED_SetRotation((OLED_Rotation)rotation); OLED_Clear();
        OLED_BlitRGB565(1,2,3,2,patch); CHECK(OLED_Update()==OLED_OK);
        CHECK(p[first_pixel[rotation]]==0x1111 && p[last_pixel[rotation]]==0x6666);
    }
    /* Replay 10 ms touch samples collected during a slow frame. Rendering
     * must not be required to debounce a 60 ms tap or preserve a double tap. */
    CHECK(gl30_demo_init(0,0)); unsigned before_input=gl30_host_frame_id();
    for(uint32_t at=10;at<=430;at+=10)
        gl30_demo_touch(233,233,at>=50 && at<110,at);
    CHECK(gl30_demo_state()->page==GL30_MENU);
    CHECK(gl30_host_frame_id()==before_input);
    tick(430); CHECK(gl30_host_frame_id()>before_input);
    for(uint32_t at=440;at<=900;at+=10)
        gl30_demo_touch(233,233,(at>=470 && at<530)||(at>=610 && at<670),at);
    CHECK(gl30_demo_state()->page==GL30_HOME);
    /* A sampled rotation belongs to the menu before the next confirmed tap. */
    gl30_demo_button(true,950); gl30_demo_touch(0,0,false,980);
    gl30_demo_button(false,1010); gl30_demo_touch(0,0,false,1320);
    CHECK(gl30_demo_state()->page==GL30_MENU);
    gl30_demo_rotate(1); gl30_demo_touch(0,0,false,1330);
    gl30_demo_button(true,1400); gl30_demo_touch(0,0,false,1430);
    gl30_demo_button(false,1460); gl30_demo_touch(0,0,false,1770);
    CHECK(gl30_demo_state()->page==GL30_APP && gl30_demo_state()->app==GL30_VOLUME);
    CHECK(gl30_demo_state()->volume==42);
    /* Absolute clock corrections must not reset elapsed timer/stopwatch time. */
    CHECK(gl30_demo_init(0,0));
    tick(2000);
    CHECK(strstr(gl30_demo_json(),"\"clock_valid\":false,\"clock_unix\":0")!=NULL);
    gl30_demo_set_clock(1800000000);
    tick(3500);
    CHECK(strstr(gl30_demo_json(),"\"clock_valid\":true,\"clock_unix\":1800000001")!=NULL);
    gl30_demo_shortcut(0); gl30_demo_rotate(2); tick(3600);
    gl30_demo_button(true,3700); tick(3730);
    gl30_demo_button(false,3760); tick(4100);
    CHECK(gl30_demo_state()->phase==GL30_RUNNING);
    uint32_t remaining=gl30_demo_state()->remaining_ms;
    gl30_demo_set_clock(1790000000); /* NTP/USB may step civil time backwards. */
    CHECK(gl30_demo_state()->remaining_ms==remaining && gl30_demo_state()->phase==GL30_RUNNING);
    tick(5100); CHECK(gl30_demo_state()->remaining_ms==remaining-1000);
    gl30_demo_button(true,5200); tick(5230); gl30_demo_button(false,5260); tick(5290);
    gl30_demo_button(true,5340); tick(5370); gl30_demo_button(false,5400); tick(5430);
    CHECK(gl30_demo_state()->page==GL30_MENU);
    gl30_demo_rotate(GL30_STOPWATCH); tick(5500);
    gl30_demo_button(true,5600); tick(5630); gl30_demo_button(false,5660); tick(6000);
    CHECK(gl30_demo_state()->app==GL30_STOPWATCH && gl30_demo_state()->page==GL30_APP);
    gl30_demo_button(true,6100); tick(6130); gl30_demo_button(false,6160); tick(6500);
    CHECK(gl30_demo_state()->stopwatch_running);
    uint64_t stopwatch_before=gl30_demo_state()->stopwatch_ms;
    gl30_demo_set_clock(1900000000);
    CHECK(gl30_demo_state()->stopwatch_ms==stopwatch_before && gl30_demo_state()->stopwatch_running);
    tick(7500); CHECK(gl30_demo_state()->stopwatch_ms==stopwatch_before+1000);
    CHECK(gl30_demo_init(0,0) && gl30_demo_display_ok());
    gl30_host_fail_next(1); gl30_demo_shortcut(1); tick(100);
    CHECK(!gl30_demo_display_ok());
    printf("RGB565/shared UI: %d checks passed\n",checks); return 0;
}
