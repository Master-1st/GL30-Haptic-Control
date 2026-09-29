#include "gl30_menu_session.h"
#include "gl30_model.h"
#include "haptics.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks,sequence;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void feed(gl30_esp_link_t *link,uint8_t type,const uint8_t *payload,size_t length,uint64_t now) {
    uint8_t frame[128]; size_t size;
    CHECK(gl30_frame_encode(type,0,++sequence,now,payload,length,frame,sizeof(frame),&size)==0);
    /* All split boundaries exercise the real incremental byte parser. */
    for(size_t i=0;i<size;i++) CHECK(gl30_esp_link_feed(link,frame+i,1,now)==0);
}
static void fast(gl30_esp_link_t *link,float angle,uint64_t now) {
    gl30_motor_state_fast_t state={0}; uint8_t payload[GL30_MOTOR_STATE_FAST_LEN];
    state.angleRad=angle; state.encoderStatus=1; state.motorState=7;
    CHECK(gl30_encode_motor_state_fast(&state,payload,sizeof(payload))==0);
    feed(link,GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_FAST,payload,sizeof(payload),now);
}
static void haptic(gl30_esp_link_t *link,const gl30_menu_session *session,int32_t q,float f,uint64_t now) {
    gl30_haptic_state_t h={.profileId=GL30_MENU_PROFILE_ID,
        .commandNonce=session->command.commandNonce,.modeFlags=GL30_HAPTIC_DETENT,
        .logicalPosition=q,.subPosition=f,.detentWidthRad=GL30_MENU_WIDTH_RAD,.motorState=7,.status=3};
    uint8_t payload[GL30_HAPTIC_STATE_LEN];
    CHECK(gl30_encode_haptic_state(&h,payload,sizeof(payload))==0);
    feed(link,GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE,payload,sizeof(payload),now);
}
static int mod_menu(int32_t q) { int n=q%GL30_MENU_APP_COUNT; return n<0?n+GL30_MENU_APP_COUNT:n; }
static float circular_delta(float a,float b) {
    float count=(float)GL30_MENU_APP_COUNT;
    float d=fmodf(a-b,count); if(d>count/2)d-=count; if(d< -count/2)d+=count; return d;
}
static void haptic_wire_validation(void) {
    gl30_esp_link_t receiver; gl30_esp_link_init(&receiver);
    gl30_haptic_state_t state={.profileId=GL30_MENU_PROFILE_ID,.commandNonce=17,
        .modeFlags=GL30_HAPTIC_DETENT,.logicalPosition=-13,.subPosition=0.375f,
        .detentWidthRad=GL30_MENU_WIDTH_RAD,.motorState=7,.status=3};
    uint8_t payload[GL30_HAPTIC_STATE_LEN],frame[128]; size_t length;
    CHECK(gl30_encode_haptic_state(&state,payload,sizeof(payload))==0);
    CHECK(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE,0,100,10000,
        payload,sizeof(payload),frame,sizeof(frame),&length)==0 && length==60);
    CHECK(gl30_esp_link_feed(&receiver,frame,length,20000)==0);
    CHECK(receiver.has_haptic && receiver.latest_haptic.logicalPosition==-13);
    CHECK(receiver.latest_haptic.commandNonce==17 && receiver.latest_haptic.subPosition==0.375f);
    CHECK(gl30_esp_link_feed(&receiver,frame,length,21000)==0); /* Duplicate cannot refresh age. */
    CHECK(receiver.last_haptic_us==20000 && receiver.stats.haptic_frames==1);
    CHECK(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE,0,99,11000,
        payload,sizeof(payload),frame,sizeof(frame),&length)==0);
    CHECK(gl30_esp_link_feed(&receiver,frame,length,22000)==0);
    CHECK(receiver.last_haptic_us==20000);
    CHECK(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE,0,101,10000,
        payload,sizeof(payload),frame,sizeof(frame),&length)==0);
    CHECK(gl30_esp_link_feed(&receiver,frame,length,23000)==0); /* Same measurement, new envelope. */
    CHECK(receiver.stats.haptic_frames==1);
    state.status=4;
    CHECK(gl30_encode_haptic_state(&state,payload,sizeof(payload))==0);
    CHECK(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE,0,102,12000,
        payload,sizeof(payload),frame,sizeof(frame),&length)==0);
    CHECK(gl30_esp_link_feed(&receiver,frame,length,24000)==0);
    CHECK(receiver.stats.haptic_frames==1 && receiver.stats.bad_float_frames==1);
    state.status=3; state.subPosition=NAN;
    CHECK(gl30_encode_haptic_state(&state,payload,sizeof(payload))==0);
    CHECK(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE,0,103,13000,
        payload,sizeof(payload),frame,sizeof(frame),&length)==0);
    CHECK(gl30_esp_link_feed(&receiver,frame,length,25000)==0);
    CHECK(receiver.stats.haptic_frames==1 && receiver.stats.bad_float_frames==2);
    /* An STM reboot (both its sequence and clock go backwards) invalidates
     * the old applied-state snapshot before a new session may be armed. */
    sequence=199; fast(&receiver,2,26000); CHECK(receiver.has_haptic);
    gl30_motor_state_fast_t rebooted={.encoderStatus=1,.motorState=7};
    uint8_t fast_payload[GL30_MOTOR_STATE_FAST_LEN];
    CHECK(gl30_encode_motor_state_fast(&rebooted,fast_payload,sizeof(fast_payload))==0);
    CHECK(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_FAST,0,1,1000,
        fast_payload,sizeof(fast_payload),frame,sizeof(frame),&length)==0);
    CHECK(gl30_esp_link_feed(&receiver,frame,length,30000)==0);
    CHECK(!receiver.has_haptic && receiver.has_fast);
    state.subPosition=0; state.logicalPosition=0;
    CHECK(gl30_encode_haptic_state(&state,payload,sizeof(payload))==0);
    CHECK(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE,0,2,1500,
        payload,sizeof(payload),frame,sizeof(frame),&length)==0);
    CHECK(gl30_esp_link_feed(&receiver,frame,length,31000)==0 && receiver.has_haptic);
    CHECK(receiver.latest_haptic.logicalPosition==0);
}
int main(void) {
    haptic_wire_validation();
    gl30_esp_link_t link; gl30_menu_session session; gl30_menu_sample sample; gl30_model ui;
    gl30_esp_link_init(&link); gl30_menu_session_init(&session,UINT32_MAX-1);
    gl30_model_init(&ui,0,0); gl30_model_primary(&ui);
    gl30_menu_session_desire(&session,true,ui.menu_epoch,1000);
    CHECK(!gl30_menu_session_step(&session,&link,1000,&sample)); CHECK(!sample.valid);
    gl30_menu_session_arm(&session,true);
    CHECK(gl30_menu_session_step(&session,&link,2000,&sample));
    CHECK(!session.armed && session.command.userTorqueLimitNm==0);

    fast(&link,1.2f,3000); gl30_menu_session_arm(&session,true);
    CHECK(gl30_menu_session_step(&session,&link,3000,&sample));
    CHECK(!sample.valid && session.armed && session.command.commandNonce!=0);
    CHECK(session.command.modeFlags==GL30_HAPTIC_DETENT);
    CHECK(session.command.userTorqueLimitNm<=0.030f && session.command.targetPositionRad==1.2f);
    uint32_t nonce=session.command.commandNonce;

    /* The release boundary tests the actual HAPTIC_STATE wire format and the
     * q/f ownership contract. Detailed STM32 detent-generation internals are
     * covered by the STM32 firmware tests, not duplicated in this ESP UI test. */
    haptic(&link,&session,0,0.0f,4000);
    CHECK(gl30_menu_session_step(&session,&link,4000,&sample) && sample.valid);
    CHECK(gl30_model_motor_menu(&ui,sample.epoch,sample.session,sample.position,sample.fraction,sample.valid));
    CHECK(ui.menu_index==0);
    CHECK(gl30_menu_session_step(&session,&link,5000,&sample)); CHECK(session.command.commandNonce==nonce);

    float prior=0;
    for(int i=0;i<=550;i++) {
        float visual=i/1000.0f;
        int32_t q=(int32_t)floorf(visual+0.5f);
        float fraction=visual-(float)q;
        uint64_t now=6000+(uint64_t)i*500;
        fast(&link,1.2f+visual*GL30_MENU_WIDTH_RAD,now);
        haptic(&link,&session,q,fraction,now+1);
        CHECK(gl30_menu_session_step(&session,&link,now+1,&sample) && sample.valid);
        gl30_model_motor_menu(&ui,sample.epoch,sample.session,sample.position,sample.fraction,true);
        CHECK(ui.menu_index==mod_menu(q));
        CHECK(fabsf(circular_delta(ui.menu_visual,prior))<0.002f);
        prior=ui.menu_visual;
    }
    /* Synthetic large q tests integer remainder without float precision loss. */
    int32_t positions[]={INT32_MAX,INT32_MIN,2000000001,-2000000001,-1,0,1,9,-9};
    for(unsigned i=0;i<sizeof(positions)/sizeof(*positions);i++) {
        gl30_model_motor_menu(&ui,ui.menu_epoch,nonce,positions[i],0.54f,true);
        CHECK(ui.menu_index==mod_menu(positions[i]));
        CHECK(fabsf(ui.menu_visual-(mod_menu(positions[i])+0.54f))<0.00001f);
    }
    int selected=ui.menu_index;
    CHECK(!gl30_model_motor_menu(&ui,ui.menu_epoch+1,nonce,4,0,true));
    CHECK(!gl30_model_motor_menu(&ui,ui.menu_epoch,nonce,4,NAN,true)); CHECK(ui.menu_index==selected);
    gl30_model_motor_menu(&ui,ui.menu_epoch,nonce,0,0.549f,true); prior=ui.menu_visual;
    gl30_model_motor_menu(&ui,ui.menu_epoch,nonce,1,-0.449f,true);
    CHECK(fabsf(circular_delta(ui.menu_visual,prior)-0.002f)<0.00001f && ui.menu_index==1);
    gl30_model_motor_menu(&ui,ui.menu_epoch,nonce,1,-0.54f,true); CHECK(ui.menu_index==1);
    gl30_model_motor_menu(&ui,ui.menu_epoch,nonce,0,0.449f,true); CHECK(ui.menu_index==0);
    CHECK(!gl30_model_motor_menu(&ui,ui.menu_epoch,nonce,0,0,false));
    gl30_model_motor_menu(&ui,ui.menu_epoch,nonce,1,0,true); CHECK(ui.menu_index==1);
    gl30_model_rotate(&ui,5); CHECK(ui.menu_index==1); /* One angle owner. */

    /* Wrong mode/nonce is rejected before UI use, and latches the session off. */
    fast(&link,2,400000); haptic(&link,&session,0,0,400001);
    link.latest_haptic.commandNonce++;
    CHECK(gl30_menu_session_step(&session,&link,400001,&sample));
    CHECK(!session.armed && !sample.valid && session.command.userTorqueLimitNm==0);
    /* Busy transport may defer a zero: do not lose it or re-arm implicitly. */
    CHECK(gl30_menu_session_step(&session,&link,400002,&sample));
    gl30_menu_session_sent(&session);
    CHECK(!gl30_menu_session_step(&session,&link,400002,&sample));
    fast(&link,2,400003); CHECK(!gl30_menu_session_step(&session,&link,400003,&sample));

    gl30_menu_session_arm(&session,true); gl30_menu_session_step(&session,&link,400004,&sample);
    haptic(&link,&session,0,0,400005); gl30_menu_session_step(&session,&link,400005,&sample);
    CHECK(sample.valid);
    fast(&link,2,420005);
    CHECK(gl30_menu_session_step(&session,&link,420005,&sample));
    CHECK(!session.armed && !sample.valid && session.command.userTorqueLimitNm==0);

    /* A pending mode must not run forever without its applied-state reply. */
    gl30_menu_session_desire(&session,true,ui.menu_epoch,500000);
    fast(&link,2,500000); gl30_menu_session_arm(&session,true);
    gl30_menu_session_step(&session,&link,500000,&sample);
    fast(&link,2,600000); CHECK(gl30_menu_session_step(&session,&link,600000,&sample));
    CHECK(!session.armed && !sample.valid && session.command.userTorqueLimitNm==0);
    fast(&link,2,700000); gl30_menu_session_arm(&session,true);
    gl30_menu_session_step(&session,&link,700000,&sample);
    gl30_menu_session_desire(&session,false,ui.menu_epoch,700000);
    CHECK(gl30_menu_session_step(&session,&link,700001,&sample));
    CHECK(session.armed && session.command.modeFlags==0 && session.command.userTorqueLimitNm==0);

    /* GUI heartbeat uses its real publication time, never the 1 kHz sender's
     * time. Normal 250 ms drawing is allowed; a 500 ms stall latches off. */
    gl30_menu_session_desire(&session,true,ui.menu_epoch,800000);
    fast(&link,2,800000); gl30_menu_session_step(&session,&link,800000,&sample);
    haptic(&link,&session,0,0,800001); gl30_menu_session_step(&session,&link,800001,&sample);
    CHECK(sample.valid && session.armed);
    fast(&link,2,1050000); haptic(&link,&session,0,0,1050001);
    gl30_menu_session_step(&session,&link,1050001,&sample); CHECK(sample.valid && session.armed);
    fast(&link,2,1300000); haptic(&link,&session,0,0,1300001);
    CHECK(gl30_menu_session_step(&session,&link,1300001,&sample));
    CHECK(!session.armed && !sample.valid && session.command.userTorqueLimitNm==0);
    gl30_menu_session_desire(&session,true,ui.menu_epoch,1300002);
    CHECK(gl30_menu_session_step(&session,&link,1300002,&sample)); CHECK(!session.armed);
    gl30_menu_session_sent(&session); CHECK(!gl30_menu_session_step(&session,&link,1300003,&sample));
    /* Valid angle alone must not arm an encoder-check/alignment state. */
    link.latest_fast.motorState=2; gl30_menu_session_arm(&session,true);
    CHECK(gl30_menu_session_step(&session,&link,1300004,&sample)); CHECK(!session.armed);

    printf("motor-menu: %u checks passed\n",checks);
    return 0;
}
