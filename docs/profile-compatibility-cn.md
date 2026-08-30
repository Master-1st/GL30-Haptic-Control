# Profile 兼容与迁移计划

> 状态：接口审计完成，导入适配器尚未实现。本文只声明有证据的兼容范围。

## 结论

- **GL30 AMOLED V6 `profileVersion: 1` Profile：数据格式原生兼容。** V6 与当前 Schema 只有 `$id` 和标题不同，两个 V6 示例与当前示例逐字节一致；两个旧 Profile 对象都能直接通过当前 `parseProfile()` 校验。
- **V6 实时帧和 `HAPTIC_COMMAND`：已实现子集在规格与字节布局上兼容。** 当前 V1 保留 `0xA55A` 帧头、CRC32C、Packet Type、`0x01 MOTOR_STATE_FAST` 和 64 B `0x10 HAPTIC_COMMAND` 布局。V6 只登记了 `0x02` 的类型和用途，没有冻结其 payload；规格兼容也不能写成两台实物设备已经互通。
- **SmartKnob、X-Knob 和 SuperDial：当前不能直接导入或直接连线。** 它们与本项目存在大量可转换的触觉语义，但文件、传输、单位、硬件增益和安全边界不同，需要 PC 侧适配器。

## 兼容等级

| 等级 | 含义 |
| --- | --- |
| `NATIVE_FORMAT` | 源数据不改字段即可进入当前校验器；不代表已经下发到实物 |
| `SPEC_SUBSET` | 线格式中的已实现子集一致；未实现消息仍会安全拒绝 |
| `ADAPTER_PLANNED` | 能建立明确映射，但必须转换、限幅并报告信息损失 |
| `SEMANTIC_REFERENCE` | 只能复用交互思想或人工重建，源项目没有可移植配置文件 |
| `REJECTED_HARDWARE_DATA` | 与特定电机/传感器绑定的数据禁止迁移 |

## 当前兼容矩阵

| 来源 | 固定参考版本 | 当前等级 | 现在可以做什么 | 仍缺什么 |
| --- | --- | --- | --- | --- |
| GL30 AMOLED V6 Profile v1 | 本地 V6 最终设计包 | `NATIVE_FORMAT` | 逐个 Profile 对象可直接通过当前 Schema 与语义校验 | 文件导入 UI、批量导入报告、ESP32/STM32 全链路 |
| GL30 AMOLED V6 实时协议 | 本地 V6 通信手册 | `SPEC_SUBSET` | 当前已实现 `0x01`、`0x02`、`0x10`；帧头、`0x01` 和 `0x10` 保持 V6 布局，`0x02` payload 是当前工程补充 | 其他 Packet Type、实物互通、能力协商 |
| [SmartKnob](https://github.com/scottbez1/smartknob/tree/4eb988399c3fda6ffd3006772856093dfe9adb86) | `4eb9883`，Apache-2.0 | `ADAPTER_PLANNED` | 可设计 `SmartKnobConfig` Protobuf → Canonical Profile 转换 | 导入器、单位/强度标定、golden fixtures、映射测试 |
| [X-Knob](https://github.com/SmallPond/X-Knob/tree/05be44fc62b27c4fa941aabd2a7e9b2553f91fb9) | `05be44f`，MIT | `ADAPTER_PLANNED` | 可把静态 `XKnobConfig` 模式人工或工具化转换 | 它的触觉模式是 C++ 常量，不是可直接导入的配置文件 |
| [SuperDial](https://github.com/CharlieYu4994/superdial/tree/1973d9436a7220f16eec6f76aac6d7029588c03f) | `1973d94`，MIT | `SEMANTIC_REFERENCE` | 可参考 BLE Dial/HID、同心结构和基础回中行为 | 没有可移植的 Haptic Profile 文件；只能人工重建行为 |
| Surface Dial / HID | 协议与系统接口待固定 | `ADAPTER_PLANNED` | 可作为上层输入输出目标 | HID 描述符、主机适配、平台测试 |
| Home Assistant / MQTT | 数据模型待固定 | `ADAPTER_PLANNED` | 可映射 Profile action 与 data source | Topic/实体发现、权限、安全和断线行为 |

## 兼容层放在哪里

```text
旧 JSON / Protobuf / C 常量 / 社区预设
                  ↓
        PC Companion Source Adapter
                  ↓
       Canonical Haptic Profile v1
                  ↓
       Schema + 语义 + 安全限幅
                  ↓
       转换报告 / 预览 / 用户确认
                  ↓
      Binary Haptic Command / Config
                  ↓
            ESP32 → STM32
```

STM32 实时核只接收一种已冻结的二进制命令，不解析 JSON、Protobuf，也不保留多套历史协议。兼容性由 PC 侧适配器承担，转换结果必须进入同一个安全限幅和验证流程。

## V6：当前已经保留下来的兼容性

当前 [Profile Schema](../pc-companion/profiles/src/profile.schema.json) 直接继承 V6 Profile v1 的字段结构，包括：

- 应用匹配；
- detent、endstop、damping、friction、inertia、texture 和 active position；
- 显示页面、主题与防烧屏设置；
- 输入映射、action、data source 和用户安全上限。

当前 [协议 v1](../protocol/schema/protocol-v1.md) 也保留 V6 设计中的帧头、时间戳、CRC32C、类型注册表、`MOTOR_STATE_FAST` 和 `HAPTIC_COMMAND` 字段顺序；V6 未定义 `MOTOR_STATE_SLOW` 的 payload，因此当前慢速载荷不宣称字节兼容。

必须注意：

- “旧 JSON 能通过校验”不等于已经有导入按钮；
- 格式兼容不表示无条件接受；旧 Schema 允许但语义矛盾或越过当前安全边界的配置仍会被拒绝；
- “字节布局相同”不等于已经完成两块实物板互通；
- V6 文档中登记但尚未实现的消息，当前仍会安全拒绝；
- Schema 中存在但 STM32 尚未执行的 texture、asymmetry 等字段，导入后也不会凭空产生实物效果。

## SmartKnob：建议的字段映射

SmartKnob 使用 Protobuf `SmartKnobConfig`，不是本项目 JSON Profile。建议适配器按以下规则转换：

| SmartKnob 字段 | Canonical Profile 目标 | 转换要求 |
| --- | --- | --- |
| `position_width_radians` | `haptic.detent.widthDeg` | 弧度转角度，可确定转换 |
| `detent_strength_unit` | `haptic.detent.strengthmNm` | **不能直接等值转换**；必须用源硬件强度标定或用户选择的映射曲线 |
| `endstop_strength_unit` | `haptic.endstops.strengthmNm` | 同样需要标定并受 GL30 安全上限约束 |
| `min_position` / `max_position` | `haptic.endstops.minPosition/maxPosition` | 保留逻辑位置；必须确认方向和零点 |
| `snap_point` | `haptic.detent.snapRatio` | 可转换，但必须检查范围 |
| `detent_positions[]` | `haptic.detent.magneticPositions[]` | 可转换，当前 Profile 上限为 64 个位置 |
| `snap_point_bias` | `haptic.detent.asymmetry` | 语义相近但比例不等价，需要预览和警告 |
| `text` | Profile 名称/说明 | 文本转换，不参与实时控制 |
| `led_hue` | `display.accent` | 可选 UI 转换，颜色空间需要定义 |
| `position` / `sub_position_unit` | 运行时状态或目标 | 不应静态写入 Profile；需单独的状态/命令语义 |
| `position_nonce` | 运行时 command nonce | 不属于持久 Profile |

SmartKnob 的 `MotorCalibration`、`StrainCalibration`、电角零位、方向、极对数以及任何旧 PID/电压/电流参数必须标记为 `REJECTED_HARDWARE_DATA`。这些数据只对原硬件有效，禁止导入 GL30。

## X-Knob 与 SuperDial

X-Knob 的 `XKnobConfig` 继承了与 SmartKnob 相似的 `position_width_radians`、detent/endstop strength 和 `snap_point` 语义，因此可以共用大部分转换规则。但它的模式保存在 `motor.cpp` 的 C++ 静态数组中；`SystemSave.json` 保存的是系统设置，不是完整触觉 Profile。第一版适配器应接受人工整理后的中间 JSON，而不是尝试执行或解析任意 C++。

SuperDial 的回中力矩、页面行为和 BLE Dial 逻辑主要写死在 `main.ino`，没有稳定的外部触觉配置格式。它适合做行为和交互参考，不应宣传成“文件兼容”。

## 每个适配器合并前必须满足

1. 固定上游仓库、commit、文件格式和许可证；
2. 提交合法的最小 fixture，不复制无关资源；
3. 明确逐字段单位、坐标方向、默认值和信息损失；
4. 拒绝电角零位、PID、电流限制等硬件专属数据；
5. 输出确定性的 Canonical Profile；
6. 通过当前 Schema、语义规则和安全限幅；
7. 生成用户可读的转换警告与未映射字段列表；
8. golden test 覆盖成功、降级和拒绝三类结果；
9. 只有连通真实设备后，才能把状态从“格式转换”升级为“实物互通”。

## 未来兼容目标预留

下表故意保留空白，新增目标时填写来源版本、许可证、映射和验证证据；未填写前不代表承诺支持。

| 插槽 | 来源/格式 | 固定版本 | 许可证 | 兼容等级 | 映射文档 | Fixture | 验证结果 |
| ---: | --- | --- | --- | --- | --- | --- | --- |
| 1 |  |  |  |  |  |  |  |
| 2 |  |  |  |  |  |  |  |
| 3 |  |  |  |  |  |  |  |
| 4 |  |  |  |  |  |  |  |

<!-- 新增适配器时保留此标记，并在上表追加一行：FUTURE_COMPATIBILITY_ADAPTER_SLOT -->
