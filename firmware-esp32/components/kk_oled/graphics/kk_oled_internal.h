#ifndef KK_OLED_INTERNAL_H
#define KK_OLED_INTERNAL_H

#include "kk_oled.h"
#include "kk_oled_driver.h"

#include <stdbool.h>
#include <stdint.h>

/** 单个 RGB565 帧缓冲区的像素数量。 */
#define OLED_BUFFER_SIZE OLED_PHYSICAL_PIXEL_COUNT

/* 图形文件只通过这些窄接口访问全局绘制状态。 */
/** 按当前前景模式绘制一个逻辑坐标像素。 */
void OLED_InternalPlot(int16_t x, int16_t y);

/** 按当前前景模式填充一条逻辑水平或垂直跨度，不暴露帧缓冲区。 */
void OLED_InternalPlotSpan(int32_t x, int32_t y, uint16_t length,
                           bool horizontal);

/** 按文字/位图的源像素值和背景模式写入一个逻辑坐标像素。 */
void OLED_InternalPlotSource(int16_t x, int16_t y, bool source_pixel);

/** 按文字/位图的源像素值填充一条逻辑水平或垂直跨度。 */
void OLED_InternalPlotSourceSpan(int32_t x, int32_t y, uint16_t length,
                                 bool horizontal, bool source_pixel);

/** 按逻辑坐标写入一个不受绘图模式影响的 RGB565 像素。 */
void OLED_InternalPlotColor(int16_t x, int16_t y, uint16_t color);

/** 按逻辑坐标将一个 RGB565 像素按 alpha 混合到当前绘制帧。 */
void OLED_InternalBlendColor(int16_t x, int16_t y, uint16_t color,
                             uint8_t alpha);

/** 获取当前画布旋转后的逻辑宽度。 */
uint16_t OLED_InternalGetLogicalWidth(void);

/** 获取当前画布旋转后的逻辑高度。 */
uint16_t OLED_InternalGetLogicalHeight(void);

/** 读取当前半开区间裁剪窗口。 */
void OLED_InternalGetClip(int16_t *x0, int16_t *y0, int16_t *x1, int16_t *y1);

/** 将半开包围盒收缩到当前裁剪窗口；没有交集时返回 false。 */
bool OLED_InternalIntersectClip(int32_t *x0, int32_t *y0,
                                int32_t *x1, int32_t *y1);

/** 查询整数角度对应的 Q15 正弦值。 */
int16_t OLED_InternalSinQ15(int16_t angle);

/** 查询整数角度对应的 Q15 余弦值。 */
int16_t OLED_InternalCosQ15(int16_t angle);

/* 异步驱动在回调中读取已冻结的传输快照。 */
/** 获取本次刷新期间保持不变的帧缓冲区。 */
/* Storage bytes are panel-wire-order MSB-first RGB565, NOT native RGB565 word values. */
const uint16_t *OLED_InternalGetTransferBuffer(void);

/** 由驱动在异步传输结束时提交最终结果。 */
void OLED_InternalTransferFinished(OLED_Status status);

#if defined(KK_OLED_TEST)
/* 测试构建专用观察口，不属于安装接口。 */
/** 获取当前绘制缓冲区，只供主机测试检查像素。 */
const uint16_t *OLED_InternalTestGetDrawBuffer(void);

/** 获取最近稳定可重发帧，只供主机测试检查提交结果。 */
const uint16_t *OLED_InternalTestGetStableBuffer(void);

/** 查询下次刷新是否已被强制为全屏，只供主机测试。 */
bool OLED_InternalTestIsForceFull(void);
#endif

#endif
