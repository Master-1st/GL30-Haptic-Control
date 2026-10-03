#include "gl30_motor.h"
#include "gl30_motor_arm_gate.h"
#include "safety_supervisor.h"
#include "driver/uart.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

/* Waveshare J1.11 U0TXD=GPIO43, J1.12 U0RXD=GPIO44, 3.3 V.
 * USB Serial/JTAG owns logs. Never send text on this binary UART. */
static const uart_port_t port=UART_NUM_0;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t task;
static QueueHandle_t events;
static esp_timer_handle_t timer;
static gl30_esp_link_t link;
static gl30_menu_session session;
static gl30_motor_status status;
static gl30_motor_arm_gate_t arm_gate;
static bool desired_menu;
static uint32_t desired_epoch,tx_sequence;
static uint64_t desired_published_us;
typedef enum {
    CONTROL_DISCOVER,CONTROL_STOP_OLD,CONTROL_RELEASE,
    CONTROL_ACQUIRE,CONTROL_FIRST_ZERO,CONTROL_RUNNING,CONTROL_HELD,CONTROL_FAILED
} control_phase;
static control_phase control;
static uint64_t lease_generation,lease_counter,candidate_generation;
static uint64_t control_sent_us,control_last_tx_us,control_zero_sent_us;
static gl30_control_lease_request_t control_request;
/* Request/claim/finish are shared only through lock. The owner alone changes
 * wire state. A claim holds ARM disabled even if later feedback is lost. */
static uint64_t maintenance_token,maintenance_sequence;
static bool maintenance_claimed,maintenance_finish;

void gl30_motor_desire_menu(bool active,uint32_t epoch) {
    uint64_t now=(uint64_t)esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    desired_menu=active; desired_epoch=epoch; desired_published_us=now;
    portEXIT_CRITICAL(&lock);
}
bool gl30_motor_arm(uint64_t captured_stop_generation) {
    portENTER_CRITICAL(&lock);
    bool accepted=status.control_ready &&
        gl30_motor_arm_gate_arm(&arm_gate,captured_stop_generation);
    portEXIT_CRITICAL(&lock);
    return accepted;
}
void gl30_motor_stop(void) {
    portENTER_CRITICAL(&lock); gl30_motor_arm_gate_stop(&arm_gate); portEXIT_CRITICAL(&lock);
}
uint64_t gl30_motor_maintenance_begin(void) {
    uint64_t token=0;
    portENTER_CRITICAL(&lock);
    if(status.control_ready && !status.armed && !arm_gate.arm_request &&
       maintenance_token==0 && maintenance_sequence!=UINT64_MAX) {
        gl30_motor_arm_gate_stop(&arm_gate);
        arm_gate.maintenance_active=true;
        maintenance_token=++maintenance_sequence;
        maintenance_finish=maintenance_claimed=false;
        token=maintenance_token;
    }
    portEXIT_CRITICAL(&lock);
    return token;
}
bool gl30_motor_maintenance_claim(uint64_t token) {
    bool accepted;
    portENTER_CRITICAL(&lock);
    accepted=token!=0 && token==maintenance_token && !maintenance_finish &&
        !maintenance_claimed && arm_gate.maintenance_active &&
        !arm_gate.arm_request && !status.armed && status.maintenance_ready;
    if(accepted) maintenance_claimed=true;
    portEXIT_CRITICAL(&lock);
    return accepted;
}
bool gl30_motor_maintenance_held(uint64_t token) {
    bool held;
    portENTER_CRITICAL(&lock);
    held=token!=0 && token==maintenance_token && maintenance_claimed &&
        arm_gate.maintenance_active && !maintenance_finish && !arm_gate.arm_request;
    portEXIT_CRITICAL(&lock);
    return held;
}
void gl30_motor_maintenance_end(uint64_t token) {
    portENTER_CRITICAL(&lock);
    if(token!=0 && token==maintenance_token) {
        maintenance_claimed=false; maintenance_finish=true;
        gl30_motor_arm_gate_stop(&arm_gate);
    }
    portEXIT_CRITICAL(&lock);
}
void gl30_motor_snapshot(gl30_motor_status *out) {
    uint64_t published_stop_generation;
    uint64_t now_us;
    portENTER_CRITICAL(&lock);
    *out=status;
    published_stop_generation=status.stop_generation;
    out->stop_generation=arm_gate.stop_generation;
    if(arm_gate.maintenance_active || arm_gate.arm_request || status.armed ||
       published_stop_generation!=arm_gate.stop_generation) out->zero_confirmed=false;
    portEXIT_CRITICAL(&lock);
    now_us=(uint64_t)esp_timer_get_time();
    out->feedback=gl30_motor_feedback_current(out->feedback,now_us);
    if(out->zero_confirmed &&
       (out->zero_sent_us==0u || out->zero_sent_us>now_us ||
        out->zero_feedback_us<out->zero_sent_us || now_us<out->zero_feedback_us ||
        now_us-out->zero_feedback_us>=GL30_MOTOR_FAST_FRESH_US ||
        out->feedback.state!=GL30_MOTOR_READY || out->feedback.received_us==0u ||
        now_us<out->feedback.received_us ||
        now_us-out->feedback.received_us>=GL30_MOTOR_FAST_FRESH_US)) {
        out->zero_confirmed=false;
    }
    portENTER_CRITICAL(&lock);
    out->stop_generation=arm_gate.stop_generation;
    if(out->zero_confirmed &&
       (arm_gate.maintenance_active || arm_gate.arm_request || status.armed || !status.zero_confirmed ||
        status.stop_generation!=arm_gate.stop_generation ||
        published_stop_generation!=arm_gate.stop_generation ||
        status.zero_command_nonce!=out->zero_command_nonce ||
        status.zero_sent_us!=out->zero_sent_us ||
        status.zero_feedback_us!=out->zero_feedback_us)) {
        out->zero_confirmed=false;
    }
    portEXIT_CRITICAL(&lock);
}
static void wake(void *arg) { (void)arg; xTaskNotifyGive(task); }
static bool apply_newer_stop(uint64_t *observed_stop_generation,
                             uint64_t *applied_stop_generation) {
    uint64_t live_stop_generation;
    portENTER_CRITICAL(&lock);
    live_stop_generation=arm_gate.stop_generation;
    portEXIT_CRITICAL(&lock);
    if(live_stop_generation==*observed_stop_generation) return false;
    if(control==CONTROL_RUNNING) gl30_menu_session_stop(&session);
    else session.armed=false; /* STOP is fenced now; frozen handshake stays zero. */
    *observed_stop_generation=live_stop_generation;
    *applied_stop_generation=live_stop_generation;
    return true;
}
static void owner_disarm(void) {
    gl30_motor_stop();
    gl30_menu_session_arm(&session,false);
}
static bool owner_session_step(uint64_t now_us,gl30_menu_sample *sample) {
    bool was_armed=session.armed;
    bool send=gl30_menu_session_step(&session,&link,now_us,sample);
    if(was_armed && !session.armed) gl30_motor_stop();
    return send;
}
static void control_transition(control_phase phase) {
    control=phase; control_sent_us=control_last_tx_us=control_zero_sent_us=0;
}
static bool fresh_haptic(uint64_t now,uint64_t after) {
    return after!=0 && now>=after && link.has_haptic &&
        link.last_haptic_us>=after && now>=link.last_haptic_us &&
        now-link.last_haptic_us<GL30_MOTOR_FAST_FRESH_US;
}
static bool zero_echo(uint64_t now,uint64_t after,uint64_t generation,
                      uint32_t nonce,uint32_t control_bits) {
    const gl30_haptic_state_t *h=&link.latest_haptic;
    return generation!=0 && nonce!=0 && fresh_haptic(now,after) &&
        h->leaseGeneration==generation && h->commandNonce==nonce &&
        h->profileId==0 && h->modeFlags==0 && h->detentWidthRad==0.0f &&
        isfinite(h->subPosition) && h->motorState<=GL30_STARTUP_FAULT_LATCHED &&
        (h->status&~GL30_HAPTIC_STATE_ENCODER_VALID)==control_bits;
}
static void control_query(void) {
    control_request=(gl30_control_lease_request_t){.action=GL30_CONTROL_QUERY};
    control_transition(CONTROL_DISCOVER);
}
static void control_new_zero(void) {
    gl30_menu_session_stop(&session);
    session.command.leaseGeneration=lease_generation;
    control_zero_sent_us=0;
}
static bool control_acquire(void) {
    if(lease_counter==UINT64_MAX) { control_transition(CONTROL_FAILED); return false; }
    candidate_generation=++lease_counter;
    if(candidate_generation==lease_generation) {
        if(lease_counter==UINT64_MAX) { control_transition(CONTROL_FAILED); return false; }
        candidate_generation=++lease_counter;
    }
    control_new_zero();
    if(session.command.commandNonce==link.latest_haptic.commandNonce)
        control_new_zero();
    session.command.leaseGeneration=candidate_generation;
    control_request=(gl30_control_lease_request_t){GL30_CONTROL_ACQUIRE,
        session.command.commandNonce,lease_generation,candidate_generation};
    control_transition(CONTROL_ACQUIRE);
    return true;
}
static void control_release(void) {
    control_request=(gl30_control_lease_request_t){GL30_CONTROL_RELEASE,
        session.command.commandNonce,lease_generation,0};
    control_transition(CONTROL_RELEASE);
}
static void record_zero_sent(gl30_motor_status *next,uint64_t at) {
    if(session.command.commandNonce!=next->zero_command_nonce ||
       session.command.leaseGeneration!=next->lease_generation) {
        next->zero_command_nonce=session.command.commandNonce;
        next->zero_sent_us=at; next->zero_feedback_us=0; next->zero_confirmed=false;
    }
}
static bool control_send(gl30_motor_status *next,uint64_t now,bool request) {
    uint8_t frame[GL30_FRAME_HEADER_BYTES+GL30_HAPTIC_COMMAND_LEN];
    uint8_t payload[GL30_CONTROL_LEASE_LEN];
    size_t length=0;
    if(uart_wait_tx_done(port,0)!=ESP_OK) return false;
    int error;
    if(request) {
        error=gl30_encode_control_lease(&control_request,payload,sizeof(payload));
        if(!error) error=gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_CONTROL_LEASE,0,
            tx_sequence++,now,payload,sizeof(payload),frame,sizeof(frame),&length);
    } else {
        session.command.leaseGeneration=lease_generation;
        error=gl30_esp_link_build_haptic_command_frame(&session.command,0,
            tx_sequence++,now,frame,sizeof(frame),&length);
    }
    if(error || uart_tx_chars(port,(const char *)frame,(uint32_t)length)!=(int)length) {
        ++next->tx_errors; owner_disarm(); return false;
    }
    ++next->tx_frames;
    uint64_t at=(uint64_t)esp_timer_get_time();
    if(request) {
        if(control_sent_us==0) control_sent_us=at;
        control_last_tx_us=at;
    } else {
        if(control_zero_sent_us==0) control_zero_sent_us=at;
        record_zero_sent(next,at); gl30_menu_session_sent(&session);
    }
    return true;
}
/* Runs on the sole UART owner. Software lease acknowledgments intentionally
 * do not require physical calibration/READY and never authorize output. */
static bool control_poll(gl30_motor_status *next,uint64_t now) {
    uint64_t token; bool finish;
    portENTER_CRITICAL(&lock);
    token=maintenance_token; finish=maintenance_finish;
    portEXIT_CRITICAL(&lock);
    if(control==CONTROL_RUNNING) {
        if(link.has_haptic && now>=link.last_haptic_us &&
           now-link.last_haptic_us<GL30_MOTOR_FAST_FRESH_US &&
           (link.latest_haptic.leaseGeneration!=lease_generation ||
            (link.latest_haptic.status&(GL30_HAPTIC_STATE_CONTROL_RELEASED|
                                      GL30_HAPTIC_STATE_CONTROL_WAITING_ZERO)))) {
            owner_disarm(); control_query();
        } else if(token!=0) {
            control_new_zero(); control_transition(CONTROL_STOP_OLD);
        } else return false;
    }
    if(control==CONTROL_STOP_OLD || control==CONTROL_RELEASE ||
       control==CONTROL_ACQUIRE || control==CONTROL_FIRST_ZERO) {
        uint64_t after=(control==CONTROL_STOP_OLD || control==CONTROL_FIRST_ZERO)?
            control_zero_sent_us:control_sent_us;
        bool changed=fresh_haptic(now,after) &&
            link.latest_haptic.leaseGeneration!=lease_generation &&
            !(control==CONTROL_ACQUIRE &&
              link.latest_haptic.leaseGeneration==candidate_generation);
        /* A reset peer or lost acknowledgment must not trap us forever on an
         * old generation. Rediscovery remains zero-only and never grants ARM.
         * HELD is excluded: Flash owns that reservation until explicit end. */
        if(changed || (after!=0 && now>=after && now-after>=500000u)) {
            owner_disarm(); control_query();
        }
    }
    next->sample=(gl30_menu_sample){.epoch=session.epoch};
    if(control==CONTROL_FAILED) return true;
    if(control==CONTROL_DISCOVER && fresh_haptic(now,control_sent_us)) {
        lease_generation=link.latest_haptic.leaseGeneration;
        if(lease_generation==0 || (link.latest_haptic.status&GL30_HAPTIC_STATE_CONTROL_RELEASED)) {
            (void)control_acquire();
        } else {
            control_new_zero(); control_transition(CONTROL_STOP_OLD);
            if(link.latest_haptic.status&GL30_HAPTIC_STATE_CONTROL_WAITING_ZERO) {
                /* A restarted ESP may find an acquire accepted by the old
                 * owner. Finish only that exact zero before releasing it. */
                session.command.commandNonce=link.latest_haptic.commandNonce;
                if(session.command.commandNonce==0) control_transition(CONTROL_FAILED);
            }
        }
    }
    if(control==CONTROL_STOP_OLD && zero_echo(now,control_zero_sent_us,
        lease_generation,session.command.commandNonce,0)) control_release();
    if(control==CONTROL_RELEASE && zero_echo(now,control_sent_us,
        lease_generation,control_request.zeroNonce,GL30_HAPTIC_STATE_CONTROL_RELEASED)) {
        if(token!=0 && !finish) control_transition(CONTROL_HELD);
        else (void)control_acquire();
    }
    if(control==CONTROL_ACQUIRE &&
       (zero_echo(now,control_sent_us,candidate_generation,control_request.zeroNonce,
                  GL30_HAPTIC_STATE_CONTROL_WAITING_ZERO) ||
        zero_echo(now,control_sent_us,candidate_generation,control_request.zeroNonce,0))) {
        lease_generation=candidate_generation;
        control_transition(CONTROL_FIRST_ZERO);
    }
    if(control==CONTROL_FIRST_ZERO && zero_echo(now,control_zero_sent_us,
        lease_generation,session.command.commandNonce,0)) {
        control_transition(CONTROL_RUNNING);
        portENTER_CRITICAL(&lock);
        /* Inputs captured while control was unavailable cannot become an ARM
         * merely because the lease finished before the UI consumed them. */
        gl30_motor_arm_gate_stop(&arm_gate);
        if(maintenance_finish) {
            arm_gate.maintenance_active=false;
            maintenance_token=0; maintenance_finish=maintenance_claimed=false;
        }
        portEXIT_CRITICAL(&lock);
        return true; /* New explicit ARM may only be consumed on a later poll. */
    }
    if(control==CONTROL_HELD) {
        if(finish) {
            /* Discard anything buffered during Flash before establishing a
             * different generation; old bytes are never fresh angle evidence. */
            uart_flush_input(port); gl30_esp_link_init(&link);
            (void)control_acquire();
        } else return true;
    }
    if(control==CONTROL_STOP_OLD || control==CONTROL_FIRST_ZERO) {
        (void)control_send(next,now,false);
    } else if(control==CONTROL_RELEASE) {
        bool due=control_last_tx_us==0 || now<control_last_tx_us ||
            now-control_last_tx_us>=10000u;
        (void)control_send(next,now,due); /* Zero heartbeat until release ack. */
    } else if(control==CONTROL_DISCOVER || control==CONTROL_ACQUIRE) {
        if(control_last_tx_us==0 || now<control_last_tx_us ||
           now-control_last_tx_us>=10000u) (void)control_send(next,now,true);
    }
    return true;
}
static void run(void *arg) {
    (void)arg;
    uint8_t bytes[256],frame[GL30_FRAME_HEADER_BYTES+GL30_HAPTIC_COMMAND_LEN];
    uint64_t previous=0,applied_stop_generation=0;
    gl30_motor_status next={0};
    for(;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        uint64_t now=(uint64_t)esp_timer_get_time();
        if(previous && now-previous>next.max_poll_us) next.max_poll_us=(uint32_t)(now-previous);
        previous=now;
        bool want_menu,want_arm,change_arm;
        uint32_t epoch;
        uint64_t poll_stop_generation;
        uint64_t published_us;
        gl30_motor_arm_gate_request_t arm_state;
        portENTER_CRITICAL(&lock);
        want_menu=desired_menu; epoch=desired_epoch;
        published_us=desired_published_us;
        arm_state=gl30_motor_arm_gate_take(&arm_gate);
        want_arm=arm_state.arm_request; change_arm=arm_state.arm_changed;
        poll_stop_generation=arm_state.stop_generation;
        portEXIT_CRITICAL(&lock);
        if(poll_stop_generation!=applied_stop_generation) {
            if(control==CONTROL_RUNNING) gl30_menu_session_stop(&session);
            else session.armed=false;
            applied_stop_generation=poll_stop_generation;
        }
        if(control==CONTROL_RUNNING)
            gl30_menu_session_desire(&session,want_menu,epoch,published_us);
        uart_event_t event;
        while(xQueueReceive(events,&event,0)==pdTRUE) {
            if(event.type==UART_FIFO_OVF || event.type==UART_BUFFER_FULL ||
               event.type==UART_FRAME_ERR || event.type==UART_PARITY_ERR ||
               event.type==UART_BREAK || event.type==UART_DATA_BREAK) {
                ++next.rx_errors; uart_flush_input(port);
                gl30_esp_link_init(&link); owner_disarm();
                if(control!=CONTROL_HELD) control_query();
                change_arm=false;
            }
        }
        /* Bounded drain. An old RX backlog is unsafe as fresh angle evidence. */
        size_t buffered=0;
        uart_get_buffered_data_len(port,&buffered);
        if(buffered>2048) {
            ++next.rx_errors; uart_flush_input(port); gl30_esp_link_init(&link);
            owner_disarm(); change_arm=false;
            if(control!=CONTROL_HELD) control_query();
        } else for(unsigned batch=0;batch<8;batch++) {
            int n=uart_read_bytes(port,bytes,sizeof(bytes),0);
            if(n<=0) break;
            if(gl30_esp_link_feed(&link,bytes,(size_t)n,now)!=0) ++next.rx_errors;
        }
        if(change_arm && control==CONTROL_RUNNING)
            gl30_menu_session_arm(&session,want_arm);
        (void)apply_newer_stop(&poll_stop_generation,&applied_stop_generation);
        bool controlled=control_poll(&next,now);
        bool send=!controlled && owner_session_step(now,&next.sample);
        session.command.leaseGeneration=lease_generation;
        if(send && uart_wait_tx_done(port,0)==ESP_OK) {
            if(apply_newer_stop(&poll_stop_generation,&applied_stop_generation))
                send=owner_session_step(now,&next.sample);
            session.command.leaseGeneration=lease_generation;
            if(send) {
                size_t length=0;
                uint32_t sequence=tx_sequence++;
                int error=gl30_esp_link_build_haptic_command_frame(&session.command,0,
                    sequence,now,frame,sizeof(frame),&length);
                if(!error && apply_newer_stop(&poll_stop_generation,&applied_stop_generation)) {
                    send=owner_session_step(now,&next.sample);
                    session.command.leaseGeneration=lease_generation;
                    if(send) error=gl30_esp_link_build_haptic_command_frame(&session.command,0,
                        sequence,now,frame,sizeof(frame),&length);
                }
                if(send) {
                    if(error || uart_tx_chars(port,(const char *)frame,(uint32_t)length)!=(int)length) {
                        ++next.tx_errors; owner_disarm(); next.sample.valid=false;
                    } else {
                        ++next.tx_frames;
                        if(gl30_menu_session_command_is_zero(&session.command) &&
                           session.command.commandNonce!=0u)
                            record_zero_sent(&next,(uint64_t)esp_timer_get_time());
                        gl30_menu_session_sent(&session);
                    }
                }
            }
        }
        (void)apply_newer_stop(&poll_stop_generation,&applied_stop_generation);
        next.connected=gl30_esp_link_connected(&link,now);
        next.feedback=gl30_menu_session_feedback(&link,now);
        next.armed=session.armed;
        next.fast_frames=(uint32_t)link.stats.fast_frames;
        next.haptic_frames=(uint32_t)link.stats.haptic_frames;
        next.control_ready=control==CONTROL_RUNNING;
        next.lease_generation=lease_generation;
        next.zero_confirmed=next.control_ready && gl30_menu_session_zero_confirmed(
            &session,&link,next.zero_command_nonce,next.zero_sent_us,now);
        if(next.zero_confirmed) next.zero_feedback_us=link.last_haptic_us;
        portENTER_CRITICAL(&lock);
        next.zero_confirmed=next.zero_confirmed &&
            !arm_gate.maintenance_active && !arm_gate.arm_request && !next.armed && !status.armed &&
            poll_stop_generation==arm_gate.stop_generation;
        next.maintenance_token=maintenance_token;
        next.maintenance_ready=control==CONTROL_HELD && maintenance_token!=0 &&
            !maintenance_finish;
        next.stop_generation=poll_stop_generation;
        status=next;
        portEXIT_CRITICAL(&lock);
    }
}
bool gl30_motor_init(void) {
    uart_config_t config={.baud_rate=5000000,.data_bits=UART_DATA_8_BITS,
        .parity=UART_PARITY_DISABLE,.stop_bits=UART_STOP_BITS_1,
        .flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    if(uart_driver_install(port,8192,0,32,&events,0)!=ESP_OK) return false;
    if(uart_param_config(port,&config)!=ESP_OK ||
       uart_set_pin(port,43,44,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK) {
        uart_driver_delete(port); return false;
    }
    gl30_esp_link_init(&link); gl30_menu_session_init(&session,esp_random());
    lease_counter=((uint64_t)esp_random()<<32)|esp_random();
    lease_generation=candidate_generation=0;
    maintenance_token=maintenance_sequence=0;
    maintenance_claimed=maintenance_finish=false;
    control_query();
    if(xTaskCreatePinnedToCore(run,"gl30_motor",4096,NULL,4,&task,1)!=pdPASS) {
        uart_driver_delete(port); return false;
    }
    esp_timer_create_args_t args={.callback=wake,.name="gl30_motor_1k",.skip_unhandled_events=true};
    if(esp_timer_create(&args,&timer)!=ESP_OK) {
        vTaskDelete(task); uart_driver_delete(port); return false;
    }
    if(esp_timer_start_periodic(timer,1000)!=ESP_OK) {
        esp_timer_delete(timer); vTaskDelete(task); uart_driver_delete(port); return false;
    }
    return true;
}
