#ifndef KK_OLED_H
#define KK_OLED_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** OLED 操作结果。异步刷新启动后请通过 OLED_GetLastStatus() 查询最终结果。 */
typedef enum {
    OLED_OK = 0,       /**< 操作成功。 */
    OLED_BUSY,         /**< 驱动正忙，本次请求未执行。 */
    OLED_ERROR,        /**< 初始化、启动或传输失败。 */
    OLED_UNSUPPORTED   /**< 当前驱动不支持该能力。 */
} OLED_Status;

/** 画布或文字方向，角度均为顺时针。 */
typedef enum {
    OLED_ROTATION_0 = 0, /**< 不旋转。 */
    OLED_ROTATION_90,    /**< 顺时针旋转 90°。 */
    OLED_ROTATION_180,   /**< 顺时针旋转 180°。 */
    OLED_ROTATION_270    /**< 顺时针旋转 270°。 */
} OLED_Rotation;

/** 图元前景像素如何作用于绘制缓冲区。 */
typedef enum {
    OLED_DRAW_CLEAR = 0, /**< 前景像素写入当前背景色。 */
    OLED_DRAW_SET,       /**< 前景像素写入当前前景色。 */
    OLED_DRAW_XOR        /**< 图元覆盖的每个像素各与原像素异或一次。 */
} OLED_DrawMode;

/** 文字和位图中值为 0 的背景像素是否写入。 */
typedef enum {
    OLED_BG_TRANSPARENT = 0, /**< 忽略文字或位图中的背景像素。 */
    OLED_BG_SOLID             /**< 用前景模式的反色写入背景像素。 */
} OLED_BackgroundMode;

/** 传入文字坐标的垂直参考语义。 */
typedef enum {
    OLED_FONT_POS_BASELINE = 0, /**< y 坐标表示文字基线。 */
    OLED_FONT_POS_TOP,          /**< y 坐标表示参考高度顶部。 */
    OLED_FONT_POS_BOTTOM,       /**< y 坐标表示参考高度底部。 */
    OLED_FONT_POS_CENTER        /**< y 坐标表示参考高度中心。 */
} OLED_FontPosition;

/** 计算文字参考高度时使用的 u8g2 兼容范围。 */
typedef enum {
    OLED_FONT_REF_TEXT = 0, /**< 使用 A 与 g 的常规文字高度。 */
    OLED_FONT_REF_EXTENDED, /**< 额外考虑括号等扩展文字高度。 */
    OLED_FONT_REF_ALL       /**< 使用字体中所有字形的最大高度。 */
} OLED_FontRefHeight;

/**
 * 核心绘图和提交累计指标；只由 UI 所有者读取。
 * clear_pixels 是实际写入清屏像素数，skipped_clears 是已知同色而跳过的清屏；
 * full_clears 是未能使用行覆盖范围、因而写完整绘制缓冲区的清屏次数。
 * compares 统计实际执行的整帧比较，identical_frames 是其中内容相同的次数。
 * 时间字段只覆盖对应的清屏或 memcmp 段，不包含绘图、驱动或提交。
 */
typedef struct {
    uint64_t clear_us;
    uint64_t compare_us;
    uint64_t clear_pixels;
    uint64_t retire_clear_us;     /**< Background clear time for retired async frames. */
    uint64_t retire_clear_pixels; /**< Pixels written by background retired-frame clears. */
    uint32_t clear_calls;
    uint32_t retire_clear_calls;
    uint32_t full_clears;
    uint32_t skipped_clears;
    uint32_t compares;
    uint32_t identical_frames;
    uint32_t skipped_compares; /**< Explicit frame-change hints that bypassed memcmp. */
} OLED_Metrics;

/** 初始化图形状态和屏幕，清空显存后点亮显示。 */
OLED_Status OLED_Init(void);

/** 阻塞刷新当前绘制帧，返回时提交已经完成。 */
OLED_Status OLED_Update(void);

/** 使用 driver 的中断路径启动异步刷新。 */
OLED_Status OLED_UpdateIT(void);

/** 使用 driver 的 DMA 路径启动异步刷新。 */
OLED_Status OLED_UpdateDMA(void);

/**
 * Mark the CURRENT drawing as intentionally changed; skip its optional memcmp.
 * Call after drawing on the UI owner only. This is a submission hint, not a
 * claim that every pixel differs. It is tied to that physical draw buffer,
 * survives BUSY, and is reset by Clear/Fill/Init or buffer reuse. Default Update
 * callers still deduplicate. Forced full recovery remains independent.
 */
void OLED_MarkFrameChanged(void);

/** Consume queued driver completions on the UI owner (never from an ISR). */
void OLED_Poll(void);

/*
 * 异步传输一旦启动，其冻结缓冲区在完成或失败后都会成为最近稳定可重发帧。
 * “稳定”只表示内存图像完整且不再被绘图修改，不保证失败帧已完整显示在屏幕上。
 */

/** 查询核心或驱动是否仍在执行异步刷新。 */
bool OLED_IsBusy(void);

/** 获取最近一次刷新或控制操作的状态。 */
OLED_Status OLED_GetLastStatus(void);

/** 复制当前累计指标快照；传入 NULL 时不执行操作。 */
void OLED_GetMetrics(OLED_Metrics *out);

/** 设置对比度，value 的有效范围为 0～255。 */
OLED_Status OLED_SetContrast(uint8_t value);

/** 进入或退出省电模式；退出时会先完整恢复最近稳定可重发帧。 */
OLED_Status OLED_SetPowerSave(bool enable);

/** 获取当前旋转方向下的逻辑画布宽度。 */
uint16_t OLED_GetWidth(void);

/** 获取当前旋转方向下的逻辑画布高度。 */
uint16_t OLED_GetHeight(void);

/** Number of physical framebuffers cycled by the renderer. */
uint8_t OLED_GetFrameBufferCount(void);

/** 设置后续绘图使用的 RGB565 前景色和背景色。 */
void OLED_SetColor(uint16_t foreground, uint16_t background);

/** 将当前绘制缓冲区按当前清除行策略填充为背景色。 */
void OLED_Clear(void);

/** 限制后续 Clear/自动缓冲复用清除到物理行半开区间 [y0,y1)。
 * 传入 0,height 恢复普通全帧清除；非法区间也安全回退全帧。
 * 只应用于需要保留静态 chrome 的场景，切换其他页面前必须恢复全帧。 */
void OLED_SetClearRows(uint16_t y0, uint16_t y1);

/** Record how the CURRENT draw frame may be cleared when it is retired later.
 * This is intentionally separate from OLED_SetClearRows(), which controls the
 * pre-draw Clear of the buffer currently owned by the UI. */
void OLED_SetRetireClearRows(uint16_t y0, uint16_t y1);

/** 将当前绘制缓冲区全部填充为当前前景色。 */
void OLED_Fill(void);

/** 将行主序、主机字节序的 RGB565 位图绘制到当前逻辑画布。 */
void OLED_BlitRGB565(int16_t x, int16_t y, uint16_t width, uint16_t height,
                     const uint16_t *pixels);

/** 按 alpha 将一个 RGB565 像素混合到当前逻辑画布。 */
void OLED_BlendPixelRGB565(int16_t x, int16_t y, uint16_t color,
                           uint8_t alpha);

/** 设置画布旋转；同时恢复为全屏裁剪窗口。 */
void OLED_SetRotation(OLED_Rotation rotation);

/** 设置后续图元的全局绘图模式。 */
void OLED_SetDrawMode(OLED_DrawMode mode);

/** 设置文字和位图的背景模式。 */
void OLED_SetBackgroundMode(OLED_BackgroundMode mode);

/** 设置半开裁剪窗口 [x,x+width) × [y,y+height)；宽高超限时保持原窗口不变。 */
void OLED_SetClipWindow(int16_t x, int16_t y, uint16_t width, uint16_t height);

/** 将裁剪窗口恢复为当前逻辑画布的全部区域。 */
void OLED_ResetClipWindow(void);

/*
 * 所有宽、高为 0 的图元均无操作；超过各接口注明上限的尺寸也无操作。
 * 只有处于这些安全范围内的坐标和尺寸才承诺按逻辑画布及裁剪窗口截断。
 */

/** 绘制一个像素。 */
void OLED_DrawPixel(int16_t x, int16_t y);

/** 从 (x,y) 向右绘制 width 个像素；width 不得超过 INT16_MAX。 */
void OLED_DrawHLine(int16_t x, int16_t y, uint16_t width);

/** 从 (x,y) 向下绘制 height 个像素；height 不得超过 INT16_MAX。 */
void OLED_DrawVLine(int16_t x, int16_t y, uint16_t height);

/** 绘制包含两个端点的直线。 */
void OLED_DrawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1);

/** 绘制精确宽高的矩形边框；宽高不得超过 INT16_MAX。 */
void OLED_DrawFrame(int16_t x, int16_t y, uint16_t width, uint16_t height);

/** 绘制精确宽高的实心矩形；宽高不得超过 INT16_MAX。 */
void OLED_DrawBox(int16_t x, int16_t y, uint16_t width, uint16_t height);

/** 绘制圆角矩形边框；宽高服从 INT16_MAX，圆角会先按矩形收缩。 */
void OLED_DrawRFrame(int16_t x, int16_t y, uint16_t width, uint16_t height,
                     uint16_t radius);

/** 绘制实心圆角矩形；宽高服从 INT16_MAX，圆角会先按矩形收缩。 */
void OLED_DrawRBox(int16_t x, int16_t y, uint16_t width, uint16_t height,
                   uint16_t radius);

/** 绘制圆周；半径超过当前逻辑画布较长边时无操作。 */
void OLED_DrawCircle(int16_t x, int16_t y, uint16_t radius);

/** 绘制实心圆；半径超过当前逻辑画布较长边时无操作。 */
void OLED_DrawDisc(int16_t x, int16_t y, uint16_t radius);

/** 绘制椭圆轮廓；任一半径超过逻辑画布较长边时无操作。 */
void OLED_DrawEllipse(int16_t x, int16_t y, uint16_t radius_x, uint16_t radius_y);

/** 绘制实心椭圆；任一半径超过逻辑画布较长边时无操作。 */
void OLED_DrawFilledEllipse(int16_t x, int16_t y, uint16_t radius_x,
                            uint16_t radius_y);

/** 绘制顺时针圆弧；起止角相等表示完整圆，半径服从画布上限。 */
void OLED_DrawArc(int16_t x, int16_t y, uint16_t radius,
                  int16_t start_angle, int16_t end_angle);

/** 绘制三角形边框。 */
void OLED_DrawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                       int16_t x2, int16_t y2);

/** 使用水平扫描线填充三角形。 */
void OLED_DrawFilledTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                             int16_t x2, int16_t y2);

/** 绘制 XBM；宽高单边不得超过当前逻辑画布较长边。 */
void OLED_DrawXBM(int16_t x, int16_t y, uint16_t width, uint16_t height,
                  const uint8_t *bitmap);

/**
 * 围绕位图内部锚点旋转后，将该锚点放到画布目标坐标。
 * 角度为顺时针整数角度，采用反向映射和最近邻采样。
 * 宽高服从画布单边上限，锚点必须位于源位图内部，否则无操作。
 */
void OLED_DrawRotatedXBM(int16_t target_x, int16_t target_y,
                         uint16_t width, uint16_t height,
                         const uint8_t *bitmap,
                         int16_t anchor_x, int16_t anchor_y,
                         int16_t angle);

/** 设置当前 u8g2 格式字体；传入 NULL 可关闭文字绘制。 */
void OLED_SetFont(const uint8_t *font);

/** 设置字形和字符串的前进方向。 */
void OLED_SetFontDirection(OLED_Rotation direction);

/** 设置文字坐标的垂直参考语义。 */
void OLED_SetFontPosition(OLED_FontPosition position);

/** 设置计算 Top、Bottom、Center 时采用的字体参考高度。 */
void OLED_SetFontRefHeight(OLED_FontRefHeight ref_height);

/** 绘制一个 BMP 字符，返回该字符的 advance。 */
int16_t OLED_DrawGlyph(int16_t x, int16_t y, uint16_t codepoint);

/** 绘制单行 UTF-8 字符串，返回全部字符的 advance 总和。 */
int16_t OLED_DrawUTF8(int16_t x, int16_t y, const char *utf8);

/** 计算 UTF-8 字符串的可见像素宽度。 */
uint16_t OLED_GetUTF8Width(const char *utf8);

/** 获取字符的 advance；缺字时返回空格的 advance。 */
int16_t OLED_GetGlyphAdvance(uint16_t codepoint);

/** 获取当前参考高度模式下的字体上升量。 */
int16_t OLED_GetFontAscent(void);

/** 获取当前参考高度模式下的字体下降量，通常为负数。 */
int16_t OLED_GetFontDescent(void);

#ifdef __cplusplus
}
#endif

#endif
