#ifndef GL30_V6_PROTOCOL_H_
#define GL30_V6_PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>

#define GL30_FRAME_SYNC 0xA55Au
#define GL30_FRAME_VERSION 1u
#define GL30_FRAME_HEADER_BYTES 24u
#define GL30_FRAME_MAX_PAYLOAD_BYTES 4096u

#define GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_FAST 0x01u
#define GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_SLOW 0x02u
#define GL30_V6_FRAME_PAYLOAD_TRACE_CHUNK 0x05u
#define GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE 0x06u
#define GL30_V6_FRAME_PAYLOAD_HAPTIC_COMMAND 0x10u

#define GL30_MOTOR_STATE_FAST_LEN 68u
#define GL30_MOTOR_STATE_SLOW_LEN 64u
#define GL30_HAPTIC_STATE_LEN 36u
#define GL30_HAPTIC_COMMAND_LEN 64u

enum {
  GL30_HAPTIC_STATE_ENCODER_VALID = 1u << 0,
  GL30_HAPTIC_STATE_DETENT_READY = 1u << 1
};

#define GL30_HAPTIC_STATE_STATUS_MASK \
  (GL30_HAPTIC_STATE_ENCODER_VALID | GL30_HAPTIC_STATE_DETENT_READY)

enum {
  GL30_SENSOR_STATUS_INA228_CONFIGURED = 1u << 0,
  GL30_SENSOR_STATUS_INA228_VALID = 1u << 1,
  GL30_SENSOR_STATUS_VEML7700_CONFIGURED = 1u << 2,
  GL30_SENSOR_STATUS_VEML7700_VALID = 1u << 3,
  GL30_SENSOR_STATUS_VEML7700_SATURATED = 1u << 4
};

typedef enum {
  GL30_PARSE_NEED_MORE,
  GL30_PARSE_BAD_LENGTH,
  GL30_PARSE_BAD_CRC,
  GL30_PARSE_BAD_SYNC,
  GL30_PARSE_BAD_PAYLOAD_LEN,
  GL30_PARSE_UNSUPPORTED_VERSION,
  GL30_PARSE_OK
} gl30_parse_status_t;

typedef enum {
  GL30_SAFE_NORMAL,
  GL30_SAFE_WARN,
  GL30_SAFE_SAFE_ZERO,
  GL30_SAFE_COMM_LOST
} gl30_safe_state_t;

typedef struct {
  float angleRad;
  float velocityRadPerSec;
  float accelerationRadPerSec2;
  float iqRefA;
  float iqMeasA;
  float idMeasA;
  float torqueCmdNm;
  float torqueEstNm;
  float busVoltageV;
  float busCurrentA;
  int32_t logicalPosition;
  float subPosition;
  uint32_t motorState;
  uint32_t faultBits;
  uint32_t warningBits;
  uint16_t isrCycles;
  uint16_t encoderStatus;
  uint16_t droppedCmds;
  uint16_t reserved;
} gl30_motor_state_fast_t;

typedef struct {
  float busVoltageV;
  float busCurrentA;
  float busPowerW;
  float energyJ;
  float chargeC;
  float inaDieTemperatureC;
  float motorTemperatureC;
  float ambientLux;
  uint32_t uptimeMs;
  uint32_t inaDiag;
  uint32_t sensorStatus;
  uint32_t inaI2cErrors;
  uint32_t vemlI2cErrors;
  uint32_t telemetryDrops;
  uint32_t encoderCrcErrors;
  uint32_t focDeadlineMisses;
} gl30_motor_state_slow_t;

typedef struct {
  uint32_t profileId;
  uint32_t commandNonce;
  float targetPositionRad;
  float targetVelocityRadS;
  float detentWidthRad;
  float detentStrengthNm;
  float endstopMinRad;
  float endstopMaxRad;
  float endstopStrengthNm;
  float dampingNmPerRadS;
  float inertiaKgM2;
  float frictionNm;
  float userTorqueLimitNm;
  float activeSpeedLimitRadS;
  uint32_t modeFlags;
  uint32_t textureId;
} gl30_haptic_command_t;

typedef struct {
  uint32_t profileId;
  uint32_t commandNonce;
  uint32_t modeFlags;
  int32_t logicalPosition;
  float subPosition;
  float detentWidthRad;
  uint32_t motorState;
  uint32_t faultBits;
  uint32_t status;
} gl30_haptic_state_t;

typedef struct {
  uint16_t sync;
  uint8_t version;
  uint8_t type;
  uint16_t payload_len;
  uint16_t flags;
  uint32_t sequence;
  uint64_t timestamp_us;
  const uint8_t *payload;
  uint32_t crc32c;
} gl30_frame_t;

typedef struct {
  gl30_frame_t frame;
  gl30_parse_status_t status;
  const char *error;
} gl30_parse_result_t;

void gl30_crc32c(const uint8_t *data, size_t len, uint32_t *out_crc);

int gl30_encode_motor_state_fast(const gl30_motor_state_fast_t *in, uint8_t *out, size_t out_cap);
int gl30_decode_motor_state_fast(const uint8_t *in, size_t in_len, gl30_motor_state_fast_t *out);
int gl30_encode_motor_state_slow(const gl30_motor_state_slow_t *in, uint8_t *out, size_t out_cap);
int gl30_decode_motor_state_slow(const uint8_t *in, size_t in_len, gl30_motor_state_slow_t *out);
int gl30_encode_haptic_command(const gl30_haptic_command_t *in, uint8_t *out, size_t out_cap);
int gl30_decode_haptic_command(const uint8_t *in, size_t in_len, gl30_haptic_command_t *out);
int gl30_encode_haptic_state(const gl30_haptic_state_t *in, uint8_t *out, size_t out_cap);
int gl30_decode_haptic_state(const uint8_t *in, size_t in_len, gl30_haptic_state_t *out);

int gl30_frame_encode(uint8_t type, uint16_t flags, uint32_t sequence, uint64_t timestamp_us,
                      const uint8_t *payload, size_t payload_len,
                      uint8_t *out_frame, size_t out_cap, size_t *out_len);

void gl30_frame_parse_init(void);
gl30_parse_result_t gl30_frame_parse(const uint8_t *chunk, size_t chunk_len, gl30_frame_t *frame_out,
                                   size_t *consumed);

#endif
