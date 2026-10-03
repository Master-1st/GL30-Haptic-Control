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
| `0x06` | `HAPTIC_STATE` | STM→ESP | 当前控制快照与菜单状态 |
| `0x10` | `HAPTIC_COMMAND` | ESP→STM | 1 kHz 目标和模式 |
| `0x11` | `CONFIG_WRITE` | ESP→STM | 参数写入 |
| `0x12` | `CONFIG_COMMIT` | ESP→STM | 原子切换 |
| `0x13` | `TRACE_ARM` | ESP→STM | 触发配置 |
| `0x14` | `FAULT_CLEAR` | ESP→STM | 受控清故障 |
| `0x15` | `CONTROL_LEASE` | ESP→STM | 控制权查询、释放与重新获取 |
| `0x20` | `TIME_SYNC_REQ` | 双向 | 时间同步 |
| `0x21` | `TIME_SYNC_RESP` | 双向 | 时间同步 |
| `0x30` | `BOOT_INFO` | 双向 | 版本与能力 |
| `0x31` | `HEARTBEAT` | 双向 | 100 Hz 链路健康 |

当前唯一 STM32 产品构建实现 `0x01`/`0x02`/`0x06` 发送和 `0x10`/`0x15` 接收。其余登记类型尚未实现，收到未实现命令时必须安全拒绝。

## 3. 已实现 payload

- `MotorStateFast`：68 B；总帧 92 B。保持源结构中的 `int32 logical_position` 与 `float sub_position`。
- `MotorStateSlow`：64 B；总帧 88 B。100 Hz 上报功率/能量/温度/环境光和累计错误。
- `HapticState`：44 B；总帧 68 B。发送当前生效命令标识、逻辑位置/细分位置、状态和控制租约代次。
- `HapticCommand`：72 B；总帧 96 B。payload 偏移 64 追加小端 `u64 lease_generation`。
- `ControlLease`：24 B；总帧 48 B。

两端固件必须使用同一份当前布局；接收端按精确长度校验，不提供旧布局兼容。

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
| 56 | 4 | `encoder_errors` | C 字段 `encoderCrcErrors`；CET6 AS5048A 端口报告奇偶校验失败的采集轮数，同轮两个回复均出错只计一次；其他错误通过本地诊断读取 |
| 60 | 4 | `foc_deadline_misses` | 40 kHz ISR 超期累计值 |

`sensor_status`：bit0=`INA228_CONFIGURED`，bit1=`INA228_VALID`，bit2=`VEML7700_CONFIGURED`，bit3=`VEML7700_VALID`，bit4=`VEML7700_SATURATED`；其余位固定 0。缺少慢速传感器只清状态位并累计错误，不替代 ADC/DRV8316/BKIN 的硬实时保护。

### 3.2 HAPTIC_STATE

44 B payload 全部小端；浮点字段为 IEEE-754 binary32：

| payload 偏移 | 长度 | 字段 | 说明 |
|---:|---:|---|---|
| 0 | 4 | `profile_id` | 当前生效命令的 profile 标识 |
| 4 | 4 | `command_nonce` | 当前生效命令的 nonce |
| 8 | 4 | `mode_flags` | 当前生效命令的模式位 |
| 12 | 4 | `logical_position` | 当前逻辑位置/档位索引 |
| 16 | 4 | `sub_position` | 非 DETENT 时按当前状态语义解释；只有 `DETENT_READY` 且 `detent_width_rad > 0` 时，才解释为格内比例 |
| 20 | 4 | `detent_width_rad` | 当前档宽；必须为有限非负数，非 DETENT/未 ready 时为 0 合法 |
| 24 | 4 | `motor_state` | 当前电机状态 |
| 28 | 4 | `fault_bits` | 当前故障位 |
| 32 | 4 | `status` | 下述编码器、档位和控制租约状态位 |
| 36 | 8 | `lease_generation` | 当前控制租约代次；尚未获取时为 0 |

`status`：bit0=`ENCODER_VALID`，bit1=`DETENT_READY`，bit2=`CONTROL_RELEASED`，bit3=`CONTROL_WAITING_ZERO`，其余位固定 0。bit2/bit3 不能同时置位。菜单接受的样本要求 status 恰为 3、代次与当前命令一致且档宽为正；未 ready 的样本不得用于菜单定位。STM DETENT 逻辑沿用超过 `±0.55×detent_width_rad` 才跨档的迟滞边界。

### 3.3 HAPTIC_COMMAND 的零限值约定（2026-10-02）

当前已获取的租约内，有效命令的 `userTorqueLimitNm == 0` 保留为普通停力/空闲心跳：STM32 刷新通信租约、撤销 arm，并确保 FOC 中性及桥输出关闭（MOE=0、DRVOFF高）。已中性且输出已关闭时不重复复位档位相位，也不取消空闲驱动配置。普通停力保留已合格配置、采零和供电请求；紧急故障作废资格并锁存 FAILED。未获取、已释放、代次不匹配、非有限、负值或超过本地接收年龄的命令均无效。

`modeFlags == 0` 本身不是停力：非零阻尼仍可产生力矩。正限值命令保留原启动、编码器、采零、电角度、接收年龄与 READY 时刻门控。租约获取后的首个普通心跳必须为完全零命令，且 nonce 和代次与获取请求一致。

ESP 在 UI 与 READY/ACTIVE FAST 状态健康时发送零限值心跳，即使本地未 armed；未对齐/未知状态、失联或界面停止更新时，不持续发送。未被 UART 完整接受的停力帧保留重发，停止发送后的20/100 ms安全策略仍适用。显式 MOTOR OFF、反馈失配或健康失效会撤销本地授权；恢复通信只恢复零心跳，需再次 MOTOR ARM 才允许菜单出力。正常页面切换保留此前人工授予的授权，离开菜单发送零限值、重返菜单可使用仍有效的授权；页面切换不等同于 MOTOR OFF。

### 3.4 CONTROL_LEASE 与设置保存

| payload 偏移 | 长度 | 字段 | 说明 |
|---:|---:|---|---|
| 0 | 4 | `action` | 0=RELEASE，1=ACQUIRE，2=QUERY |
| 4 | 4 | `zero_nonce` | 完全零命令的非零 nonce；QUERY 为 0 |
| 8 | 8 | `current_generation` | 当前租约代次；QUERY 为 0 |
| 16 | 8 | `next_generation` | ACQUIRE 的新非零代次；RELEASE/QUERY 为 0 |

QUERY 所有其余字段必须为 0，仅请求当前 HAPTIC_STATE，不获取、释放或续租。ACQUIRE 要求 next 不同于 current；STM 只从尚未获取或已释放状态接受，且已释放时 nonce 不能重复上次释放值。接受后输出保持关闭，并置 WAITING_ZERO；回执只是构造零命令的确认，不能冒充首个普通零心跳已应用。

RELEASE 必须精确匹配当前已生效的完全零命令 nonce/代次，并满足未 arm、驱动输出关闭、没有 arm 过程。接受后关闭输出、清普通待处理命令及通信时间戳，进入 RELEASED。此阶段拒绝所有普通命令，保留故障和本地保护；仅在收到控制请求时提交一次合并的 HAPTIC_STATE 回执，成功启动 UART DMA 后才消耗该回执。精确重复请求可重新索取回执，但不续租。控制请求在独立槽中捕获，前台优先处理；本地接收年龄达到 10 ms 即拒绝。

ESP 在设置稳定 1.5 s 且本地未授权出力时申请维护。先发同代次完全零命令，收到匹配回显后释放；收到 RELEASED 回执后才持有电机静默预约，同时暂停界面提交并取得无 DMA 在途的显示预约，随后写一个 NVS blob。释放等待超过 2 s 不写 Flash。无 STM 伙伴时设置仅留 RAM，不以失联为保存许可。

保存结束丢弃暂停期间的 RX 缓冲，以新的代次重新获取并完成首零心跳；ARM 仍需新的人为命令。启动恢复 NVS 在显示、电机和输入任务开始前完成。运行时 NVS 写失败不保证旧数据回滚，保留 pending 并报告持久化结果不确定，不自动擦除分区。

64 位代次只用于区分事务；本次 ESP boot 内不复用，跨重启随机种子仍有碰撞概率。零回显和租约回执是软件证据，不证明实际相线、电流、制动器或 GPIO 已按预期动作。

## 4. 参考实现保护边界

- 本工程本地限定 `MAX_PAYLOAD = 4096` B；这是实现保护值，不是已冻结的线协议上限。
- CRC 错、长度错误、非有限命令值、未支持版本或未实现命令类型均不执行命令。
- 未支持版本/类型只计数和拒绝；通信监督器仍按最后一帧**本地有效接收时刻**执行 10/20/100 ms 策略。普通固件在 20 ms 时立即撤销力矩许可，在 100 ms 时锁存通信丢失故障。
- `sequence` 连续性当前只监视，不作为单独拒绝条件。
- `timestamp_us` 在 TypeScript 中使用 `bigint`，不得缩为 32 位。

## 5. 尚未冻结

- `flags` 的 bit 分配；
- 除已实现 `0x06`/`0x10`/`0x15` 外，其余登记但未实现类型的 payload 最终布局；
- 全局最大 payload、分片与重传细节；
- `0x05 TRACE_CHUNK` 的采集、分片、落盘和转发格式；当前 STM32 产品固件尚未实现。
