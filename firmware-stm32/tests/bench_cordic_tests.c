#include <stdio.h>
#include <math.h>
#include <float.h>
#include "bench/NUCLEO_G474RE_FOC/Core/Src/bench_cordic.c"
#include "control/foc.c"
static unsigned checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); } } while (0)
void bench_hw_watchdog_feed(void) { ++reloads; }
static void reset(void) {
  g_cordic_ready = false; fake_cordic.CSR = 0u; clock_on = true;
  never_ready = corrupt_result = false; ready_after = 0u;
  writes = reads = polls = reloads = write_index = read_index = 0u; residual_lsb = 0;
}
static gl30_foc_state_t state_at(float angle) {
  gl30_foc_state_t s; gl30_foc_init(&s); s.observer_initialized = true;
  s.theta_elec_rad = angle; s.i_q_ref_a = 0.1f; return s;
}
int main(void) {
  reset(); float s=77.0f,c=88.0f;
  CHECK(!gl30_foc_sincos(0.0f,&s,&c) && writes==0u && s==77.0f && c==88.0f);
  CHECK(bench_cordic_startup_test());
  CHECK(g_cordic_diag.samples==65537u && g_cordic_diag.failures==0u);
  CHECK(writes==2u*65537u && reads==writes && reloads==257u && CORDIC->CSR==CORDIC_CONFIG);
  CHECK(g_cordic_diag.max_error<=1.0e-6f && g_cordic_diag.max_norm_error<=3.0e-6f);
  const float bad[]={NAN,INFINITY,-INFINITY,FLT_MAX,-FLT_MAX,4.0f,-4.0f};
  unsigned old=writes;
  for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) { CHECK(!gl30_foc_sincos(bad[i],&s,&c)); }
  CHECK(!gl30_foc_sincos(0.0f,NULL,&c)); CHECK(!gl30_foc_sincos(0.0f,&s,NULL));
  CHECK(writes==old);
  CHECK(gl30_foc_sincos(CORDIC_PI_F,&s,&c) && last_phase==INT32_MAX && fabsf(s)<1e-6f && c<-.999999f);
  CHECK(gl30_foc_sincos(-CORDIC_PI_F,&s,&c) && last_phase==INT32_MIN && fabsf(s)<1e-6f && c<-.999999f);
  CHECK(gl30_foc_sincos(0.0f,&s,&c) && last_phase==0 && fabsf(s)<1e-6f && c>.999999f);
  CHECK(gl30_foc_sincos(0.5f*CORDIC_PI_F,&s,&c) && s>.999999f && fabsf(c)<1e-6f);
  float max_current=0.0f,max_duty=0.0f;
  for (unsigned i=0u;i<=65536u;++i) {
    float a=-CORDIC_PI_F+(float)i*(2.0f*CORDIC_PI_F/65536.0f);
    gl30_foc_state_t st=state_at(a);
    gl30_foc_output_t o=gl30_foc_voltage_tick(&st,0.1f,-0.06f,-0.04f,12.0f,0.00005f,0.6f,0.38f,0.12f);
    CHECK(o.valid && !o.overcurrent);
    float ia=.995832f*.1f-.028199f*(-.06f)-.014988f*(-.04f);
    float ib=.037737f*.1f+1.007723f*(-.06f)-.033757f*(-.04f);
    float ic=.009226f*.1f+.029805f*(-.06f)+1.003268f*(-.04f);
    float alpha=(2.0f*ia-ib-ic)/3.0f, beta=(ib-ic)/1.73205080756887729353f;
    float rs=sinf(a),rc=cosf(a);
    float err=fmaxf(fabsf(o.i_d_a-(rc*alpha+rs*beta)),fabsf(o.i_q_a-(-rs*alpha+rc*beta)));
    if(err>max_current) max_current=err;
    float va=rc*.38f-rs*.12f, vb=-.5f*va+.86602540378443864676f*(rs*.38f+rc*.12f);
    float vc=-.5f*va-.86602540378443864676f*(rs*.38f+rc*.12f);
    float common=-.5f*(fmaxf(va,fmaxf(vb,vc))+fminf(va,fminf(vb,vc)));
    float e=fmaxf(fabsf(o.duty_a-(.5f+(va+common)/12.f)),fmaxf(fabsf(o.duty_b-(.5f+(vb+common)/12.f)),fabsf(o.duty_c-(.5f+(vc+common)/12.f))));
    if(e>max_duty) max_duty=e;
    CHECK(err<2.0e-7f && e<2.0e-7f);
  }
  const float outside[]={4.0f,-4.0f,FLT_MAX,-FLT_MAX};
  for(unsigned i=0;i<4;++i){gl30_foc_state_t st=state_at(outside[i]);CHECK(gl30_foc_current_tick(&st,0.f,0.f,0.f,12.f,.00005f,.3f).valid);}
  float residual_current_error=0.0f;
  for(int sign=-1;sign<=1;sign+=2) {
    reset(); residual_lsb=sign*4096;
    CHECK(bench_cordic_startup_test());
    CHECK(g_cordic_diag.max_error<=CORDIC_MAX_ERROR && g_cordic_diag.max_norm_error<=CORDIC_MAX_NORM_ERROR);
    for(unsigned i=0;i<=65536u;++i) {
      float a=-CORDIC_PI_F+(float)i*(2.0f*CORDIC_PI_F/65536.0f);
      gl30_foc_state_t st=state_at(a);
      gl30_foc_output_t out=gl30_foc_voltage_tick(&st,.6f,-.36f,-.24f,12.f,.00005f,.6f,.38f,.12f);
      CHECK(out.valid && !out.overcurrent);
      const float ca=.995832f*.6f-.028199f*(-.36f)-.014988f*(-.24f);
      const float cb=.037737f*.6f+1.007723f*(-.36f)-.033757f*(-.24f);
      const float cc=.009226f*.6f+.029805f*(-.36f)+1.003268f*(-.24f);
      const float al=(2.f*ca-cb-cc)/3.f,be=(cb-cc)/1.73205080756887729353f;
      const float er=fmaxf(fabsf(out.i_d_a-(cosf(a)*al+sinf(a)*be)),fabsf(out.i_q_a-(-sinf(a)*al+cosf(a)*be)));
      CHECK(er<2.0e-6f);
      if(er>residual_current_error) residual_current_error=er;
      const float va=cosf(a)*.38f-sinf(a)*.12f;
      const float vb=-.5f*va+.86602540378443864676f*(sinf(a)*.38f+cosf(a)*.12f);
      const float vc=-.5f*va-.86602540378443864676f*(sinf(a)*.38f+cosf(a)*.12f);
      const float cm=-.5f*(fmaxf(va,fmaxf(vb,vc))+fminf(va,fminf(vb,vc)));
      CHECK(fabsf(out.duty_a-(.5f+(va+cm)/12.f))<3.0e-7f &&
            fabsf(out.duty_b-(.5f+(vb+cm)/12.f))<3.0e-7f &&
            fabsf(out.duty_c-(.5f+(vc+cm)/12.f))<3.0e-7f);
    }
  }
  reset();residual_lsb=8192;CHECK(!bench_cordic_startup_test() && !g_cordic_ready);
  reset();CHECK(bench_cordic_startup_test());
  printf("CORDIC_RESIDUAL_BOUND phase_input_peak_A=0.6 max_current_error_A=%.9g injected_q31_lsb=4096\n",(double)residual_current_error);
  ready_after=31u; CHECK(gl30_foc_sincos(.2f,&s,&c) && polls==32u);
  ready_after=32u; old=reads; CHECK(!gl30_foc_sincos(.2f,&s,&c) && polls==32u && reads==old);
  old=writes; ready_after=0; CHECK(!gl30_foc_sincos(.2f,&s,&c) && writes==old);
  gl30_foc_state_t st=state_at(.2f); st.integrator_d_v=.2f; st.integrator_q_v=.1f;
  gl30_foc_output_t o=gl30_foc_current_tick(&st,0.f,0.f,0.f,12.f,.00005f,.3f);
  CHECK(!o.valid && o.duty_a==.5f && o.duty_b==.5f && o.duty_c==.5f);
  CHECK(st.i_q_ref_a==0.f && st.integrator_d_v==0.f && st.integrator_q_v==0.f);
  reset();CHECK(bench_cordic_startup_test()); CORDIC->CSR|=0x80000000u;old=writes;
  CHECK(!gl30_foc_sincos(0.f,&s,&c) && writes==old && !g_cordic_ready);
  reset();CHECK(bench_cordic_startup_test());CORDIC->CSR^=1u;old=writes;
  CHECK(!gl30_foc_sincos(0.f,&s,&c) && writes==old && !g_cordic_ready);
  reset(); never_ready=true; CHECK(!bench_cordic_startup_test() && g_cordic_diag.samples==1u && g_cordic_diag.failures==1u);
  reset(); corrupt_result=true; CHECK(!bench_cordic_startup_test() && g_cordic_diag.failures==1u && !g_cordic_ready);
  printf("CORDIC_HOST_SIM checks=%u failed=%u current_error=%.9g duty_error=%.9g (LL transport simulated, not hardware proof)\n",checks,failed,(double)max_current,(double)max_duty);
  return failed?1:0;
}
