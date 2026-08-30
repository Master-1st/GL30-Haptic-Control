# GL30 AMOLED V7 产品版协议 v1（当前实现基线）

本文件记录当前唯一 V1 线协议。原始设计包是来源证据；本文件与金样是当前实现入口。

## 1. 帧布局（源定义）

全部多字节字段为小端：

| 绝对偏移 | 长度 | 字段 | 说明 |
|---:|---:|---|---|
| 0 | 2 | `sync` | `u16`，固定 `0xA55A` |
| 2 | 1 | `version` | `u8` |
| 3 | 1 | `type` | `u8` |
| 4 | 2 | `payload_len` | `u16` |
| 6 | 2 | `flags` | `u16` |
| 8 | 4 | `sequence` | `u32`，按方向独立 |
| 12 | 8 | `timestamp_us` | `u64`，源时钟的**测量时刻**，不是发包时刻 |
| 20 | N | `payload` | `payload_len` 字节 |
| 20+N | 4 | `crc32c` | `u32` |

固定开销为 24 B。CRC32C 覆盖绝对偏移 2 至 payload 尾，即 `version` 到 `payload`；`sync` 和 CRC 字段自身不参与。

原始设计包只定义了 `flags` 的字段宽度，**没有冻结各 bit 的含义**。当前参考实现把它视为不透明 `u16`，不得擅自分配 bit。

## 2. Packet Type 注册表（源定义）

| Type | 名称 | 方向 | 目标频率/用途 |
|---:|---|---|---|
| `0x01` | `MOTOR_STATE_FAST` | STM→ESP | 2 kHz 核心连续遥测 |
| `0x02` | `MOTOR_STATE_SLOW` | STM→ESP | 100 Hz 温度/能量/统计 |
| `0x03` | `FAULT_EVENT` | STM→ESP | 故障事件 |
| `0x04` | `TRACE_META` | STM→ESP | Trace 头 |
| `0x05` | `TRACE_CHUNK` | STM→ESP | Trace 数据 |
| `0x10` | `HAPTIC_COMMAND` | ESP→STM | 1 kHz 目标和模式 |
| `0x11` | `CONFIG_WRITE` | ESP→STM | 参数写入 |
| `0x12` | `CONFIG_COMMIT` | ESP→STM | 原子切换 |
| `0x13` | `TRACE_ARM` | ESP→STM | 触发配置 |
| `0x14` | `FAULT_CLEAR` | ESP→STM | 受控清故障 |
| `0x20` | `TIME_SYNC_REQ` | 双向 | 时间同步 |
| `0x21` | `TIME_SYNC_RESP` | 双向 | 时间同步 |
| `0x30` | `BOOT_INFO` | 双向 | 版本与能力 |
| `0x31` | `HEARTBEAT` | 双向 | 100 Hz 链路健康 |

当前唯一 STM32 产品构建实现 `0x01`/`0x02` 发送和 `0x10` 接收。其余登记类型尚未实现，收到未实现命令时必须安全拒绝。

## 3. 已实现 payload

- `MotorStateFast`：68 B；总帧 92 B。保持源结构中的 `int32 logical_position` 与 `float sub_position`。
- `MotorStateSlow`：64 B；总帧 88 B。100 Hz 上报功率/能量/温度/环境光和累计错误。
- `HapticCommand`：64 B；总帧 88 B。

对应确定性字节金样见 `protocol/generated/v1.json`。

### 3.1 MOTOR_STATE_SLOW

64 B payload 全部小端；浮点字段为 IEEE-754 binary32：

| payload 偏移 | 长度 | 字段 | 说明 |
|---:|---:|---|---|
| 0 | 4 | `bus_voltage_v` | INA228 母线电压 |
| 4 | 4 | `bus_current_a` | INA228 双向电流，正负号按 IN+→IN−定义 |
| 8 | 4 | `bus_power_w` | INA228 无符号功率 |
| 12 | 4 | `energy_j` | INA228 累计能量 |
| 16 | 4 | `charge_c` | INA228 双向累计电荷，可为负 |
| 20 | 4 | `ina_die_temperature_c` | INA228 芯片温度 |
| 24 | 4 | `motor_temperature_c` | PB2 模拟温度通道 |
| 28 | 4 | `ambient_lux` | VEML7700 环境光，已按当前增益/积分换算 |
| 32 | 4 | `uptime_ms` | 低 32 bit 运行时间 |
| 36 | 4 | `ina_diag` | 低 16 bit 为 INA228 `DIAG_ALRT` 原值 |
| 40 | 4 | `sensor_status` | 下述状态位 |
| 44 | 4 | `ina_i2c_errors` | INA228 I2C 累计失败 |
| 48 | 4 | `veml_i2c_errors` | VEML7700 I2C 累计失败 |
| 52 | 4 | `telemetry_drops` | STM UART 遥测累计丢弃 |
| 56 | 4 | `encoder_errors` | 工厂编码器链路/样本累计错误；`PENDING_VENDOR` 阶段固定为 0 |
| 60 | 4 | `foc_deadline_misses` | 40 kHz ISR 超期累计值 |

`sensor_status`：bit0=`INA228_CONFIGURED`，bit1=`INA228_VALID`，bit2=`VEML7700_CONFIGURED`，bit3=`VEML7700_VALID`，bit4=`VEML7700_SATURATED`；其余位固定 0。缺少慢速传感器只清状态位并累计错误，不替代 ADC/DRV8316/BKIN 的硬实时保护。

## 4. 参考实现保护边界

- 本工程本地限定 `MAX_PAYLOAD = 4096` B；这是实现保护值，不是已冻结的线协议上限。
- CRC 错、长度错误、非有限命令值、未支持版本或未实现命令类型均不执行命令。
- 未支持版本/类型只计数和拒绝；通信监督器仍按最后一帧**本地有效接收时刻**执行 10/20/100 ms 策略。普通固件在 20 ms 时立即撤销力矩许可，在 100 ms 时锁存通信丢失故障。
- `sequence` 连续性当前只监视，不作为单独拒绝条件。
- `timestamp_us` 在 TypeScript 中使用 `bigint`，不得缩为 32 位。

## 5. 尚未冻结

- `flags` 的 bit 分配；
- `0x03`–`0x31` 中除 `0x10` 外各 payload 的最终布局；
- 全局最大 payload、分片与重传细节；
- `0x05 TRACE_CHUNK` 的采集、分片、落盘和转发格式；当前 STM32 产品固件尚未实现。
