#ifndef GL30_APP_H_
#define GL30_APP_H_

void gl30_app_init(void);
void gl30_app_run_once(void);
void gl30_app_emergency_off(void);

void gl30_app_dma1_channel1_irq(void);
void gl30_app_dma1_channel2_irq(void);
void gl30_app_adc1_2_irq(void);
void gl30_app_tim1_break_irq(void);
void gl30_app_tim2_irq(void);
void gl30_app_usart3_irq(void);
void gl30_app_tim6_irq(void);
void gl30_app_tim7_irq(void);

#endif
