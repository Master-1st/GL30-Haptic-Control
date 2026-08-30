# 力反馈旋钮配置兼容与迁移计划

> 状态：V6 格式审计完成；SmartKnob 导入适配器尚未实现。本文只声明有证据的配置兼容范围。

## 范围

兼容目标必须同时满足：

1. 来源设备本身具有电机力反馈；
2. 上游存在公开、稳定、可序列化的触觉配置格式；
3. 配置中包含档位、端点、磁吸点、强度或其他可映射的触觉参数；
4. 来源版本和许可证能够固定并审计。

非力反馈的上层输入输出协议、自动化接口、普通旋转编码器设置和 UI-only 配置不属于本兼容计划。没有稳定外部触觉配置格式、只有写死算法或 C/C++ 常量的项目，也不进入兼容矩阵。

## 当前结论

- **GL30 AMOLED V6 `profileVersion: 1`：数据格式原生兼容。** V6 与当前 Schema 只有 `$id` 和标题不同，两个 V6 示例与当前示例逐字节一致；两个旧 Profile 对象都能直接通过当前 `parseProfile()` 校验。
- **SmartKnob `SmartKnobConfig`：字段可转换，但适配器尚未实现。** Protobuf 传输、归一化强度单位、运行时状态和硬件校准不能直接复制，必须经过 PC 侧转换、限幅和警告。

## 兼容等级

| 等级 | 含义 |
| --- | --- |
| `NATIVE_FORMAT` | 旧触觉配置不改字段即可进入当前校验器；不代表已经下发到实物 |
| `ADAPTER_PLANNED` | 存在明确字段映射，但导入器、单位转换和验证尚未完成 |
| `REJECTED_HARDWARE_DATA` | 与原电机、传感器或驱动板绑定的数据禁止迁移 |

## 当前兼容矩阵

| 来源 | 固定参考版本 | 当前等级 | 现在可以做什么 | 仍缺什么 |
| --- | --- | --- | --- | --- |
| GL30 AMOLED V6 Profile v1 | 本地 V6 最终设计包 | `NATIVE_FORMAT` | 逐个 Profile 对象可直接通过当前 Schema 与语义校验，旧示例验证 2/2 通过 | 文件导入 UI、批量转换报告、ESP32/STM32 全链路 |
| [SmartKnob `SmartKnobConfig`](https://github.com/scottbez1/smartknob/blob/4eb988399c3fda6ffd3006772856093dfe9adb86/proto/smartknob.proto) | `4eb9883`，Apache-2.0 | `ADAPTER_PLANNED` | 已完成字段级映射设计 | Protobuf 解析器、强度标定策略、fixtures、转换测试和导入 UI |

## 兼容层放在哪里

```text
旧力反馈 JSON / Protobuf 配置
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

STM32 实时核只接收一种已冻结的二进制命令，不解析旧 JSON/Protobuf，也不保留多套历史协议。兼容性由 PC 侧适配器承担，所有转换结果必须进入同一个校验和安全限幅流程。

## V6：当前已经具备的格式兼容

当前 [Profile Schema](../pc-companion/profiles/src/profile.schema.json) 直接继承 V6 Profile v1 的字段结构，包括应用匹配、触觉原语、显示设置、输入映射、数据源和用户安全上限。

必须注意：

- “旧 Profile 能通过校验”不等于已经有导入按钮；
- 格式兼容不表示无条件接受，语义矛盾或越过当前安全边界的旧配置仍会被拒绝；
- Schema 中存在但 STM32 尚未执行的 texture、asymmetry 等字段，导入后不会凭空产生实物效果；
- 当前设备二进制协议是 Canonical Profile 编译后的内部传输，不作为外部旧设备协议兼容承诺。

## SmartKnob：字段映射设计

SmartKnob 使用 Protobuf `SmartKnobConfig`。计划适配器只转换可移植的触觉语义：

| SmartKnob 字段 | Canonical Profile 目标 | 转换要求 |
| --- | --- | --- |
| `position_width_radians` | `haptic.detent.widthDeg` | 弧度转角度，可确定转换 |
| `detent_strength_unit` | `haptic.detent.strengthmNm` | **不能等值转换**；需要明确的强度映射或标定策略 |
| `endstop_strength_unit` | `haptic.endstops.strengthmNm` | 需要映射并受 GL30 安全上限约束 |
| `min_position` / `max_position` | `haptic.endstops.minPosition/maxPosition` | 保留逻辑位置，必须确认方向和零点 |
| `snap_point` | `haptic.detent.snapRatio` | 可转换，但必须检查范围 |
| `detent_positions[]` | `haptic.detent.magneticPositions[]` | 可转换，受当前 Profile 数量上限约束 |
| `snap_point_bias` | `haptic.detent.asymmetry` | 语义相近但比例不等价，必须警告并预览 |
| `text` | Profile 名称/说明 | 文本转换，不进入实时控制 |
| `position` / `sub_position_unit` | 运行时状态或目标 | 不应作为静态 Profile 导入 |
| `position_nonce` | 运行时 command nonce | 不属于持久 Profile |

SmartKnob 的 `MotorCalibration`、`StrainCalibration`、电角零位、方向、极对数以及任何旧 PID、电压和电流参数均为 `REJECTED_HARDWARE_DATA`，禁止导入 GL30。

## 每个适配器合并前必须满足

1. 来源是力反馈设备，并具有公开稳定的触觉配置格式；
2. 固定上游仓库、commit、配置格式和许可证；
3. 提交合法的最小 fixture，不复制无关资源；
4. 明确逐字段单位、坐标方向、默认值和信息损失；
5. 拒绝电角零位、PID、电流限制等硬件专属数据；
6. 输出确定性的 Canonical Profile；
7. 通过当前 Schema、语义规则和安全限幅；
8. 生成用户可读的转换警告与未映射字段列表；
9. golden test 覆盖成功、降级和拒绝三类结果；
10. 只有完成真实设备下发和触觉检查后，才能把状态从“格式转换”升级为“实物可用”。

## 未来力反馈配置格式预留

下表故意保留空白。只有满足本文范围的力反馈配置格式，才能填写来源版本、许可证、映射和验证证据；未填写前不代表承诺支持。

| 插槽 | 力反馈配置来源/格式 | 固定版本 | 许可证 | 兼容等级 | 映射文档 | Fixture | 验证结果 |
| ---: | --- | --- | --- | --- | --- | --- | --- |
| 1 |  |  |  |  |  |  |  |
| 2 |  |  |  |  |  |  |  |
| 3 |  |  |  |  |  |  |  |
| 4 |  |  |  |  |  |  |  |

<!-- 新增适配器时保留此标记：FUTURE_FORCE_FEEDBACK_CONFIG_ADAPTER_SLOT -->
