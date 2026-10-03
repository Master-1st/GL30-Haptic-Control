#ifndef KK_UI_DRAW_H
#define KK_UI_DRAW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stateless animation helpers shared by built-in and custom pages.
 * Q12 progress is 0..4096. No allocation, clock or refresh ownership.
 * Ease: duration <= UINT32_MAX/300; elapsed>=duration completes immediately.
 * Lerp: progress<=4096; from, to and their difference must fit int32_t. */
uint16_t KK_UI_EaseQ12(uint32_t elapsed, uint32_t duration);
int32_t KK_UI_LerpQ12(int32_t from, int32_t to, uint16_t progress);

void KK_UI_DrawFocus(int16_t x, int16_t y, uint16_t width, uint16_t height,
                     uint16_t radius);
void KK_UI_DrawButton(int16_t x, int16_t y, uint16_t width, uint16_t height,
                      const char *label, bool focused);
void KK_UI_DrawProgressBar(int16_t x, int16_t y, uint16_t width,
                           uint16_t height, uint16_t value,
                           uint16_t maximum);
void KK_UI_DrawScrollbar(int16_t x, int16_t y, uint16_t height,
                         uint16_t visible, uint16_t total,
                         uint16_t first_visible);
void KK_UI_DrawSwitch(int16_t x, int16_t y, uint16_t width, uint16_t height,
                      bool on, uint16_t progress_q12);

#ifdef __cplusplus
}
#endif

#endif
