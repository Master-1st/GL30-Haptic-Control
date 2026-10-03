#include "gl30_menu_session.h"
#include "gl30_model.h"
#include "foc.h"
#include "haptics.h"
#include "safety_supervisor.h"
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
static void publish_fast(gl30_esp_link_t *link,uint32_t motor_state,uint32_t fault_bits,
                         uint16_t encoder_status,float angle,uint64_t now) {
    gl30_motor_state_fast_t state={0}; uint8_t payload[GL30_MOTOR_STATE_FAST_LEN];
    state.angleRad=angle; state.encoderStatus=encoder_status;
    state.motorState=motor_state; state.faultBits=fault_bits;
    CHECK(gl30_encode_motor_state_fast(&state,payload,sizeof(payload))==0);
    feed(link,GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_FAST,payload,sizeof(payload),now);
}
static void fast(gl30_esp_link_t *link,float angle,uint64_t now) {
    publish_fast(link,GL30_STARTUP_ACTIVE,0,1,angle,now);
}
static void haptic(gl30_esp_link_t *link,const gl30_menu_session *session,int32_t q,float f,uint64_t now) {
    gl30_haptic_state_t h={.profileId=GL30_MENU_PROFILE_ID,
        .commandNonce=session->command.commandNonce,.modeFlags=GL30_HAPTIC_DETENT,
        .logicalPosition=q,.subPosition=f,.detentWidthRad=GL30_MENU_WIDTH_RAD,.motorState=7,.status=3};
    uint8_t payload[GL30_HAPTIC_STATE_LEN];
    CHECK(gl30_encode_haptic_state(&h,payload,sizeof(payload))==0);
    feed(link,GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE,payload,sizeof(payload),now);
}
#define ZERO_TEST_NONCE 0x1235u
static void publish_fast_state(gl30_esp_link_t *link,const gl30_motor_state_fast_t *state,
                               uint64_t now) {
    uint8_t payload[GL30_MOTOR_STATE_FAST_LEN];
    CHECK(gl30_encode_motor_state_fast(state,payload,sizeof(payload))==0);
    feed(link,GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_FAST,payload,sizeof(payload),now);
}
static void publish_haptic_state(gl30_esp_link_t *link,const gl30_haptic_state_t *state,
                                 uint64_t now) {
    uint8_t payload[GL30_HAPTIC_STATE_LEN];
    CHECK(gl30_encode_haptic_state(state,payload,sizeof(payload))==0);
    feed(link,GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE,payload,sizeof(payload),now);
}
static gl30_motor_state_fast_t zero_ready_fast(void) {
    return (gl30_motor_state_fast_t){.angleRad=0.25f,.encoderStatus=1u,
        .motorState=GL30_STARTUP_READY};
}
static gl30_haptic_state_t zero_ready_haptic(uint32_t nonce) {
    return (gl30_haptic_state_t){.commandNonce=nonce,.subPosition=0.25f,
        .motorState=GL30_STARTUP_READY,.status=GL30_HAPTIC_STATE_ENCODER_VALID};
}
static bool zero_result(const gl30_motor_state_fast_t *fast_state,
                        const gl30_haptic_state_t *haptic_state,
                        uint64_t sent_us,uint64_t fast_at,uint64_t haptic_at,
                        uint64_t now_us,uint32_t sent_nonce) {
    gl30_esp_link_t link; gl30_menu_session session;
    gl30_esp_link_init(&link); gl30_menu_session_init(&session,0x1234u);
    if(fast_state) publish_fast_state(&link,fast_state,fast_at);
    if(haptic_state) publish_haptic_state(&link,haptic_state,haptic_at);
    return gl30_menu_session_zero_confirmed(&session,&link,sent_nonce,sent_us,now_us);
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
        payload,sizeof(payload),frame,sizeof(frame),&length)==0 &&
        length==GL30_FRAME_HEADER_BYTES+GL30_HAPTIC_STATE_LEN);
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
    CHECK(receiver.stats.haptic_frames==2 && receiver.stats.bad_float_frames==0);
    CHECK(receiver.latest_haptic.status==GL30_HAPTIC_STATE_CONTROL_RELEASED);
    gl30_motor_state_fast_t ready={.angleRad=0.25f,.motorState=GL30_STARTUP_READY,
        .encoderStatus=1u};
    uint8_t fast_payload[GL30_MOTOR_STATE_FAST_LEN];
    CHECK(gl30_encode_motor_state_fast(&ready,fast_payload,sizeof(fast_payload))==0);
    CHECK(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_FAST,0,103,13000,
        fast_payload,sizeof(fast_payload),frame,sizeof(frame),&length)==0);
    CHECK(gl30_esp_link_feed(&receiver,frame,length,24100)==0);
    gl30_menu_session session;
    gl30_menu_session_init(&session,0x1234u);
    session.menu=true; session.armed=true; session.configured=true;
    session.ui_updated_us=24500u; session.requested_us=24500u;
    session.command=(gl30_haptic_command_t){.profileId=GL30_MENU_PROFILE_ID,
        .commandNonce=17u,.detentWidthRad=GL30_MENU_WIDTH_RAD,
        .modeFlags=GL30_HAPTIC_DETENT};
    gl30_menu_sample sample={0};
    CHECK(gl30_menu_session_step(&session,&receiver,24500u,&sample));
    CHECK(!sample.valid && !session.acknowledged);

    state.status=3u; /* Craft malformed wire bits after the encoder rejects them. */
    CHECK(gl30_encode_haptic_state(&state,payload,sizeof(payload))==0);
    payload[32]=16u;
    CHECK(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE,0,104,14000,
        payload,sizeof(payload),frame,sizeof(frame),&length)==0);
    CHECK(gl30_esp_link_feed(&receiver,frame,length,24600)==0);
    CHECK(receiver.stats.haptic_frames==2 && receiver.stats.bad_float_frames==1);
    state.status=3; state.subPosition=NAN;
    CHECK(gl30_encode_haptic_state(&state,payload,sizeof(payload))==0);
    CHECK(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE,0,105,15000,
        payload,sizeof(payload),frame,sizeof(frame),&length)==0);
    CHECK(gl30_esp_link_feed(&receiver,frame,length,24700)==0);
    CHECK(receiver.stats.haptic_frames==2 && receiver.stats.bad_float_frames==2);
    /* An STM reboot (both its sequence and clock go backwards) invalidates
     * the old applied-state snapshot before a new session may be armed. */
    sequence=199; fast(&receiver,2,26000); CHECK(receiver.has_haptic);
    gl30_motor_state_fast_t rebooted={.encoderStatus=1,.motorState=7};
    uint8_t reboot_fast_payload[GL30_MOTOR_STATE_FAST_LEN];
    CHECK(gl30_encode_motor_state_fast(&rebooted,reboot_fast_payload,sizeof(reboot_fast_payload))==0);
    CHECK(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_FAST,0,1,1000,
        reboot_fast_payload,sizeof(reboot_fast_payload),frame,sizeof(frame),&length)==0);
    CHECK(gl30_esp_link_feed(&receiver,frame,length,30000)==0);
    CHECK(!receiver.has_haptic && receiver.has_fast);
    state.subPosition=0; state.logicalPosition=0;
    CHECK(gl30_encode_haptic_state(&state,payload,sizeof(payload))==0);
    CHECK(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE,0,2,1500,
        payload,sizeof(payload),frame,sizeof(frame),&length)==0);
    CHECK(gl30_esp_link_feed(&receiver,frame,length,31000)==0 && receiver.has_haptic);
    CHECK(receiver.latest_haptic.logicalPosition==0);
}

static void healthy_unarmed_zero_heartbeat(void) {
    gl30_esp_link_t link; gl30_menu_session session; gl30_menu_sample sample;
    gl30_esp_link_init(&link); gl30_menu_session_init(&session,91);
    gl30_menu_session_desire(&session,false,4,1000);
    fast(&link,0.8f,1000);
    link.latest_fast.motorState=GL30_STARTUP_READY;
    CHECK(gl30_menu_session_step(&session,&link,1000,&sample));
    CHECK(!session.armed && !sample.valid && session.command.modeFlags==0 &&
          session.command.userTorqueLimitNm==0);
    uint32_t nonce=session.command.commandNonce;
    fast(&link,0.9f,2000); /* Active is also a healthy receiver state. */
    CHECK(gl30_menu_session_step(&session,&link,2000,&sample));
    CHECK(!session.armed && !sample.valid && session.command.commandNonce==nonce &&
          session.command.userTorqueLimitNm==0);
}

static void zero_confirmation_requires_post_tx_fresh_matching_echo(void) {
    const uint64_t sent=100000u;
    gl30_motor_state_fast_t fast_state=zero_ready_fast();
    gl30_haptic_state_t haptic_state=zero_ready_haptic(ZERO_TEST_NONCE);
    CHECK(!zero_result(NULL,NULL,sent,sent,sent,sent,ZERO_TEST_NONCE));
    CHECK(!zero_result(&fast_state,NULL,sent,sent+2u,sent+2u,sent+2u,ZERO_TEST_NONCE));
    CHECK(!zero_result(NULL,&haptic_state,sent,sent+2u,sent+2u,sent+2u,ZERO_TEST_NONCE));

    /* The TX time is a lower bound, not a 20 ms arrival deadline. */
    CHECK(zero_result(&fast_state,&haptic_state,sent,sent+100000u,sent+100000u,
                      sent+100000u,ZERO_TEST_NONCE));
    CHECK(zero_result(&fast_state,&haptic_state,sent,sent+2u,sent+2u,
                      sent+2u+GL30_MOTOR_FAST_FRESH_US-1u,ZERO_TEST_NONCE));
    CHECK(!zero_result(&fast_state,&haptic_state,sent,sent+2u,sent+2u,
                       sent+2u+GL30_MOTOR_FAST_FRESH_US,ZERO_TEST_NONCE));
    CHECK(!zero_result(&fast_state,&haptic_state,sent,sent+2u,sent+2u,
                       sent+2u+GL30_MOTOR_FAST_FRESH_US+1u,ZERO_TEST_NONCE));
    CHECK(!zero_result(&fast_state,&haptic_state,sent,sent-1u,sent+1u,
                       sent+2u,ZERO_TEST_NONCE));
    CHECK(!zero_result(&fast_state,&haptic_state,sent,sent+1u,sent-1u,
                       sent+2u,ZERO_TEST_NONCE));
    CHECK(!zero_result(&fast_state,&haptic_state,sent,sent+1u,sent+1u,
                       sent-1u,ZERO_TEST_NONCE));

    /* Check each input's freshness independently at the exact 20 ms edge. */
    CHECK(!zero_result(&fast_state,&haptic_state,sent,sent+1u,sent+20001u,
                       sent+20001u,ZERO_TEST_NONCE));
    CHECK(!zero_result(&fast_state,&haptic_state,sent,sent+20001u,sent+1u,
                       sent+20001u,ZERO_TEST_NONCE));
}

static void zero_confirmation_rejects_future_samples_and_zero_arguments(void) {
    const uint64_t sent=700000u,now=sent+10u;
    gl30_motor_state_fast_t fast_state=zero_ready_fast();
    gl30_haptic_state_t haptic_state=zero_ready_haptic(ZERO_TEST_NONCE);
    CHECK(!zero_result(&fast_state,&haptic_state,sent,now+1u,now,now,ZERO_TEST_NONCE));
    CHECK(!zero_result(&fast_state,&haptic_state,sent,now,now+1u,now,ZERO_TEST_NONCE));
    CHECK(!zero_result(&fast_state,&haptic_state,0u,now,now,now,ZERO_TEST_NONCE));

    gl30_esp_link_t link; gl30_menu_session session;
    gl30_esp_link_init(&link); gl30_menu_session_init(&session,0x1234u);
    const uint64_t at=sent+1u;
    fast_state=zero_ready_fast();
    haptic_state=zero_ready_haptic(0u);
    publish_fast_state(&link,&fast_state,at);
    publish_haptic_state(&link,&haptic_state,at);
    session.command.commandNonce=0u;
    CHECK(!gl30_menu_session_zero_confirmed(&session,&link,0u,sent,at));
}

static void zero_confirmation_rejects_nonfinite_mutated_parsed_link(void) {
    gl30_esp_link_t link; gl30_menu_session session;
    gl30_esp_link_init(&link); gl30_menu_session_init(&session,0x1234u);
    const uint32_t nonce=session.command.commandNonce;
    const uint64_t sent=710000u,at=sent+1u;
    gl30_motor_state_fast_t fast_state=zero_ready_fast();
    gl30_haptic_state_t haptic_state=zero_ready_haptic(nonce);
    publish_fast_state(&link,&fast_state,at);
    publish_haptic_state(&link,&haptic_state,at);
    CHECK(gl30_menu_session_zero_confirmed(&session,&link,nonce,sent,at));

    link.latest_fast.angleRad=NAN;
    CHECK(!gl30_menu_session_zero_confirmed(&session,&link,nonce,sent,at));
    link.latest_fast.angleRad=INFINITY;
    CHECK(!gl30_menu_session_zero_confirmed(&session,&link,nonce,sent,at));
    link.latest_fast.angleRad=0.25f;
    link.latest_haptic.subPosition=NAN;
    CHECK(!gl30_menu_session_zero_confirmed(&session,&link,nonce,sent,at));
    link.latest_haptic.subPosition=INFINITY;
    CHECK(!gl30_menu_session_zero_confirmed(&session,&link,nonce,sent,at));
}

static void zero_confirmation_rejects_unhealthy_wire_fields(void) {
    const uint64_t sent=300000u,at=sent+1u;
    gl30_motor_state_fast_t fast_state=zero_ready_fast();
    gl30_haptic_state_t haptic_state=zero_ready_haptic(ZERO_TEST_NONCE);

    fast_state.motorState=GL30_STARTUP_ACTIVE;
    CHECK(!zero_result(&fast_state,&haptic_state,sent,at,at,at,ZERO_TEST_NONCE));
    fast_state=zero_ready_fast(); fast_state.faultBits=1u;
    CHECK(!zero_result(&fast_state,&haptic_state,sent,at,at,at,ZERO_TEST_NONCE));
    fast_state=zero_ready_fast(); fast_state.encoderStatus=0u;
    CHECK(!zero_result(&fast_state,&haptic_state,sent,at,at,at,ZERO_TEST_NONCE));
    fast_state=zero_ready_fast(); fast_state.angleRad=NAN;
    CHECK(!zero_result(&fast_state,&haptic_state,sent,at,at,at,ZERO_TEST_NONCE));

    fast_state=zero_ready_fast(); haptic_state=zero_ready_haptic(ZERO_TEST_NONCE);
    haptic_state.motorState=GL30_STARTUP_ACTIVE;
    CHECK(!zero_result(&fast_state,&haptic_state,sent,at,at,at,ZERO_TEST_NONCE));
    haptic_state=zero_ready_haptic(ZERO_TEST_NONCE); haptic_state.faultBits=1u;
    CHECK(!zero_result(&fast_state,&haptic_state,sent,at,at,at,ZERO_TEST_NONCE));
    haptic_state=zero_ready_haptic(ZERO_TEST_NONCE); haptic_state.profileId=1u;
    CHECK(!zero_result(&fast_state,&haptic_state,sent,at,at,at,ZERO_TEST_NONCE));
    haptic_state=zero_ready_haptic(ZERO_TEST_NONCE); haptic_state.modeFlags=1u;
    CHECK(!zero_result(&fast_state,&haptic_state,sent,at,at,at,ZERO_TEST_NONCE));
    haptic_state=zero_ready_haptic(ZERO_TEST_NONCE); haptic_state.detentWidthRad=0.1f;
    CHECK(!zero_result(&fast_state,&haptic_state,sent,at,at,at,ZERO_TEST_NONCE));
    haptic_state=zero_ready_haptic(ZERO_TEST_NONCE); haptic_state.status=0u;
    CHECK(!zero_result(&fast_state,&haptic_state,sent,at,at,at,ZERO_TEST_NONCE));
    haptic_state=zero_ready_haptic(ZERO_TEST_NONCE);
    haptic_state.status|=GL30_HAPTIC_STATE_DETENT_READY;
    haptic_state.detentWidthRad=GL30_MENU_WIDTH_RAD;
    CHECK(!zero_result(&fast_state,&haptic_state,sent,at,at,at,ZERO_TEST_NONCE));
    haptic_state=zero_ready_haptic(ZERO_TEST_NONCE+1u);
    CHECK(!zero_result(&fast_state,&haptic_state,sent,at,at,at,ZERO_TEST_NONCE));
    haptic_state=zero_ready_haptic(ZERO_TEST_NONCE); haptic_state.subPosition=NAN;
    CHECK(!zero_result(&fast_state,&haptic_state,sent,at,at,at,ZERO_TEST_NONCE));
}

static void zero_confirmation_requires_every_command_config_field_zero_and_is_read_only(void) {
    gl30_menu_session session; gl30_esp_link_t link;
    gl30_menu_session_init(&session,0x1234u); gl30_esp_link_init(&link);
    const uint32_t nonce=session.command.commandNonce;
    const uint64_t sent=500000u,at=sent+1u;
    gl30_motor_state_fast_t fast_state=zero_ready_fast();
    gl30_haptic_state_t haptic_state=zero_ready_haptic(nonce);
    publish_fast_state(&link,&fast_state,at);
    publish_haptic_state(&link,&haptic_state,at);
    CHECK(gl30_menu_session_command_is_zero(&session.command));
    const gl30_menu_session session_before=session;
    const gl30_esp_link_t link_before=link;
    CHECK(gl30_menu_session_zero_confirmed(&session,&link,nonce,sent,at));
    CHECK(memcmp(&session,&session_before,sizeof(session))==0);
    CHECK(memcmp(&link,&link_before,sizeof(link))==0);

    float *float_fields[]={&session.command.targetPositionRad,
        &session.command.targetVelocityRadS,&session.command.detentWidthRad,
        &session.command.detentStrengthNm,&session.command.endstopMinRad,
        &session.command.endstopMaxRad,&session.command.endstopStrengthNm,
        &session.command.dampingNmPerRadS,&session.command.inertiaKgM2,
        &session.command.frictionNm,&session.command.userTorqueLimitNm,
        &session.command.activeSpeedLimitRadS};
    for(size_t i=0u;i<sizeof(float_fields)/sizeof(float_fields[0]);++i) {
        *float_fields[i]=0.001f;
        CHECK(!gl30_menu_session_zero_confirmed(&session,&link,nonce,sent,at));
        *float_fields[i]=0.0f;
    }
    uint32_t *integer_fields[]={&session.command.profileId,&session.command.modeFlags,
        &session.command.textureId};
    for(size_t i=0u;i<sizeof(integer_fields)/sizeof(integer_fields[0]);++i) {
        *integer_fields[i]=1u;
        CHECK(!gl30_menu_session_zero_confirmed(&session,&link,nonce,sent,at));
        *integer_fields[i]=0u;
    }
    CHECK(!gl30_menu_session_zero_confirmed(&session,&link,nonce+1u,sent,at));
    session.command.commandNonce=nonce+1u;
    CHECK(!gl30_menu_session_zero_confirmed(&session,&link,nonce,sent,at));
    session.command.commandNonce=nonce;

    session.armed=true;
    const gl30_menu_session armed_before=session;
    CHECK(!gl30_menu_session_zero_confirmed(&session,&link,nonce,sent,at));
    CHECK(memcmp(&session,&armed_before,sizeof(session))==0);
    CHECK(memcmp(&link,&link_before,sizeof(link))==0);
}

static void motor_feedback_projection_contract(void) {
    static const struct { uint32_t raw; gl30_motor_feedback_state projected; } cases[]={
        {GL30_STARTUP_POWER_CHECK,GL30_MOTOR_CHECKING},
        {GL30_STARTUP_SELF_TEST,GL30_MOTOR_CHECKING},
        {GL30_STARTUP_ENCODER_CHECK,GL30_MOTOR_CHECKING},
        {GL30_STARTUP_DRIVER_CHECK,GL30_MOTOR_CHECKING},
        {GL30_STARTUP_ADC_ZERO_CHECK,GL30_MOTOR_CHECKING},
        {GL30_STARTUP_FOC_ALIGN,GL30_MOTOR_ALIGN},
        {GL30_STARTUP_READY,GL30_MOTOR_READY},
        {GL30_STARTUP_ACTIVE,GL30_MOTOR_ACTIVE},
        {GL30_STARTUP_FAULT_LATCHED,GL30_MOTOR_FAULT},
        {99u,GL30_MOTOR_INVALID},
    };
    gl30_esp_link_t link; gl30_esp_link_init(&link);
    gl30_motor_feedback value=gl30_menu_session_feedback(&link,10000u);
    CHECK(!link.has_fast && value.state==GL30_MOTOR_OFFLINE &&
          value.fault_bits==0 && value.received_us==0);

    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        gl30_esp_link_init(&link);
        uint64_t at=1000u+(uint64_t)i*1000u;
        publish_fast(&link,cases[i].raw,0,1,0.75f,at);
        value=gl30_menu_session_feedback(&link,at);
        CHECK(value.state==cases[i].projected && value.received_us==at &&
              value.fault_bits==0);
    }

    gl30_esp_link_init(&link);
    publish_fast(&link,GL30_STARTUP_READY,0x52u,1,0.75f,30000u);
    value=gl30_menu_session_feedback(&link,30000u);
    CHECK(value.state==GL30_MOTOR_FAULT && value.fault_bits==0x52u);

    gl30_esp_link_init(&link);
    publish_fast(&link,GL30_STARTUP_READY,0,0,0.75f,40000u);
    CHECK(gl30_menu_session_feedback(&link,40000u).state==GL30_MOTOR_INVALID);
    gl30_esp_link_init(&link);
    publish_fast(&link,GL30_STARTUP_ACTIVE,0,2,0.75f,41000u);
    CHECK(gl30_menu_session_feedback(&link,41000u).state==GL30_MOTOR_INVALID);
    gl30_esp_link_init(&link);
    publish_fast(&link,GL30_STARTUP_READY,0,1,0.75f,42000u);
    link.latest_fast.angleRad=NAN;
    CHECK(gl30_menu_session_feedback(&link,42000u).state==GL30_MOTOR_INVALID);
    link.latest_fast.angleRad=INFINITY;
    CHECK(gl30_menu_session_feedback(&link,42000u).state==GL30_MOTOR_INVALID);

    gl30_esp_link_init(&link);
    publish_fast(&link,GL30_STARTUP_ACTIVE,0,1,1.0f,50000u);
    value=gl30_menu_session_feedback(&link,69999u);
    CHECK(value.state==GL30_MOTOR_ACTIVE && value.received_us==50000u);
    value=gl30_menu_session_feedback(&link,70000u);
    CHECK(value.state==GL30_MOTOR_OFFLINE && value.fault_bits==0);
    value=gl30_menu_session_feedback(&link,49999u);
    CHECK(value.state==GL30_MOTOR_OFFLINE && value.fault_bits==0);

    const gl30_motor_feedback captured={50000u,0x1234u,GL30_MOTOR_FAULT};
    value=gl30_motor_feedback_current(captured,69999u);
    CHECK(value.state==GL30_MOTOR_FAULT && value.fault_bits==0x1234u);
    value=gl30_motor_feedback_current(captured,70000u);
    CHECK(value.state==GL30_MOTOR_OFFLINE && value.fault_bits==0);
    value=gl30_motor_feedback_current(captured,49999u);
    CHECK(value.state==GL30_MOTOR_OFFLINE && value.fault_bits==0);
    CHECK(captured.state==GL30_MOTOR_FAULT && captured.fault_bits==0x1234u &&
          captured.received_us==50000u);

    gl30_menu_session session; gl30_menu_session_init(&session,77u);
    gl30_menu_session_arm(&session,true);
    const gl30_menu_session session_before=session;
    const gl30_esp_link_t link_before=link;
    (void)gl30_menu_session_feedback(&link,50000u);
    CHECK(memcmp(&link,&link_before,sizeof(link))==0 &&
          memcmp(&session,&session_before,sizeof(session))==0 && session.armed);
}

static void motor_feedback_latched_fault_freshness_contract(void) {
    gl30_esp_link_t link; gl30_menu_session session;
    gl30_esp_link_init(&link); gl30_menu_session_init(&session,78u);
    gl30_menu_session_arm(&session,true);
    const uint64_t received_us=100000u;
    publish_fast(&link,GL30_STARTUP_FAULT_LATCHED,0x52u,1,0.75f,received_us);
    CHECK(link.has_fast && link.latest_fast.motorState==GL30_STARTUP_FAULT_LATCHED &&
          link.latest_fast.faultBits==0x52u && link.last_fast_us==received_us);
    const gl30_motor_state_fast_t raw_fast=link.latest_fast;
    const gl30_menu_session session_before=session;

    gl30_motor_feedback feedback=gl30_menu_session_feedback(&link,received_us+19999u);
    CHECK(feedback.state==GL30_MOTOR_FAULT && feedback.fault_bits==0x52u &&
          feedback.received_us==received_us);
    feedback=gl30_menu_session_feedback(&link,received_us+20000u);
    CHECK(feedback.state==GL30_MOTOR_OFFLINE && feedback.fault_bits==0);
    feedback=gl30_menu_session_feedback(&link,received_us+20001u);
    CHECK(feedback.state==GL30_MOTOR_OFFLINE && feedback.fault_bits==0);

    CHECK(link.has_fast && link.last_fast_us==received_us &&
          link.latest_fast.motorState==raw_fast.motorState &&
          link.latest_fast.faultBits==raw_fast.faultBits);
    CHECK(session.armed && memcmp(&session,&session_before,sizeof(session))==0);
}

int main(void) {
    motor_feedback_projection_contract();
    motor_feedback_latched_fault_freshness_contract();
    haptic_wire_validation();
    healthy_unarmed_zero_heartbeat();
    zero_confirmation_requires_post_tx_fresh_matching_echo();
    zero_confirmation_rejects_future_samples_and_zero_arguments();
    zero_confirmation_rejects_nonfinite_mutated_parsed_link();
    zero_confirmation_rejects_unhealthy_wire_fields();
    zero_confirmation_requires_every_command_config_field_zero_and_is_read_only();
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

    /* Actual STM haptics produce q/f, encoded through the real protocol into
     * the ESP session and the same model used by the screen. */
    gl30_foc_state_t motor; gl30_foc_init(&motor);
    gl30_foc_observer_tick_4k(&motor,1.2f,true);
    CHECK(gl30_foc_apply_command(&motor,&session.command));
    gl30_haptic_tick_2k(&motor,true);
    CHECK(motor.detent_initialized && motor.detent_position==0);
    haptic(&link,&session,motor.detent_position,motor.detent_fraction,4000);
    CHECK(gl30_menu_session_step(&session,&link,4000,&sample) && sample.valid);
    CHECK(gl30_model_motor_menu(&ui,sample.epoch,sample.session,sample.position,sample.fraction,sample.valid));
    CHECK(ui.menu_index==0);
    CHECK(gl30_menu_session_step(&session,&link,5000,&sample)); CHECK(session.command.commandNonce==nonce);

    float prior=0;
    for(int i=0;i<=550;i++) {
        float phase=i/1000.0f;
        uint64_t now=6000+(uint64_t)i*500;
        gl30_foc_observer_tick_4k(&motor,1.2f+phase*GL30_MENU_WIDTH_RAD,true);
        gl30_haptic_tick_2k(&motor,true);
        fast(&link,motor.theta_unwrapped_rad,now);
        haptic(&link,&session,motor.detent_position,motor.detent_fraction,now+1);
        CHECK(gl30_menu_session_step(&session,&link,now+1,&sample) && sample.valid);
        gl30_model_motor_menu(&ui,sample.epoch,sample.session,sample.position,sample.fraction,true);
        CHECK(ui.menu_index==mod_menu(motor.detent_position));
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
    uint32_t stop_nonce=session.command.commandNonce;
    CHECK(gl30_menu_session_step(&session,&link,400002,&sample) && !session.armed &&
          session.command.modeFlags==0 && session.command.userTorqueLimitNm==0 &&
          session.command.commandNonce==stop_nonce);
    fast(&link,2,400003);
    CHECK(gl30_menu_session_step(&session,&link,400003,&sample) && !session.armed &&
          session.command.userTorqueLimitNm==0 && session.command.commandNonce==stop_nonce);

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
    uint32_t feedback_stop_nonce=session.command.commandNonce;
    gl30_menu_session_sent(&session);
    CHECK(gl30_menu_session_step(&session,&link,1300003,&sample) && !session.armed &&
          !sample.valid && session.command.userTorqueLimitNm==0 &&
          session.command.commandNonce==feedback_stop_nonce);
    /* Valid angle alone must not arm an encoder-check/alignment state. */
    link.latest_fast.motorState=2; gl30_menu_session_arm(&session,true);
    CHECK(gl30_menu_session_step(&session,&link,1300004,&sample)); CHECK(!session.armed);
    gl30_menu_session_sent(&session);
    CHECK(!gl30_menu_session_step(&session,&link,1300005,&sample) && !session.armed);

    gl30_foc_force_zero(&motor); CHECK(!motor.detent_initialized && motor.detent_fraction==0);
    printf("motor-menu: %u checks passed\n",checks);
    return 0;
}
