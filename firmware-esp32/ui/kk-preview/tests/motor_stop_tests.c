/* Run the production motor owner once per fake notification. */
#include "fake_idf.h"
#include <setjmp.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "safety_supervisor.h"
#include "v6_protocol.h"
#include "haptics.h"

static unsigned checks;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "motor-stop FAIL line %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static int64_t fake_now_us;
static jmp_buf owner_return;
static bool owner_return_ready;
static unsigned notify_tokens;
static uart_event_t fake_events[4];
static size_t fake_event_count;
static uint8_t fake_rx[256];
static size_t fake_rx_size, fake_rx_offset;
static uint32_t fake_rx_sequence;
static bool fake_backlog_once;
static uint8_t fake_tx[GL30_FRAME_HEADER_BYTES + GL30_HAPTIC_COMMAND_LEN];
static size_t fake_tx_size;
static unsigned fake_tx_count;
static bool fake_tx_short_once;
static bool fake_stop_on_tx_once;
static void (*fake_time_hook)(void);
static void (*wait_tx_hook)(void);
static bool wait_tx_hook_pending;
static void (*owner_wait_hook)(unsigned wait_index);
static unsigned owner_wait_index;
typedef enum { PEER_UNOWNED,PEER_OWNED,PEER_RELEASED,PEER_WAITING_ZERO } fake_peer_state;
static bool fake_peer_enabled;
static fake_peer_state fake_peer_lease_state;
static uint64_t fake_peer_generation;
static uint32_t fake_peer_nonce;
static unsigned fake_peer_queries,fake_peer_acquires,fake_peer_releases,fake_peer_zeros;
static unsigned fake_peer_drop_query_replies,fake_peer_drop_acquire_replies;
static unsigned fake_peer_drop_release_replies;
static unsigned fake_peer_seen_tx_count;
static unsigned fake_peer_reboot_wait;
static unsigned fake_peer_reboot_observed_wait;
static uint64_t fake_peer_reboot_generation;
static bool saw_rebooted_peer_before_control_ready;
static uint64_t captured_pre_ready_arm_token;
static bool captured_arm_token_while_not_ready;
static void prepare_confirmed_menu_session(void);

#include "../../../components/gl30_motor/src/gl30_motor.c"

static void run_owner_polls(unsigned count,void (*hook)(unsigned wait_index))
{
    notify_tokens = count;
    owner_return_ready = true;
    owner_wait_hook = hook;
    owner_wait_index = 0u;
    if (setjmp(owner_return) == 0) run(NULL);
    owner_return_ready = false;
    owner_wait_hook = NULL;
}

static void run_owner_poll(void)
{
    run_owner_polls(1u,NULL);
}

static void reset_fixture(void)
{
    fake_now_us = 1000000;
    notify_tokens = 0;
    fake_event_count = 0;
    fake_rx_size = 0;
    fake_rx_offset = 0;
    fake_rx_sequence = 1u;
    fake_backlog_once = false;
    fake_stop_on_tx_once = false;
    fake_tx_size = 0;
    fake_tx_count = 0;
    fake_tx_short_once = false;
    fake_time_hook = NULL;
    wait_tx_hook = NULL;
    wait_tx_hook_pending = false;
    owner_wait_hook = NULL;
    owner_wait_index = 0u;
    fake_peer_enabled = false;
    fake_peer_lease_state = PEER_UNOWNED;
    fake_peer_generation = 0u;
    fake_peer_nonce = 0u;
    fake_peer_queries = fake_peer_acquires = fake_peer_releases = fake_peer_zeros = 0u;
    fake_peer_drop_query_replies = fake_peer_drop_acquire_replies = 0u;
    fake_peer_drop_release_replies = 0u;
    fake_peer_seen_tx_count = 0u;
    fake_peer_reboot_wait = 0u;
    fake_peer_reboot_observed_wait = 0u;
    fake_peer_reboot_generation = 0u;
    saw_rebooted_peer_before_control_ready = false;
    captured_pre_ready_arm_token = 0u;
    captured_arm_token_while_not_ready = false;
    arm_gate = (gl30_motor_arm_gate_t){0};
    status = (gl30_motor_status){0};
    /* Legacy STOP/session tests start after the real owner lease handshake.
     * Dedicated discovery tests below opt back into CONTROL_DISCOVER. */
    control = CONTROL_RUNNING;
    lease_generation = UINT64_C(0x100000001);
    lease_counter = lease_generation;
    candidate_generation = 0u;
    control_sent_us = control_last_tx_us = control_zero_sent_us = 0u;
    control_request = (gl30_control_lease_request_t){0};
    maintenance_token = maintenance_sequence = 0u;
    maintenance_claimed = maintenance_finish = false;
    status.control_ready = true;
    status.lease_generation = lease_generation;
    gl30_esp_link_init(&link);
    gl30_menu_session_init(&session, 17u);
    session.command.leaseGeneration = lease_generation;
    desired_menu = false;
    desired_epoch = 0u;
    desired_published_us = (uint64_t)fake_now_us;
    tx_sequence = 0u;
    events = (QueueHandle_t)fake_events;
    task = (TaskHandle_t)0x22;
}

static void append_rx_frame(uint8_t type, const uint8_t *payload, size_t payload_size)
{
    size_t frame_size = 0u;
    CHECK(fake_rx_size <= sizeof(fake_rx));
    CHECK(gl30_frame_encode(type, 0u, fake_rx_sequence++, (uint64_t)fake_now_us,
        payload, payload_size, fake_rx + fake_rx_size, sizeof(fake_rx) - fake_rx_size,
        &frame_size) == 0);
    fake_rx_size += frame_size;
}

static void append_fast_frame(uint32_t motor_state)
{
    gl30_motor_state_fast_t fast = {0};
    uint8_t payload[GL30_MOTOR_STATE_FAST_LEN];
    fast.angleRad = 0.25f;
    fast.velocityRadPerSec = 0.0f;
    fast.motorState = motor_state;
    fast.encoderStatus = 1u;
    CHECK(gl30_encode_motor_state_fast(&fast, payload, sizeof(payload)) == 0);
    append_rx_frame(GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_FAST, payload, sizeof(payload));
    fake_rx_offset = 0u;
}

static void queue_fast_frame(uint32_t motor_state)
{
    if (fake_rx_offset == fake_rx_size) fake_rx_size = fake_rx_offset = 0u;
    CHECK(fake_rx_size == 0u);
    append_fast_frame(motor_state);
}

static void queue_haptic_ack(void)
{
    gl30_haptic_state_t haptic = {0};
    uint8_t payload[GL30_HAPTIC_STATE_LEN];
    haptic.profileId = session.command.profileId;
    haptic.commandNonce = session.command.commandNonce;
    haptic.modeFlags = GL30_HAPTIC_DETENT;
    haptic.logicalPosition = 12;
    haptic.subPosition = 0.0f;
    haptic.detentWidthRad = GL30_MENU_WIDTH_RAD;
    haptic.motorState = GL30_STARTUP_ACTIVE;
    haptic.status = GL30_HAPTIC_STATE_ENCODER_VALID | GL30_HAPTIC_STATE_DETENT_READY;
    haptic.leaseGeneration = lease_generation;
    CHECK(gl30_encode_haptic_state(&haptic, payload, sizeof(payload)) == 0);
    append_rx_frame(GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE, payload, sizeof(payload));
    fake_rx_offset = 0u;
}

static void queue_zero_haptic_ack(uint32_t nonce)
{
    gl30_haptic_state_t haptic = {0};
    uint8_t payload[GL30_HAPTIC_STATE_LEN];
    haptic.commandNonce = nonce;
    haptic.subPosition = 0.125f;
    haptic.motorState = GL30_STARTUP_READY;
    haptic.status = GL30_HAPTIC_STATE_ENCODER_VALID;
    haptic.leaseGeneration = lease_generation;
    CHECK(gl30_encode_haptic_state(&haptic, payload, sizeof(payload)) == 0);
    append_rx_frame(GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE, payload, sizeof(payload));
    fake_rx_offset = 0u;
}

static void queue_zero_haptic_reply(uint32_t nonce)
{
    if(fake_rx_offset==fake_rx_size) fake_rx_size=fake_rx_offset=0u;
    CHECK(fake_rx_size==0u);
    queue_zero_haptic_ack(nonce);
}

static void queue_zero_ready_reply(uint32_t nonce)
{
    queue_fast_frame(GL30_STARTUP_READY);
    queue_zero_haptic_ack(nonce);
}

static void queue_healthy_menu_reply(void)
{
    queue_fast_frame(GL30_STARTUP_ACTIVE);
    queue_haptic_ack();
}

static void reset_discovery_fixture(fake_peer_state initial_state,uint64_t generation)
{
    reset_fixture();
    control_query();
    lease_generation = 0u;
    lease_counter = 0u;
    candidate_generation = 0u;
    status.control_ready = false;
    status.lease_generation = 0u;
    session.command.leaseGeneration = 0u;
    fake_peer_enabled = true;
    fake_peer_lease_state = initial_state;
    fake_peer_generation = generation;
    fake_peer_nonce = initial_state == PEER_UNOWNED ? 0u : 0x77112233u;
}

static void fake_peer_queue_state(void)
{
    gl30_haptic_state_t haptic = {0};
    uint8_t payload[GL30_HAPTIC_STATE_LEN];
    CHECK(fake_rx_offset == fake_rx_size);
    fake_rx_size = fake_rx_offset = 0u;
    haptic.commandNonce = fake_peer_nonce;
    haptic.subPosition = 0.125f;
    haptic.motorState = GL30_STARTUP_READY;
    haptic.leaseGeneration = fake_peer_generation;
    switch (fake_peer_lease_state) {
    case PEER_RELEASED:
        haptic.status = GL30_HAPTIC_STATE_CONTROL_RELEASED;
        break;
    case PEER_WAITING_ZERO:
        haptic.status = GL30_HAPTIC_STATE_CONTROL_WAITING_ZERO;
        break;
    default:
        haptic.status = 0u;
        break;
    }
    CHECK(gl30_encode_haptic_state(&haptic, payload, sizeof(payload)) == 0);
    append_rx_frame(GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE, payload, sizeof(payload));
    fake_rx_offset = 0u;
}

static bool fake_peer_drop(unsigned *remaining)
{
    if (*remaining == 0u) return false;
    if (*remaining != UINT_MAX) --*remaining;
    return true;
}

static void fake_peer_respond_to_last_tx(void)
{
    gl30_frame_t frame = {0};
    gl30_parse_result_t parsed;
    size_t consumed = 0u;
    if (!fake_peer_enabled || fake_tx_count == 0u ||
        fake_tx_count == fake_peer_seen_tx_count) return;
    fake_peer_seen_tx_count = fake_tx_count;
    gl30_frame_parse_init();
    parsed = gl30_frame_parse(fake_tx, fake_tx_size, &frame, &consumed);
    CHECK(parsed.status == GL30_PARSE_OK && consumed == fake_tx_size);
    if (frame.type == GL30_V6_FRAME_PAYLOAD_CONTROL_LEASE) {
        gl30_control_lease_request_t request = {0};
        CHECK(gl30_decode_control_lease(frame.payload, frame.payload_len, &request) == 0);
        switch (request.action) {
        case GL30_CONTROL_QUERY:
            ++fake_peer_queries;
            if (fake_peer_drop(&fake_peer_drop_query_replies)) return;
            fake_peer_queue_state();
            return;
        case GL30_CONTROL_RELEASE:
            ++fake_peer_releases;
            if (request.currentGeneration == fake_peer_generation &&
                (fake_peer_lease_state == PEER_OWNED ||
                 fake_peer_lease_state == PEER_RELEASED)) {
                fake_peer_lease_state = PEER_RELEASED;
                fake_peer_nonce = request.zeroNonce;
            }
            if (fake_peer_drop(&fake_peer_drop_release_replies)) return;
            fake_peer_queue_state();
            return;
        case GL30_CONTROL_ACQUIRE:
            ++fake_peer_acquires;
            if ((request.currentGeneration == fake_peer_generation &&
                 (fake_peer_lease_state == PEER_UNOWNED ||
                  fake_peer_lease_state == PEER_RELEASED)) ||
                (request.nextGeneration == fake_peer_generation &&
                 request.zeroNonce == fake_peer_nonce &&
                 fake_peer_lease_state == PEER_WAITING_ZERO)) {
                fake_peer_generation = request.nextGeneration;
                fake_peer_nonce = request.zeroNonce;
                fake_peer_lease_state = PEER_WAITING_ZERO;
            }
            if (fake_peer_drop(&fake_peer_drop_acquire_replies)) return;
            fake_peer_queue_state();
            return;
        default:
            CHECK(false);
            return;
        }
    }
    if (frame.type == GL30_V6_FRAME_PAYLOAD_HAPTIC_COMMAND) {
        gl30_haptic_command_t command = {0};
        CHECK(gl30_decode_haptic_command(frame.payload, frame.payload_len, &command) == 0);
        ++fake_peer_zeros;
        if (command.leaseGeneration != fake_peer_generation) return;
        fake_peer_nonce = command.commandNonce;
        if (fake_peer_lease_state == PEER_WAITING_ZERO &&
            gl30_menu_session_command_is_zero(&command))
            fake_peer_lease_state = PEER_OWNED;
        fake_peer_queue_state();
        return;
    }
    CHECK(false);
}

static void fake_peer_wait_step(unsigned wait_index)
{
    if (!fake_peer_enabled) return;
    fake_now_us += 10000;
    if (fake_peer_reboot_wait == wait_index) {
        /* The peer has rebooted or a second master changed its generation.
         * The previously sent transaction is intentionally not acknowledged. */
        fake_peer_generation = fake_peer_reboot_generation;
        fake_peer_nonce = 0x55AA7711u;
        fake_peer_lease_state = PEER_OWNED;
        fake_peer_seen_tx_count = fake_tx_count;
        fake_peer_reboot_observed_wait = wait_index;
        fake_peer_queue_state();
        fake_peer_reboot_wait = 0u;
    } else {
        fake_peer_respond_to_last_tx();
    }
}

static void observe_reboot_intermediate_state(unsigned wait_index)
{
    if (fake_peer_reboot_observed_wait != 0u &&
        wait_index > fake_peer_reboot_observed_wait &&
        fake_peer_generation == fake_peer_reboot_generation &&
        !status.control_ready) {
        saw_rebooted_peer_before_control_ready = true;
    }
}

static void capture_arm_token_before_handshake_completes(unsigned wait_index)
{
    if (wait_index == 1u) {
        gl30_motor_status snapshot = {0};
        gl30_motor_snapshot(&snapshot);
        if (!snapshot.control_ready) {
            captured_pre_ready_arm_token = snapshot.stop_generation;
            captured_arm_token_while_not_ready = true;
        }
    }
}

static bool recover_control_after_transport_loss(void)
{
    fake_peer_enabled = true;
    fake_peer_lease_state = PEER_OWNED;
    fake_peer_generation = lease_generation;
    fake_peer_nonce = 0x55AA7711u;
    run_owner_polls(8u, NULL);
    fake_peer_enabled = false;

    gl30_motor_status snapshot = {0};
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.control_ready && !snapshot.maintenance_ready);
    CHECK(!snapshot.armed && !session.armed && !arm_gate.arm_request);
    return true;
}

static void inject_uart_event(uart_event_type_t type)
{
    CHECK(fake_event_count < sizeof(fake_events) / sizeof(fake_events[0]));
    fake_events[fake_event_count++].type = type;
}

static void inject_stop_after_arm_take(void)
{
    gl30_motor_stop();
}

static void decode_last_command(gl30_haptic_command_t *command)
{
    gl30_frame_t frame = {0};
    gl30_parse_result_t parsed;
    size_t consumed = 0u;
    CHECK(fake_tx_count > 0u && fake_tx_size > 0u);
    gl30_frame_parse_init();
    parsed = gl30_frame_parse(fake_tx, fake_tx_size, &frame, &consumed);
    CHECK(parsed.status == GL30_PARSE_OK);
    CHECK(consumed == fake_tx_size);
    CHECK(frame.type == GL30_V6_FRAME_PAYLOAD_HAPTIC_COMMAND);
    CHECK(gl30_decode_haptic_command(frame.payload, frame.payload_len, command) == 0);
}

static void expect_last_control_query(void)
{
    gl30_frame_t frame = {0};
    gl30_parse_result_t parsed;
    size_t consumed = 0u;
    gl30_control_lease_request_t request = {0};
    CHECK(fake_tx_count > 0u && fake_tx_size > 0u);
    gl30_frame_parse_init();
    parsed = gl30_frame_parse(fake_tx, fake_tx_size, &frame, &consumed);
    CHECK(parsed.status == GL30_PARSE_OK && consumed == fake_tx_size);
    CHECK(frame.type == GL30_V6_FRAME_PAYLOAD_CONTROL_LEASE);
    CHECK(gl30_decode_control_lease(frame.payload, frame.payload_len, &request) == 0);
    CHECK(request.action == GL30_CONTROL_QUERY);
}

static void expect_last_command_zero(void)
{
    gl30_haptic_command_t command = {0};
    decode_last_command(&command);
    CHECK(command.userTorqueLimitNm == 0.0f);
    CHECK(command.modeFlags == 0u);
}

static void expect_last_command_menu_torque(void)
{
    gl30_haptic_command_t command = {0};
    decode_last_command(&command);
    CHECK(command.userTorqueLimitNm == 0.030f);
    CHECK(command.modeFlags == GL30_HAPTIC_DETENT);
}

static bool stop_generation_fences_old_requests_and_wraps(void)
{
    reset_fixture();
    status.stop_generation = 9u;
    arm_gate.stop_generation = UINT64_MAX;
    gl30_motor_status snapshot = {0};
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.stop_generation == UINT64_MAX);

    CHECK(!gl30_motor_arm(0u));
    gl30_motor_stop();
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.stop_generation == 0u);
    CHECK(!gl30_motor_arm(UINT64_MAX));
    CHECK(gl30_motor_arm(0u));
    gl30_motor_stop();
    gl30_motor_stop();
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.stop_generation == 2u);
    CHECK(!gl30_motor_arm(0u));
    CHECK(gl30_motor_arm(2u));

    portENTER_CRITICAL(&lock);
    gl30_motor_arm_gate_request_t request = gl30_motor_arm_gate_take(&arm_gate);
    gl30_motor_arm_gate_request_t empty = gl30_motor_arm_gate_take(&arm_gate);
    portEXIT_CRITICAL(&lock);
    CHECK(request.stop_generation == 2u && request.arm_request && request.arm_changed);
    CHECK(empty.stop_generation == 2u && empty.arm_request && !empty.arm_changed);
    return true;
}

static bool stop_after_owner_take_sends_zero_and_keeps_stop_pending(void)
{
    reset_fixture();
    const uint64_t token = 0u;
    desired_menu = true;
    desired_epoch = 7u;
    desired_published_us = (uint64_t)fake_now_us;
    queue_fast_frame(GL30_STARTUP_ACTIVE);
    CHECK(gl30_motor_arm(token));

    wait_tx_hook = inject_stop_after_arm_take;
    wait_tx_hook_pending = true;
    run_owner_poll();

    gl30_motor_status snapshot = {0};
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.stop_generation == 1u);
    CHECK(!snapshot.armed);
    CHECK(!arm_gate.arm_request && arm_gate.arm_changed);
    CHECK(!session.armed);
    CHECK(session.stop_pending == false); /* accepted zero command discharged the pending stop */
    expect_last_command_zero();

    run_owner_poll();
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.stop_generation == 1u && !snapshot.armed);
    CHECK(!arm_gate.arm_changed);
    expect_last_command_zero();
    return true;
}

static bool health_loss_disarms_and_requires_a_fresh_owner_check(void)
{
    reset_fixture();
    const uint64_t old_token = 0u;
    queue_fast_frame(GL30_STARTUP_ACTIVE);
    CHECK(gl30_motor_arm(old_token));
    run_owner_poll();

    gl30_motor_status snapshot = {0};
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.armed && snapshot.stop_generation == old_token);

    fake_now_us += GL30_MOTOR_FAST_FRESH_US;
    run_owner_poll();
    gl30_motor_snapshot(&snapshot);
    CHECK(!snapshot.armed);
    CHECK(snapshot.stop_generation == old_token + 1u);
    CHECK(snapshot.feedback.state == GL30_MOTOR_OFFLINE);
    CHECK(!gl30_motor_arm(old_token));

    const uint64_t fresh_token = snapshot.stop_generation;
    CHECK(gl30_motor_arm(fresh_token));
    run_owner_poll();
    gl30_motor_snapshot(&snapshot);
    CHECK(!snapshot.armed);
    CHECK(snapshot.stop_generation == fresh_token + 1u);
    return true;
}

static bool uart_error_disarms_and_fences_the_captured_token(void)
{
    reset_fixture();
    const uint64_t token = 0u;
    queue_fast_frame(GL30_STARTUP_READY);
    CHECK(gl30_motor_arm(token));
    run_owner_poll();
    CHECK(session.armed);

    inject_uart_event(UART_FRAME_ERR);
    run_owner_poll();

    gl30_motor_status snapshot = {0};
    gl30_motor_snapshot(&snapshot);
    CHECK(!snapshot.armed);
    CHECK(snapshot.rx_errors == 1u);
    CHECK(snapshot.stop_generation == token + 1u);
    CHECK(!gl30_motor_arm(token));
    CHECK(recover_control_after_transport_loss());
    gl30_motor_snapshot(&snapshot);
    const uint64_t fresh_token = snapshot.stop_generation;
    CHECK(!snapshot.armed && snapshot.stop_generation == token + 2u);
    CHECK(!gl30_motor_arm(token));
    fake_now_us += 1000;
    queue_fast_frame(GL30_STARTUP_READY);
    CHECK(gl30_motor_arm(fresh_token));
    run_owner_poll();
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.armed && snapshot.stop_generation == fresh_token);
    return true;
}

static bool fifo_overflow_same_poll_disarms_and_recovers_with_fresh_token(void)
{
    reset_fixture();
    const uint64_t old_token = arm_gate.stop_generation;
    CHECK(gl30_motor_arm(old_token));
    CHECK(arm_gate.arm_request);
    inject_uart_event(UART_FIFO_OVF);
    run_owner_poll();

    gl30_motor_status snapshot = {0};
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.rx_errors == 1u && !snapshot.control_ready);
    CHECK(!snapshot.armed && !session.armed && !arm_gate.arm_request);
    CHECK(snapshot.stop_generation == old_token + 1u);
    CHECK(!gl30_motor_arm(old_token));
    expect_last_control_query(); /* No positive command escaped this poll. */

    CHECK(recover_control_after_transport_loss());
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.control_ready && !snapshot.armed && !arm_gate.arm_request);
    const uint64_t fresh_token = snapshot.stop_generation;
    CHECK(fresh_token == old_token + 2u);
    CHECK(!gl30_motor_arm(old_token));
    CHECK(gl30_motor_arm(fresh_token));
    CHECK(arm_gate.arm_request && !snapshot.armed);

    gl30_motor_stop();
    const uint64_t maintenance = gl30_motor_maintenance_begin();
    CHECK(maintenance != 0u); /* Transport recovery did not strand maintenance. */
    return true;
}

static bool startup_and_fault_feedback_revoke_arm_until_explicit_request(void)
{
    static const uint32_t unavailable_states[] = {
        GL30_STARTUP_POWER_CHECK, GL30_STARTUP_FAULT_LATCHED
    };
    static const gl30_motor_feedback_state expected_feedback[] = {
        GL30_MOTOR_CHECKING, GL30_MOTOR_FAULT
    };
    for (size_t index = 0u;
         index < sizeof(unavailable_states) / sizeof(unavailable_states[0]);
         ++index) {
        prepare_confirmed_menu_session();
        const uint64_t captured_token = arm_gate.stop_generation;
        fake_now_us += 1000;
        desired_published_us = (uint64_t)fake_now_us;
        queue_fast_frame(unavailable_states[index]);
        run_owner_poll();

        gl30_motor_status snapshot = {0};
        gl30_motor_snapshot(&snapshot);
        CHECK(snapshot.feedback.state == expected_feedback[index]);
        CHECK(!snapshot.armed && !session.armed && !arm_gate.arm_request);
        CHECK(snapshot.stop_generation == captured_token + 1u);
        expect_last_command_zero();

        fake_now_us += 1000;
        desired_published_us = (uint64_t)fake_now_us;
        queue_fast_frame(GL30_STARTUP_ACTIVE);
        run_owner_poll();
        gl30_motor_snapshot(&snapshot);
        CHECK(snapshot.feedback.state == GL30_MOTOR_ACTIVE);
        CHECK(!snapshot.armed && !session.armed && !arm_gate.arm_request);
        CHECK(!gl30_motor_arm(captured_token));
        expect_last_command_zero();

        const uint64_t fresh_token = snapshot.stop_generation;
        CHECK(fresh_token == captured_token + 1u);
        CHECK(gl30_motor_arm(fresh_token));
        CHECK(arm_gate.arm_request && !snapshot.armed);
    }
    return true;
}

static void prepare_confirmed_menu_session(void)
{
    reset_fixture();
    desired_menu = true;
    desired_epoch = 7u;
    desired_published_us = (uint64_t)fake_now_us;
    queue_fast_frame(GL30_STARTUP_ACTIVE);
    CHECK(gl30_motor_arm(0u));
    run_owner_poll();
    CHECK(session.armed && session.configured);

    fake_now_us += 1000;
    desired_published_us = (uint64_t)fake_now_us;
    queue_healthy_menu_reply();
    run_owner_poll();
    gl30_motor_status snapshot = {0};
    gl30_motor_snapshot(&snapshot);
    CHECK(session.armed && session.acknowledged);
    CHECK(snapshot.sample.valid);
}

static bool short_uart_write_disarms_and_requires_explicit_rearm(void)
{
    prepare_confirmed_menu_session();
    const uint64_t old_token = arm_gate.stop_generation;
    fake_now_us += 1000;
    desired_published_us = (uint64_t)fake_now_us;
    queue_healthy_menu_reply();
    fake_tx_short_once = true;
    run_owner_poll();

    gl30_motor_status snapshot = {0};
    gl30_motor_snapshot(&snapshot);
    CHECK(!snapshot.armed && !snapshot.sample.valid);
    CHECK(!session.armed && session.stop_pending);
    CHECK(snapshot.tx_errors == 1u);
    CHECK(snapshot.stop_generation == old_token + 1u);
    CHECK(!gl30_motor_arm(old_token));

    fake_now_us += 1000;
    desired_published_us = (uint64_t)fake_now_us;
    queue_fast_frame(GL30_STARTUP_ACTIVE);
    run_owner_poll();
    gl30_motor_snapshot(&snapshot);
    CHECK(!snapshot.armed && snapshot.stop_generation == old_token + 1u);

    const uint64_t fresh_token = snapshot.stop_generation;
    CHECK(gl30_motor_arm(fresh_token));
    fake_now_us += 1000;
    desired_published_us = (uint64_t)fake_now_us;
    queue_fast_frame(GL30_STARTUP_ACTIVE);
    run_owner_poll();
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.armed && snapshot.stop_generation == fresh_token);
    return true;
}

static bool rx_backlog_disarms_and_requires_explicit_rearm(void)
{
    prepare_confirmed_menu_session();
    const uint64_t old_token = arm_gate.stop_generation;
    fake_now_us += 1000;
    desired_published_us = (uint64_t)fake_now_us;
    fake_backlog_once = true;
    run_owner_poll();

    gl30_motor_status snapshot = {0};
    gl30_motor_snapshot(&snapshot);
    CHECK(!snapshot.armed && !snapshot.sample.valid);
    CHECK(!session.armed);
    CHECK(snapshot.rx_errors == 1u);
    CHECK(snapshot.stop_generation == old_token + 1u);
    CHECK(!gl30_motor_arm(old_token));

    CHECK(recover_control_after_transport_loss());
    gl30_motor_snapshot(&snapshot);
    const uint64_t fresh_token = snapshot.stop_generation;
    CHECK(!snapshot.armed && snapshot.stop_generation == old_token + 2u);
    CHECK(!gl30_motor_arm(old_token));
    fake_now_us += 1000;
    desired_published_us = (uint64_t)fake_now_us;
    queue_fast_frame(GL30_STARTUP_ACTIVE);
    CHECK(gl30_motor_arm(fresh_token));
    run_owner_poll();
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.armed && snapshot.stop_generation == fresh_token);
    return true;
}

static bool uart_break_events_revoke_arm_tokens(void)
{
    const uart_event_type_t break_events[] = {UART_BREAK, UART_DATA_BREAK};
    for (size_t index = 0u; index < sizeof(break_events) / sizeof(break_events[0]); ++index) {
        reset_fixture();
        const uint64_t token = 0u;
        queue_fast_frame(GL30_STARTUP_ACTIVE);
        CHECK(gl30_motor_arm(token));
        run_owner_poll();
        CHECK(session.armed);

        inject_uart_event(break_events[index]);
        run_owner_poll();
        gl30_motor_status snapshot = {0};
        gl30_motor_snapshot(&snapshot);
        CHECK(!snapshot.armed);
        CHECK(snapshot.rx_errors == 1u);
        CHECK(snapshot.stop_generation == token + 1u);
        CHECK(!gl30_motor_arm(token));
    }
    return true;
}

static bool stop_at_tx_commit_allows_one_frame_then_sends_zero(void)
{
    prepare_confirmed_menu_session();
    const uint64_t token = arm_gate.stop_generation;
    fake_now_us += 1000;
    desired_published_us = (uint64_t)fake_now_us;
    queue_healthy_menu_reply();
    fake_stop_on_tx_once = true;
    run_owner_poll();

    gl30_motor_status snapshot = {0};
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.stop_generation == token + 1u);
    CHECK(!snapshot.armed);
    CHECK(!gl30_motor_arm(token));
    expect_last_command_menu_torque();

    fake_now_us += 1000;
    desired_published_us = (uint64_t)fake_now_us;
    queue_healthy_menu_reply();
    run_owner_poll();
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.stop_generation == token + 1u && !snapshot.armed);
    expect_last_command_zero();
    return true;
}

static uint32_t zero_owner_test_nonce;
static uint64_t zero_owner_test_first_tx_us;
static uint32_t zero_stop_fence_old_nonce,zero_stop_fence_new_nonce;
static uint64_t zero_stop_fence_old_generation,zero_stop_fence_old_tx_us;
static void zero_owner_poll_hook(unsigned wait_index)
{
    gl30_motor_status snapshot = {0};
    if (wait_index == 2u) {
        /* The first full zero TX has been published before the next poll. */
        gl30_motor_snapshot(&snapshot);
        CHECK(!snapshot.zero_confirmed && snapshot.zero_command_nonce != 0u);
        zero_owner_test_nonce = snapshot.zero_command_nonce;
        zero_owner_test_first_tx_us = snapshot.zero_sent_us;
        CHECK(zero_owner_test_first_tx_us != 0u && snapshot.zero_feedback_us == 0u);
        fake_now_us += 1000;
        desired_published_us = (uint64_t)fake_now_us;
        queue_fast_frame(GL30_STARTUP_READY);
    } else if (wait_index == 3u) {
        /* A response may arrive 100 ms after TX; its second stream is still pending. */
        gl30_motor_snapshot(&snapshot);
        CHECK(!snapshot.zero_confirmed && snapshot.zero_command_nonce == zero_owner_test_nonce);
        CHECK(snapshot.zero_sent_us == zero_owner_test_first_tx_us &&
              snapshot.zero_feedback_us == 0u);
        fake_now_us += 100000;
        desired_published_us = (uint64_t)fake_now_us;
        queue_zero_haptic_reply(zero_owner_test_nonce);
    } else if(wait_index==4u) {
        /* Same nonce heartbeat retains first TX time while FAST is refreshed. */
        gl30_motor_snapshot(&snapshot);
        CHECK(!snapshot.zero_confirmed && snapshot.zero_command_nonce==zero_owner_test_nonce);
        CHECK(snapshot.zero_sent_us==zero_owner_test_first_tx_us &&
              snapshot.zero_feedback_us==0u);
        ++fake_now_us;
        desired_published_us=(uint64_t)fake_now_us;
        queue_fast_frame(GL30_STARTUP_READY);
    }
}

static void zero_owner_stop_fence_poll_hook(unsigned wait_index)
{
    if(wait_index<=4u) {
        zero_owner_poll_hook(wait_index);
    } else if(wait_index==5u) {
        CHECK(status.zero_confirmed);
        zero_stop_fence_old_nonce=status.zero_command_nonce;
        zero_stop_fence_old_tx_us=status.zero_sent_us;
        zero_stop_fence_old_generation=arm_gate.stop_generation;
        CHECK(zero_stop_fence_old_nonce!=0u && zero_stop_fence_old_tx_us!=0u);
        gl30_motor_stop();
        CHECK(arm_gate.stop_generation==zero_stop_fence_old_generation+1u);
        fake_now_us+=1000;
        desired_published_us=(uint64_t)fake_now_us;
        queue_zero_ready_reply(zero_stop_fence_old_nonce);
    } else if(wait_index==6u) {
        /* A fresh old-nonce echo after a new STOP must not re-confirm zero. */
        CHECK(!status.zero_confirmed);
        CHECK(status.stop_generation==zero_stop_fence_old_generation+1u);
        zero_stop_fence_new_nonce=status.zero_command_nonce;
        CHECK(zero_stop_fence_new_nonce!=0u &&
              zero_stop_fence_new_nonce!=zero_stop_fence_old_nonce);
        CHECK(status.zero_sent_us>zero_stop_fence_old_tx_us);
        fake_now_us+=1000;
        desired_published_us=(uint64_t)fake_now_us;
        queue_zero_ready_reply(zero_stop_fence_new_nonce);
    } else if(wait_index==7u) {
        CHECK(status.zero_confirmed);
        CHECK(status.zero_command_nonce==zero_stop_fence_new_nonce);
        CHECK(status.zero_sent_us>zero_stop_fence_old_tx_us);
    }
}

static void publish_zero_confirmation_with_owner(void)
{
    prepare_confirmed_menu_session();
    gl30_motor_stop();

    fake_now_us += 1000;
    desired_published_us = (uint64_t)fake_now_us;
    queue_fast_frame(GL30_STARTUP_READY);
    zero_owner_test_nonce = 0u;
    zero_owner_test_first_tx_us = 0u;
    run_owner_polls(4u,zero_owner_poll_hook);

    gl30_motor_status snapshot = {0};
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.zero_confirmed && snapshot.zero_command_nonce != 0u);
}

static uint64_t snapshot_injected_arm_token;
static void inject_arm_at_snapshot_clock_read(void)
{
    CHECK(gl30_motor_arm(snapshot_injected_arm_token));
}
static void inject_stop_at_snapshot_clock_read(void)
{
    gl30_motor_stop();
}

static bool zero_echo_confirms_only_after_tx_and_snapshot_revokes_on_new_intent(void)
{
    publish_zero_confirmation_with_owner();

    gl30_motor_status snapshot = {0};
    gl30_motor_snapshot(&snapshot);
    CHECK(!snapshot.armed && !session.armed);
    CHECK(snapshot.zero_confirmed && snapshot.zero_command_nonce != 0u);
    CHECK(snapshot.zero_sent_us == zero_owner_test_first_tx_us);
    CHECK(snapshot.zero_command_nonce == zero_owner_test_nonce);
    const uint32_t zero_nonce = snapshot.zero_command_nonce;
    gl30_haptic_command_t transmitted = {0};
    decode_last_command(&transmitted);
    CHECK(gl30_menu_session_command_is_zero(&transmitted));
    CHECK(transmitted.commandNonce == zero_nonce);
    CHECK(snapshot.zero_feedback_us+1u == (uint64_t)fake_now_us);
    CHECK(snapshot.feedback.state == GL30_MOTOR_READY);

    const uint64_t haptic_received_us=snapshot.zero_feedback_us;
    CHECK(snapshot.feedback.received_us==haptic_received_us+1u);
    fake_now_us=(int64_t)(haptic_received_us+GL30_MOTOR_FAST_FRESH_US-1u);
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.zero_confirmed && snapshot.feedback.state==GL30_MOTOR_READY);
    CHECK(snapshot.feedback.received_us==haptic_received_us+1u);
    fake_now_us=(int64_t)(haptic_received_us+GL30_MOTOR_FAST_FRESH_US);
    gl30_motor_snapshot(&snapshot);
    CHECK(!snapshot.zero_confirmed && snapshot.feedback.state==GL30_MOTOR_READY);
    CHECK(fake_now_us-(int64_t)snapshot.feedback.received_us==
          (int64_t)GL30_MOTOR_FAST_FRESH_US-1);
    fake_now_us=(int64_t)(haptic_received_us-1u);
    gl30_motor_snapshot(&snapshot);
    CHECK(!snapshot.zero_confirmed && snapshot.feedback.state==GL30_MOTOR_OFFLINE);
    fake_now_us=(int64_t)(haptic_received_us+1u);
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.zero_confirmed);

    const gl30_menu_session session_before_snapshot = session;
    const gl30_esp_link_t link_before_snapshot = link;
    const gl30_motor_status status_before_snapshot = status;
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.zero_confirmed);
    CHECK(memcmp(&session, &session_before_snapshot, sizeof(session)) == 0);
    CHECK(memcmp(&link, &link_before_snapshot, sizeof(link)) == 0);
    CHECK(memcmp(&status, &status_before_snapshot, sizeof(status)) == 0);

    CHECK(gl30_motor_arm(snapshot.stop_generation));
    const gl30_menu_session session_before_pending_snapshot = session;
    const gl30_esp_link_t link_before_pending_snapshot = link;
    const gl30_motor_status status_before_pending_snapshot = status;
    gl30_motor_snapshot(&snapshot);
    CHECK(!snapshot.zero_confirmed && snapshot.zero_command_nonce == zero_nonce);
    CHECK(arm_gate.arm_request && status.zero_confirmed);
    CHECK(memcmp(&session, &session_before_pending_snapshot, sizeof(session)) == 0);
    CHECK(memcmp(&link, &link_before_pending_snapshot, sizeof(link)) == 0);
    CHECK(memcmp(&status, &status_before_pending_snapshot, sizeof(status)) == 0);

    gl30_motor_stop();
    const uint64_t stopped_generation = arm_gate.stop_generation;
    const gl30_menu_session session_before_stop_snapshot = session;
    const gl30_esp_link_t link_before_stop_snapshot = link;
    const gl30_motor_status status_before_stop_snapshot = status;
    gl30_motor_snapshot(&snapshot);
    CHECK(!snapshot.zero_confirmed && snapshot.stop_generation == stopped_generation);
    CHECK(status.zero_confirmed && arm_gate.stop_generation != status.stop_generation);
    CHECK(memcmp(&session, &session_before_stop_snapshot, sizeof(session)) == 0);
    CHECK(memcmp(&link, &link_before_stop_snapshot, sizeof(link)) == 0);
    CHECK(memcmp(&status, &status_before_stop_snapshot, sizeof(status)) == 0);
    return true;
}

static bool zero_snapshot_second_lock_rechecks_arm_and_stop_races(void)
{
    gl30_motor_status snapshot={0};
    gl30_motor_status published_before;
    gl30_menu_session session_before;
    gl30_esp_link_t link_before;

    publish_zero_confirmation_with_owner();
    published_before=status;
    session_before=session;
    link_before=link;
    snapshot_injected_arm_token=arm_gate.stop_generation;
    fake_time_hook=inject_arm_at_snapshot_clock_read;
    gl30_motor_snapshot(&snapshot);
    CHECK(!snapshot.zero_confirmed && arm_gate.arm_request);
    CHECK(memcmp(&status,&published_before,sizeof(status))==0);
    CHECK(memcmp(&session,&session_before,sizeof(session))==0);
    CHECK(memcmp(&link,&link_before,sizeof(link))==0);

    publish_zero_confirmation_with_owner();
    const uint64_t previous_generation=arm_gate.stop_generation;
    published_before=status;
    session_before=session;
    link_before=link;
    fake_time_hook=inject_stop_at_snapshot_clock_read;
    gl30_motor_snapshot(&snapshot);
    CHECK(!snapshot.zero_confirmed && snapshot.stop_generation==previous_generation+1u);
    CHECK(status.stop_generation==previous_generation);
    CHECK(memcmp(&status,&published_before,sizeof(status))==0);
    CHECK(memcmp(&session,&session_before,sizeof(session))==0);
    CHECK(memcmp(&link,&link_before,sizeof(link))==0);
    return true;
}

static bool zero_confirmation_nonce_rotates_for_each_observed_stop(void)
{
    prepare_confirmed_menu_session();
    gl30_motor_stop();
    fake_now_us+=1000;
    desired_published_us=(uint64_t)fake_now_us;
    queue_fast_frame(GL30_STARTUP_READY);
    zero_owner_test_nonce=0u;
    zero_owner_test_first_tx_us=0u;
    run_owner_polls(7u,zero_owner_stop_fence_poll_hook);

    gl30_motor_status snapshot={0};
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.zero_confirmed);
    CHECK(snapshot.zero_command_nonce==zero_stop_fence_new_nonce);
    CHECK(snapshot.zero_command_nonce!=zero_stop_fence_old_nonce);
    CHECK(snapshot.stop_generation==zero_stop_fence_old_generation+1u);
    return true;
}

static bool zero_nonce_change_and_short_write_never_reuse_confirmation(void)
{
    reset_fixture();
    queue_fast_frame(GL30_STARTUP_READY);
    run_owner_poll();
    gl30_motor_status snapshot = {0};
    gl30_motor_snapshot(&snapshot);
    CHECK(!snapshot.zero_confirmed && snapshot.zero_command_nonce != 0u);
    const uint32_t old_nonce = snapshot.zero_command_nonce;
    const uint64_t old_tx_us = snapshot.zero_sent_us;

    fake_now_us += 1000;
    gl30_motor_desire_menu(false, 1u);
    desired_published_us = (uint64_t)fake_now_us;
    queue_zero_ready_reply(old_nonce);
    run_owner_poll();
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.zero_command_nonce != old_nonce && snapshot.zero_sent_us > old_tx_us);
    CHECK(!snapshot.zero_confirmed && snapshot.zero_feedback_us == 0u);

    reset_fixture();
    queue_fast_frame(GL30_STARTUP_READY);
    fake_tx_short_once = true;
    run_owner_poll();
    gl30_motor_snapshot(&snapshot);
    CHECK(snapshot.tx_errors == 1u && !snapshot.zero_confirmed);
    CHECK(snapshot.zero_command_nonce == 0u && snapshot.zero_sent_us == 0u &&
          snapshot.zero_feedback_us == 0u);
    return true;
}

static bool peer_generation_change_during_old_zero_requests_rediscovery(void)
{
    const uint64_t old_generation = UINT64_C(0x100000010);
    reset_discovery_fixture(PEER_OWNED, old_generation);
    const uint64_t old_arm_token = arm_gate.stop_generation;
    fake_peer_reboot_wait = 3u;
    fake_peer_reboot_generation = old_generation + 1u;
    run_owner_polls(12u, observe_reboot_intermediate_state);

    CHECK(fake_peer_queries >= 2u);
    CHECK(saw_rebooted_peer_before_control_ready);
    CHECK(status.control_ready && !status.maintenance_ready);
    CHECK(lease_generation == status.lease_generation);
    CHECK(lease_generation != old_generation);
    CHECK(!status.armed && !session.armed && !arm_gate.arm_request);
    CHECK(!gl30_motor_arm(old_arm_token));
    const uint64_t current_arm_token = arm_gate.stop_generation;
    CHECK(current_arm_token != old_arm_token);
    CHECK(gl30_motor_arm(current_arm_token));
    CHECK(arm_gate.arm_request && !status.armed);
    return true;
}

static bool acquire_without_ack_retries_discovery_within_bound(void)
{
    reset_discovery_fixture(PEER_UNOWNED, 0u);
    fake_peer_drop_acquire_replies = UINT_MAX;
    run_owner_polls(62u, NULL); /* 610 ms of 1 kHz owner notifications. */

    CHECK(fake_peer_acquires >= 2u);
    CHECK(fake_peer_queries >= 2u);
    CHECK(!status.control_ready && !status.maintenance_ready);
    return true;
}

static bool lost_release_ack_never_claims_maintenance_and_recovers_query(void)
{
    reset_fixture();
    fake_peer_enabled = true;
    fake_peer_lease_state = PEER_OWNED;
    fake_peer_generation = lease_generation;
    fake_peer_nonce = session.command.commandNonce;
    fake_peer_drop_release_replies = UINT_MAX;

    const uint64_t token = gl30_motor_maintenance_begin();
    CHECK(token != 0u);
    run_owner_polls(62u, NULL); /* More than the bounded release-ack interval. */

    CHECK(fake_peer_releases >= 2u);
    CHECK(fake_peer_queries >= 1u);
    CHECK(!status.maintenance_ready);
    CHECK(!gl30_motor_maintenance_claim(token));
    return true;
}

static void inject_uart_error_on_owner_poll(unsigned wait_index)
{
    if (wait_index == 3u) inject_uart_event(UART_FRAME_ERR);
}

static bool rx_loss_during_acquire_restarts_peer_discovery(void)
{
    reset_discovery_fixture(PEER_UNOWNED, 0u);
    fake_peer_drop_acquire_replies = UINT_MAX;
    run_owner_polls(12u, inject_uart_error_on_owner_poll);

    CHECK(fake_peer_acquires >= 2u);
    CHECK(fake_peer_queries >= 2u);
    CHECK(status.rx_errors == 1u);
    CHECK(!status.control_ready);
    return true;
}

static bool maintenance_claim_stays_silent_across_rx_error(void)
{
    reset_discovery_fixture(PEER_UNOWNED, 0u);
    run_owner_polls(4u, NULL);
    CHECK(status.control_ready);

    fake_peer_lease_state = PEER_OWNED;
    fake_peer_generation = lease_generation;
    fake_peer_nonce = session.command.commandNonce;
    const uint64_t token = gl30_motor_maintenance_begin();
    CHECK(token != 0u);
    run_owner_polls(3u, NULL);
    CHECK(status.maintenance_ready && status.maintenance_token == token);
    CHECK(gl30_motor_maintenance_claim(token));
    CHECK(gl30_motor_maintenance_held(token));

    const unsigned before_error = fake_tx_count;
    inject_uart_event(UART_FRAME_ERR);
    run_owner_polls(3u, NULL);
    CHECK(status.maintenance_ready && gl30_motor_maintenance_held(token));
    CHECK(fake_tx_count == before_error);
    return true;
}

static bool handshake_completion_fences_tokens_captured_before_ready(void)
{
    reset_discovery_fixture(PEER_UNOWNED, 0u);
    run_owner_polls(8u, capture_arm_token_before_handshake_completes);

    CHECK(captured_arm_token_while_not_ready);
    CHECK(status.control_ready && !status.maintenance_ready &&
          !status.armed && !session.armed);
    gl30_motor_status snapshot = {0};
    gl30_motor_snapshot(&snapshot);
    CHECK(captured_pre_ready_arm_token != snapshot.stop_generation);
    CHECK(!gl30_motor_arm(captured_pre_ready_arm_token));
    CHECK(!arm_gate.arm_request);

    gl30_motor_snapshot(&snapshot);
    CHECK(gl30_motor_arm(snapshot.stop_generation));
    CHECK(arm_gate.arm_request && !status.armed);
    return true;
}

int64_t esp_timer_get_time(void)
{
    void (*hook)(void)=fake_time_hook;
    fake_time_hook=NULL;
    if(hook) hook();
    return fake_now_us;
}
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out)
{ (void)args; *out = (esp_timer_handle_t)0x11; return ESP_OK; }
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer_handle, uint64_t period_us)
{ (void)timer_handle; (void)period_us; return ESP_OK; }
esp_err_t esp_timer_start_once(esp_timer_handle_t timer_handle, uint64_t timeout_us)
{ (void)timer_handle; (void)timeout_us; return ESP_OK; }
esp_err_t esp_timer_stop(esp_timer_handle_t timer_handle) { (void)timer_handle; return ESP_OK; }
esp_err_t esp_timer_delete(esp_timer_handle_t timer_handle) { (void)timer_handle; return ESP_OK; }
uint32_t esp_random(void) { return 17u; }

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait_ticks)
{ (void)queue; (void)item; (void)wait_ticks; return pdFALSE; }
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait_ticks)
{
    (void)queue; (void)wait_ticks;
    if (fake_event_count == 0u) return pdFALSE;
    *(uart_event_t *)item = fake_events[0];
    --fake_event_count;
    memmove(fake_events, fake_events + 1, fake_event_count * sizeof(fake_events[0]));
    return pdTRUE;
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) { (void)queue; return 0u; }
BaseType_t xQueueReset(QueueHandle_t queue) { (void)queue; fake_event_count = 0u; return pdTRUE; }
QueueHandle_t xQueueCreate(UBaseType_t length, size_t item_size)
{ (void)length; (void)item_size; return (QueueHandle_t)fake_events; }
void vQueueDelete(QueueHandle_t queue) { (void)queue; }

TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)0x22; }
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char *name,
    uint32_t stack_size, void *argument, UBaseType_t priority,
    TaskHandle_t *out, BaseType_t core_id)
{ (void)entry; (void)name; (void)stack_size; (void)argument; (void)priority; (void)core_id; *out = task; return pdPASS; }
void vTaskDelete(TaskHandle_t handle) { (void)handle; }
void xTaskNotifyGive(TaskHandle_t handle) { (void)handle; ++notify_tokens; }
uint32_t ulTaskNotifyTake(BaseType_t clear_count, TickType_t ticks)
{
    (void)clear_count; (void)ticks;
    if (notify_tokens > 0u) {
        --notify_tokens;
        ++owner_wait_index;
        fake_peer_wait_step(owner_wait_index);
        if (owner_wait_hook) owner_wait_hook(owner_wait_index);
        return 1u;
    }
    if (owner_return_ready) longjmp(owner_return, 1);
    return 0u;
}
TickType_t xTaskGetTickCount(void) { return 0u; }
void vTaskDelay(TickType_t ticks) { (void)ticks; }
void xTaskDelayUntil(TickType_t *wake_tick, TickType_t increment)
{ *wake_tick += increment; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t handle) { (void)handle; return 0u; }

esp_err_t uart_driver_install(uart_port_t uart, int rx_buffer_size, int tx_buffer_size,
    int event_queue_size, QueueHandle_t *event_queue_out, int interrupt_flags)
{ (void)uart; (void)rx_buffer_size; (void)tx_buffer_size; (void)event_queue_size; (void)interrupt_flags; *event_queue_out = (QueueHandle_t)fake_events; return ESP_OK; }
esp_err_t uart_param_config(uart_port_t uart, const uart_config_t *config)
{ (void)uart; (void)config; return ESP_OK; }
esp_err_t uart_set_pin(uart_port_t uart, int tx, int rx, int rts, int cts)
{ (void)uart; (void)tx; (void)rx; (void)rts; (void)cts; return ESP_OK; }
esp_err_t uart_driver_delete(uart_port_t uart) { (void)uart; return ESP_OK; }
esp_err_t uart_get_buffered_data_len(uart_port_t uart, size_t *length)
{
    (void)uart;
    if (fake_backlog_once) {
        fake_backlog_once = false;
        *length = 2049u;
    } else *length = fake_rx_size - fake_rx_offset;
    return ESP_OK;
}
void uart_flush_input(uart_port_t uart) { (void)uart; fake_rx_size = fake_rx_offset = 0u; }
int uart_read_bytes(uart_port_t uart, void *buffer, uint32_t length, TickType_t wait_ticks)
{
    (void)uart; (void)wait_ticks;
    size_t available = fake_rx_size - fake_rx_offset;
    size_t count = available < length ? available : length;
    if (count == 0u) return 0;
    memcpy(buffer, fake_rx + fake_rx_offset, count);
    fake_rx_offset += count;
    return (int)count;
}
esp_err_t uart_wait_tx_done(uart_port_t uart, TickType_t wait_ticks)
{
    (void)uart; (void)wait_ticks;
    if (wait_tx_hook_pending && wait_tx_hook) {
        wait_tx_hook_pending = false;
        wait_tx_hook();
    }
    return ESP_OK;
}
int uart_tx_chars(uart_port_t uart, const char *buffer, uint32_t length)
{
    (void)uart;
    CHECK(length <= sizeof(fake_tx));
    if (fake_stop_on_tx_once) {
        fake_stop_on_tx_once = false;
        gl30_motor_stop();
    }
    memcpy(fake_tx, buffer, length);
    fake_tx_size = length;
    ++fake_tx_count;
    if (fake_tx_short_once) {
        fake_tx_short_once = false;
        return (int)length - 1;
    }
    return (int)length;
}

int main(void)
{
    if (!stop_generation_fences_old_requests_and_wraps()) return 1;
    if (!stop_after_owner_take_sends_zero_and_keeps_stop_pending()) return 1;
    if (!health_loss_disarms_and_requires_a_fresh_owner_check()) return 1;
    if (!uart_error_disarms_and_fences_the_captured_token()) return 1;
    if (!fifo_overflow_same_poll_disarms_and_recovers_with_fresh_token()) return 1;
    if (!startup_and_fault_feedback_revoke_arm_until_explicit_request()) return 1;
    if (!short_uart_write_disarms_and_requires_explicit_rearm()) return 1;
    if (!rx_backlog_disarms_and_requires_explicit_rearm()) return 1;
    if (!uart_break_events_revoke_arm_tokens()) return 1;
    if (!stop_at_tx_commit_allows_one_frame_then_sends_zero()) return 1;
    if (!zero_echo_confirms_only_after_tx_and_snapshot_revokes_on_new_intent()) return 1;
    if (!zero_confirmation_nonce_rotates_for_each_observed_stop()) return 1;
    if (!zero_snapshot_second_lock_rechecks_arm_and_stop_races()) return 1;
    if (!zero_nonce_change_and_short_write_never_reuse_confirmation()) return 1;
    if (!peer_generation_change_during_old_zero_requests_rediscovery()) return 1;
    if (!acquire_without_ack_retries_discovery_within_bound()) return 1;
    if (!lost_release_ack_never_claims_maintenance_and_recovers_query()) return 1;
    if (!rx_loss_during_acquire_restarts_peer_discovery()) return 1;
    if (!maintenance_claim_stays_silent_across_rx_error()) return 1;
    if (!handshake_completion_fences_tokens_captured_before_ready()) return 1;
    printf("motor stop production-owner: %u checks passed\n", checks);
    return 0;
}
