#ifndef KK_OLED_DRIVER_H
#define KK_OLED_DRIVER_H

#include "kk_oled.h"

#include <stdbool.h>
#include <stdint.h>

/** 目标彩色 AMOLED 的物理宽度。 */
#define OLED_PHYSICAL_WIDTH 466U
/** 目标彩色 AMOLED 的物理高度。 */
#define OLED_PHYSICAL_HEIGHT 466U
/** 以 uint16_t RGB565 像素计的单帧元素数量。 */
#define OLED_PHYSICAL_PIXEL_COUNT \
    ((uint32_t)OLED_PHYSICAL_WIDTH * (uint32_t)OLED_PHYSICAL_HEIGHT)
/** 以字节计的单帧大小；466×466×2 = 434312。 */
#define OLED_FRAMEBUFFER_BYTES \
    (OLED_PHYSICAL_PIXEL_COUNT * (uint32_t)sizeof(uint16_t))

/** 初始化 466×466 AMOLED，清空整帧并点亮显示。 */
OLED_Status OLED_DriverInit(void);

/** 阻塞发送核心准备好的完整 RGB565 帧。 */
OLED_Status OLED_DriverWriteBlocking(void);

/** 启动真正的异步 RGB565 刷新；当前 BSP 未接入时返回 OLED_UNSUPPORTED。 */
OLED_Status OLED_DriverWriteIT(void);

/** 启动真正的 DMA RGB565 刷新；当前 BSP 未接入时返回 OLED_UNSUPPORTED。 */
OLED_Status OLED_DriverWriteDMA(void);

/** 查询驱动异步状态机是否正在传输。 */
bool OLED_DriverIsBusy(void);

/** Poll completion events on the UI owner; never fabricate DMA completion. */
void OLED_DriverPoll(void);

/** 阻塞发送 AMOLED 对比度命令。 */
OLED_Status OLED_DriverSetContrast(uint8_t value);

/** 阻塞发送显示关闭或显示开启命令。 */
OLED_Status OLED_DriverSetPowerSave(bool enable);

/* 由应用外设回调转发，不直接占用 SDK 全局弱回调。 */
/** 处理一次异步帧传输完成事件并推进 driver 状态机。 */
void OLED_DriverHandleMemTxComplete(void);

/** 终止当前异步传输，并向核心报告错误。 */
void OLED_DriverHandleError(void);

#endif
