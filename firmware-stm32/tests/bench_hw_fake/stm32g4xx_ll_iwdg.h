#ifndef STM32G4XX_LL_IWDG_H
#define STM32G4XX_LL_IWDG_H

#include <stdint.h>
#include <stddef.h>
#include "main.h"

typedef struct {
  uint32_t KR;
  uint32_t PR;
  uint32_t RLR;
  uint32_t WINR;
  uint32_t SR;
} IWDG_TypeDef;

static IWDG_TypeDef g_iwdg_obj;
#define IWDG (&g_iwdg_obj)

static uint32_t g_iwdg_reload_count;

static inline void LL_IWDG_ReloadCounter(IWDG_TypeDef *IWDGx) {
  (void)IWDGx;
  ++g_iwdg_reload_count;
}

#endif
