#ifndef BENCH_APP_H
#define BENCH_APP_H

#include <stdbool.h>

void Bench_Init(void);
void Bench_Loop(void);
void Bench_AdcIRQ(void);
void Bench_EncoderIRQ(void);
void Bench_UartIRQ(void);
void Bench_BreakIRQ(void);
void Bench_Fatal(void);

struct bench_adc_diag;
/* Memory-only copy. Caller holds a short IRQ lock for a coherent sample. */
void Bench_AdcDiagnostics(struct bench_adc_diag *out);
/* Main-loop diagnostic guard, also rechecked between driver transactions. */
bool Bench_DriverTraceSafe(void);

#endif
