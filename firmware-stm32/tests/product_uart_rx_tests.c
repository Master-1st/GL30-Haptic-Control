/* CMake extracts the real uart_rx_drain body, not a rewritten algorithm. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define GL30_UART_RX_BUFFER_SIZE 256u
#define DMA1 1
#define LL_DMA_CHANNEL_1 1
static uint8_t g_uart_rx[GL30_UART_RX_BUFFER_SIZE];
static volatile uint16_t g_uart_rx_consumed;
static uint32_t remaining;
static unsigned polls,bytes,segments,seen[256];
static bool force_reload,arrival_during_parse;
static uint32_t LL_DMA_GetDataLength(int dma,int channel) {
  (void)dma; (void)channel;
  /* Bound the old loop in a failing test: DMA later reloads anyway. */
  if (++polls>4u && force_reload) { remaining=256u; }
  return remaining;
}
static void parse_bytes(const uint8_t *data,size_t count) {
  segments++; bytes+=(unsigned)count;
  for(size_t i=0;i<count;i++) { seen[data-g_uart_rx+i]++; }
  if(arrival_during_parse) { remaining=206u; arrival_during_parse=false; }
}
#include "product_uart_rx_under_test.inc"
static unsigned failed,checks;
#define CHECK(x,msg) do { checks++; if (!(x)) { failed++; fprintf(stderr,"[FAIL] %s\n",msg); } } while(0)
static void reset(uint16_t consumed,uint32_t ndtr) {
  g_uart_rx_consumed=consumed;remaining=ndtr;polls=bytes=segments=0u;
  force_reload=arrival_during_parse=false;
  for(unsigned i=0;i<256;i++) { seen[i]=0u; }
}
int main(void) {
  reset(128u,0u);force_reload=true;
  uart_rx_drain();
  CHECK(bytes==128u,"NDTR=0 consumes only the second half, never repeats a full buffer");
  CHECK(g_uart_rx_consumed==0u,"terminal DMA position normalizes to zero");
  CHECK(seen[0]==0u && seen[255]==1u,"each unread terminal byte consumed once");
  reset(240u,240u); uart_rx_drain();
  CHECK(bytes==32u && segments==2u,"wrapped partial input is delivered as two correct spans");
  CHECK(g_uart_rx_consumed==16u && seen[0]==1u && seen[239]==0u,"wrapped partial indices correct");
  reset(0u,256u);uart_rx_drain();CHECK(bytes==0u,"idle DMA does not fabricate a frame");
  reset(0u,240u);arrival_during_parse=true;uart_rx_drain();
  CHECK(bytes==16u && g_uart_rx_consumed==16u,"ISR drains one entry snapshot, not an unbounded moving producer");
  uart_rx_drain();
  CHECK(bytes==50u && g_uart_rx_consumed==50u,"later IRQ drains newly arrived bytes exactly once");
  printf("product UART RX: %u checks, %u failed\n",checks,failed);
  return failed ? 1 : 0;
}
