# GL30 ESP32-S3 工程框架

目录按 V7 Product Edition 架构建立：`bsp / ui / transport / audio / profiles / assets / ota`。本轮优先完成 STM32，因此这里尚无可烧录 ESP-IDF 应用，不能标记为 `BUILD_ONLY`。

已冻结接口：

- USART 5 Mbaud、8N1、全双工 DMA；不实现旧草案的 6 Mbaud。
- Waveshare J1-11 `ESP TX -> STM PB11 RX`。
- Waveshare J1-12 `ESP RX <- STM PB10 TX`。
- GP1 接经开漏缓冲后的 `SYS_FAULT_N`；GP0 首版 DNP；GP2 禁止外部驱动。
- ESP UART0 启动日志必须关闭或移动，不能污染 V1 二进制链路。
- 3.3 V 电源不通过扩展口互相回灌，只共地和信号。

下一最小闭环：`transport` 接收 STM `MOTOR_STATE_FAST`/`TRACE_CHUNK`，CRC 正确后进入无 UI 的录制/回放；该闭环通过后再接 AMOLED/LVGL。
