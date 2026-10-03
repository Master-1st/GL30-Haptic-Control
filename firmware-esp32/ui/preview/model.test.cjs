'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
require('./model.js');const M=globalThis.GL30;
const near=(a,b)=>assert.ok(Math.abs(a-b)<1e-7,a+' != '+b);
const refresh=(s,now)=>{M.tick(s,now);M.observe(s,M.position(s),now);};
const ready=()=>{const s=M.create(0);M.setMode(s,'timer');refresh(s,0);return s;};
test('zero boot cannot start and formats hours minutes seconds',()=>{
  const s=ready();assert.equal(M.primary(s),'empty');assert.equal(M.timeText(s),'00:00:00');
  M.preset(s,90);assert.equal(M.timeText(s),'01:30:00');M.preset(s,360);assert.equal(M.timeText(s),'06:00:00');
});
test('volume 33 percent aligns the simulated shaft, screen and light endpoint',()=>{
  const s=ready();M.setMode(s,'volume');M.adjust(s,-9);refresh(s,0);
  assert.equal(s.volume,33);near(M.position(s),118.8);near(s.angle,118.8);near(M.lighting(s).angle,118.8);near(M.rings(s)[0],.33);
});
test('manual clockwise and anticlockwise full turns set one hour and return zero',()=>{
  const s=ready();for(let i=0;i<8;i++)M.turn(s,45);assert.equal(M.timeText(s),'01:00:00');
  for(let i=0;i<8;i++)M.turn(s,-45);assert.equal(M.timeText(s),'00:00:00');
});
test('presets position the simulated oven knob, including multiple revolutions',()=>{
  const s=ready();M.preset(s,90);refresh(s,0);assert.equal(M.position(s),540);assert.equal(s.angle,180);
  assert.deepEqual(M.rings(s),[1,.5,0,0,0,0]);
});
test('automatic return consumes elapsed time exactly once despite encoder feedback',()=>{
  const s=ready();M.preset(s,1);M.primary(s);
  for(let i=1;i<=100;i++)refresh(s,i*100);
  assert.equal(s.remaining,50000);assert.equal(M.position(s),5);assert.equal(s.angle,5);
});
test('read-only feedback cannot turn servo motion or missed turns into an edit',()=>{
  const s=ready();M.preset(s,25);
  M.observe(s,359,0);M.observe(s,1,10);M.observe(s,180,20);M.observe(s,7200,30);
  assert.equal(s.duration,25*M.MINUTE);assert.equal(M.timeText(s),'00:25:00');
});
test('reverse during running reduces remaining time and the next tick preserves that edit',()=>{
  const s=ready();M.preset(s,25);M.primary(s);refresh(s,1000);M.turn(s,-60);
  assert.equal(s.phase,'running');assert.equal(s.remaining,14*M.MINUTE+59000);
  refresh(s,2000);assert.equal(s.remaining,14*M.MINUTE+58000);
});
test('clockwise running adjustment adds time without restarting or losing elapsed seconds',()=>{
  const s=ready();M.preset(s,1);M.primary(s);refresh(s,1500);M.turn(s,60);refresh(s,2500);
  assert.equal(s.phase,'running');assert.equal(s.remaining,11*M.MINUTE-2500);
});
test('pause freezes auto return but allows manual adjustment and resumes from new value',()=>{
  const s=ready();M.preset(s,25);M.primary(s);M.primary(s);refresh(s,1000);M.turn(s,-30);
  assert.equal(M.timeText(s),'00:20:00');assert.equal(s.phase,'paused');refresh(s,5000);
  assert.equal(M.primary(s),'resume');refresh(s,6000);assert.equal(M.timeText(s),'00:19:59');
});
test('manual reverse to zero completes, cannot go negative, and new rotation does not auto-start',()=>{
  const s=ready();M.preset(s,1);M.primary(s);M.turn(s,-12);
  assert.equal(s.phase,'done');assert.equal(s.remaining,0);assert.equal(s.endstop,'lower');assert.ok(s.stopIntent>0);
  M.turn(s,6);assert.equal(s.phase,'setting');assert.equal(s.duration,M.MINUTE);assert.equal(s.endstop,null);
});
test('zero endstop has no hidden negative debt and opposing intent clears immediately',()=>{
  const s=ready();for(let i=0;i<100;i++)M.turn(s,-360);
  assert.equal(M.position(s),0);assert.equal(s.endstop,'lower');assert.ok(s.stopIntent>0);
  M.turn(s,6);assert.equal(M.value(s),1);assert.equal(s.stopIntent,0);assert.equal(s.endstop,null);
});
test('volume endstops clamp both ends and release immediately when reversing',()=>{
  const s=ready();M.setMode(s,'volume');M.turn(s,-360);assert.equal(s.volume,0);assert.ok(s.stopIntent>0);
  M.turn(s,3.6);assert.equal(s.volume,1);assert.equal(s.stopIntent,0);
  M.turn(s,720);assert.equal(s.volume,100);assert.equal(s.endstop,'upper');assert.ok(s.stopIntent<0);
  M.turn(s,-3.6);assert.equal(s.volume,99);assert.equal(s.stopIntent,0);
});
test('time upper boundary is six hours and attempted overtravel does not accumulate',()=>{
  const s=ready();M.turn(s,9999);assert.equal(M.value(s),360);assert.equal(s.endstop,'upper');
  M.turn(s,-6);assert.equal(M.value(s),359);
});
test('endstop feedback is transient and fault clears all stop intent',()=>{
  const s=ready();M.turn(s,-30);refresh(s,449);assert.equal(s.endstop,'lower');
  refresh(s,450);assert.equal(s.endstop,null);M.turn(s,-30);M.setFault(s,true);assert.equal(s.stopIntent,0);
});
test('arrival exactly at zero cues the stop before further overtravel, including automatic completion',()=>{
  const s=ready();M.setMode(s,'volume');M.adjust(s,-42);assert.equal(s.volume,0);assert.equal(s.endstop,'lower');assert.ok(s.stopIntent>0);
  M.setMode(s,'timer');M.preset(s,1);M.primary(s);refresh(s,60000);
  assert.equal(s.phase,'done');assert.equal(s.endstop,'lower');assert.ok(s.stopIntent>0);
  refresh(s,60450);assert.equal(s.stopIntent,0);assert.equal(s.phase,'done');
});
test('whole laps keep a full colored trail with blue head at zero, never an empty next lap',()=>{
  const s=ready();for(let i=1;i<=6;i++){
    M.preset(s,i*60);const light=M.lighting(s);
    assert.equal(light.index,i-1);assert.equal(light.fraction,1);assert.equal(light.angle,0);assert.equal(light.head,'#55c9ff');
  }
});
test('different hour layers use distinct colors and outer current lap matches the screen layer',()=>{
  const s=ready();assert.equal(new Set(M.COLORS).size,6);
  M.preset(s,150);const light=M.lighting(s);assert.equal(light.color,M.COLORS[2]);
  assert.equal(light.previous,M.COLORS[1]);assert.equal(light.fraction,.5);assert.deepEqual(M.rings(s),[1,1,.5,0,0,0]);
});
test('mute preserves mechanical position and progress, and adjusting unmutes',()=>{
  const s=ready();M.setMode(s,'volume');const angle=M.position(s);M.primary(s);
  assert.equal(s.muted,true);assert.equal(M.position(s),angle);M.turn(s,3.6);assert.equal(s.muted,false);
});
test('background countdown advances logical time but cannot drive the foreground volume shaft',()=>{
  const s=ready();M.preset(s,1);M.primary(s);M.setMode(s,'volume');M.adjust(s,-9);refresh(s,1000);
  assert.equal(s.remaining,59000);assert.equal(s.volume,33);near(s.angle,118.8);near(M.position(s),118.8);
  M.setMode(s,'timer');refresh(s,1000);near(s.angle,5.9);assert.equal(s.remaining,59000);
});
test('near-complete reverse revolutions clamp to exact zero without a phantom new lap',()=>{
  for(const error of [-.000001,0,.000001]){
    const s=ready();M.preset(s,60);M.turn(s,-360+error);const light=M.lighting(s);
    assert.equal(M.value(s),0);assert.equal(light.fraction,0);assert.equal(light.index,0);assert.equal(light.angle,0);
  }
});
test('off hides strip, timer and return continue, first press only wakes',()=>{
  const s=ready();M.preset(s,1);M.primary(s);s.off=true;refresh(s,1000);
  assert.equal(M.lighting(s).visible,false);near(M.position(s),5.9);
  M.turn(s,-6);assert.equal(s.remaining,59000);assert.equal(M.primary(s),'wake');assert.equal(s.phase,'running');
});
test('disconnect hides position while local clock continues and reconnection never adds inferred turns',()=>{
  const s=ready();M.preset(s,1);M.primary(s);M.setConnected(s,false);M.tick(s,1000);
  assert.equal(s.remaining,59000);assert.equal(M.lighting(s).visible,false);assert.equal(M.primary(s),'pause');
  M.setConnected(s,true);M.observe(s,340,1000);assert.equal(s.remaining,59000);assert.equal(s.phase,'paused');
});
test('invalid stale and backward samples cannot establish a usable position',()=>{
  const s=ready();M.tick(s,251);assert.equal(s.angle,null);
  M.observe(s,NaN,252);assert.equal(s.angle,null);M.observe(s,20,253);M.observe(s,60,252);
  assert.equal(s.angle,20);M.observe(s,Infinity,254);assert.equal(s.angle,null);
});
test('fault pauses and blocks changes, clearing it requires deliberate resume',()=>{
  const s=ready();M.preset(s,1);M.primary(s);refresh(s,1000);M.setFault(s,true);
  M.turn(s,60);assert.equal(s.remaining,59000);assert.equal(M.primary(s),'blocked');
  M.setFault(s,false);refresh(s,5000);assert.equal(s.phase,'paused');assert.equal(M.primary(s),'resume');
});
test('background timer can pause in volume and completion takes over without waking off display',()=>{
  const s=ready();M.preset(s,1);M.primary(s);M.setMode(s,'volume');M.timerPrimary(s);
  assert.equal(s.phase,'paused');assert.equal(s.mode,'volume');M.timerPrimary(s);s.off=true;M.tick(s,60000);
  assert.equal(s.phase,'done');assert.equal(s.mode,'timer');assert.equal(s.off,true);assert.equal(M.primary(s),'wake');assert.equal(s.phase,'done');
});
test('edits before deadline extend countdown; ticking after deadline cannot auto-restart it',()=>{
  const s=ready();M.preset(s,1);M.primary(s);refresh(s,59999);M.turn(s,6);refresh(s,60001);
  assert.equal(s.phase,'running');assert.equal(s.remaining,59999);
  refresh(s,120000);assert.equal(s.phase,'done');assert.equal(s.remaining,0);M.turn(s,6);assert.equal(s.phase,'setting');
});
test('12 second demo auto-returns to zero and restores 1x clock for the next run',()=>{
  const s=ready();M.demo(s);refresh(s,6000);assert.equal(M.position(s),270);
  refresh(s,12000);assert.equal(M.position(s),0);assert.equal(s.phase,'done');assert.equal(s.rate,1);
  M.reset(s);M.preset(s,1);M.primary(s);refresh(s,13000);assert.equal(s.remaining,59000);
});
test('demo cannot overwrite an ordinary running or paused timer',()=>{
  const s=ready();M.preset(s,25);M.primary(s);M.demo(s);assert.equal(s.duration,25*M.MINUTE);
  M.primary(s);M.demo(s);assert.equal(s.phase,'paused');assert.equal(s.demo,false);
});
test('boot home opens menu first, then confirms the selected application',()=>{
  const s=M.create(0);refresh(s,0);assert.equal(s.page,'home');assert.equal(M.primary(s),'menu');
  assert.equal(s.page,'menu');assert.equal(s.menuIndex,0);M.turn(s,M.MENU_STEP);
  assert.equal(s.menuIndex,1);assert.equal(s.mode,'timer');assert.equal(s.duration,0);assert.equal(s.volume,42);
  assert.equal(M.primary(s),'open');assert.equal(s.page,'app');assert.equal(s.mode,'volume');assert.equal(s.muted,false);
});
test('home rotation is inert and menu preserves continuous travel through the seam in either direction',()=>{
  const s=M.create(0);refresh(s,0);M.turn(s,720);M.adjust(s,30);assert.equal(s.duration,0);assert.equal(s.menuIndex,0);
  M.primary(s);M.turn(s,M.MENU_STEP/3);assert.equal(s.menuIndex,0);near(M.position(s),M.MENU_STEP/3);M.turn(s,M.MENU_STEP*2/3);assert.equal(s.menuIndex,1);
  M.turn(s,360*12);assert.equal(s.menuIndex,1);near(s.menuPosition,1+M.APPS.length*12);
  M.turn(s,-360*12-M.MENU_STEP*2);assert.equal(s.menuIndex,M.APPS.length-1);near(s.menuPosition,-1);
  M.turn(s,M.MENU_STEP*2);assert.equal(s.menuIndex,1);near(s.menuPosition,1);
  M.turn(s,NaN);M.selectMenu(s,NaN);assert.equal(s.menuIndex,1);
});
test('back preserves running timer and settings through app menu home and reentry',()=>{
  const s=ready();M.preset(s,25);M.primary(s);refresh(s,1000);const before=s.remaining;
  M.back(s);assert.equal(s.page,'menu');assert.equal(s.phase,'running');assert.equal(s.remaining,before);
  M.back(s);assert.equal(s.page,'home');refresh(s,2000);assert.equal(s.remaining,before-1000);assert.equal(M.position(s),0);
  M.primary(s);M.primary(s);assert.equal(s.page,'app');assert.equal(s.phase,'running');assert.equal(s.remaining,before-1000);
});
test('menu selection is separate from application mode and background timer shaft',()=>{
  const s=ready();M.preset(s,1);M.primary(s);M.back(s);M.selectMenu(s,1);refresh(s,1000);
  assert.equal(s.mode,'timer');assert.equal(s.menuIndex,1);assert.equal(M.position(s),M.MENU_STEP);assert.equal(s.remaining,59000);
  assert.deepEqual(M.rings(s),[0,0,0,0,0,0]);M.primary(s);M.back(s);assert.equal(s.menuIndex,1);
});
test('background completion returns from either navigation page without waking a dark screen',()=>{
  for(const page of ['home','menu'])for(const off of [false,true]){
    const s=ready();M.preset(s,1);M.primary(s);M.back(s);if(page==='home')M.back(s);s.off=off;
    M.tick(s,60000);assert.equal(s.page,'app');assert.equal(s.mode,'timer');assert.equal(s.phase,'done');assert.equal(s.off,off);
  }
});
test('first single or back gesture while off only wakes and retains navigation context',()=>{
  for(const page of ['home','menu','app'])for(const fn of [M.primary,M.back]){
    const s=ready();s.page=page;s.off=true;assert.equal(fn(s),'wake');assert.equal(s.page,page);assert.equal(s.off,false);
  }
});
test('fault blocks application edits but not return, menu selection or navigation',()=>{
  const s=ready();M.preset(s,1);M.primary(s);M.setFault(s,true);M.back(s);M.selectMenu(s,1);M.primary(s);
  assert.equal(s.mode,'volume');assert.equal(s.page,'app');assert.equal(s.phase,'paused');assert.equal(M.primary(s),'blocked');
  M.turn(s,90);assert.equal(s.volume,42);M.home(s);assert.equal(s.page,'home');
});
test('all nine applications open from a cyclic selection without performing their primary action',()=>{
  const s=ready();assert.equal(M.APPS.length,9);assert.equal(new Set(M.APPS.map(a=>a.id)).size,9);
  for(const [i,app] of M.APPS.entries()){
    M.back(s);M.selectMenu(s,i);assert.equal(M.primary(s),'open');assert.equal(s.mode,app.id);
    assert.equal(s.page,'app');assert.equal(s.phase,'setting');assert.equal(s.stopwatchRunning,false);assert.equal(s.muted,false);
  }
});
test('direct selection takes the short seam path and preserves multiple winding counts',()=>{
  const s=ready(),n=M.APPS.length;M.back(s);M.turn(s,360*5);M.selectMenu(s,n-1);
  near(s.menuPosition,5*n-1);M.selectMenu(s,0);near(s.menuPosition,5*n);
  M.turn(s,9);M.selectMenu(s,1);near(s.menuPosition,5*n+1);
  M.selectMenu(s,-1);assert.equal(s.menuIndex,n-1);near(s.menuPosition,5*n-1);
  M.selectMenu(s,2*n);assert.equal(s.menuIndex,0);near(s.menuPosition,5*n);
});
test('menu always has only a blue pointer even after reverse travel and multiple full laps',()=>{
  const s=ready();M.preset(s,150);M.back(s);
  for(const degrees of [-45,-360,360*9,45,22.5]){
    M.turn(s,degrees);refresh(s,0);const light=M.lighting(s);
    assert.equal(light.visible,true);assert.equal(light.pointerOnly,true);assert.equal(light.head,M.BLUE);
    assert.equal(light.fraction,0);assert.equal(light.previous,null);near(light.angle,M.wrap(M.position(s)));
    assert.deepEqual(M.rings(s),[0,0,0,0,0,0]);assert.equal(s.duration,150*M.MINUTE);
  }
});
test('utility turns and informational single presses cannot alter hidden timer or volume',()=>{
  const s=ready();M.preset(s,25);M.primary(s);M.setMode(s,'volume');M.primary(s);
  for(const app of M.APPS.slice(2)){
    M.setMode(s,app.id);M.turn(s,-720);M.adjust(s,99);
    if(!['stopwatch','lighting'].includes(app.id))assert.equal(M.primary(s),'info');
    assert.equal(s.remaining,25*M.MINUTE);assert.equal(s.phase,'running');assert.equal(s.volume,42);assert.equal(s.muted,true);
    assert.deepEqual(M.rings(s),[0,0,0,0,0,0]);
  }
});
test('stopwatch supports start pause resume and reset with hours minutes seconds and hundredths',()=>{
  const s=ready();M.setMode(s,'stopwatch');assert.equal(M.stopwatchText(s),'00:00:00.00');
  assert.equal(M.primary(s),'start');refresh(s,3723456);assert.equal(M.stopwatchText(s),'01:02:03.45');
  assert.equal(M.primary(s),'pause');refresh(s,3724456);assert.equal(s.stopwatchElapsed,3723456);
  assert.equal(M.primary(s),'resume');refresh(s,3724500);assert.equal(M.stopwatchText(s),'01:02:03.50');
  M.resetStopwatch(s);assert.equal(s.stopwatchRunning,false);assert.equal(M.stopwatchText(s),'00:00:00.00');
});
test('stopwatch continues across navigation and off while first wake cannot toggle it',()=>{
  const s=ready();M.setMode(s,'stopwatch');M.primary(s);M.back(s);refresh(s,1000);M.back(s);refresh(s,2000);
  s.off=true;refresh(s,3000);assert.equal(s.stopwatchElapsed,3000);assert.equal(M.primary(s),'wake');
  assert.equal(s.stopwatchRunning,true);M.primary(s);M.primary(s);assert.equal(s.mode,'stopwatch');assert.equal(s.stopwatchRunning,true);
});
test('stopwatch is independent of timer resets demo rate and completion takeover',()=>{
  const s=ready();M.setMode(s,'stopwatch');M.primary(s);M.demo(s);M.setMode(s,'stopwatch');refresh(s,6000);
  assert.equal(s.stopwatchElapsed,6000);assert.equal(s.remaining,45*M.MINUTE);
  M.reset(s);refresh(s,7000);assert.equal(s.stopwatchElapsed,7000);M.demo(s);M.setMode(s,'stopwatch');refresh(s,19000);
  assert.equal(s.phase,'done');assert.equal(s.mode,'timer');assert.equal(s.stopwatchElapsed,19000);assert.equal(s.stopwatchRunning,true);
  M.resetStopwatch(s);assert.equal(s.phase,'done');assert.equal(s.remaining,0);
});
test('fault pauses stopwatch and recovery requires an explicit healthy start',()=>{
  const s=ready();M.setMode(s,'stopwatch');M.primary(s);refresh(s,1234);M.setFault(s,true);refresh(s,5000);
  assert.equal(s.stopwatchElapsed,1234);assert.equal(s.stopwatchRunning,false);assert.equal(M.primary(s),'blocked');
  M.setFault(s,false);refresh(s,6000);assert.equal(s.stopwatchRunning,false);assert.equal(M.primary(s),'resume');
  refresh(s,7000);assert.equal(s.stopwatchElapsed,2234);
});
test('missing angle leaves local stopwatch running and permits pause but blocks restart',()=>{
  const s=ready();M.setMode(s,'stopwatch');M.primary(s);M.setConnected(s,false);M.tick(s,1200);
  assert.equal(s.stopwatchElapsed,1200);assert.equal(M.lighting(s).visible,false);assert.equal(M.primary(s),'pause');
  assert.equal(M.primary(s),'blocked');M.tick(s,3000);assert.equal(s.stopwatchElapsed,1200);
});
test('lighting opens as a field selector and single enters and confirms an editor',()=>{
  const s=ready();M.back(s);M.selectMenu(s,8);M.primary(s);assert.equal(s.mode,'lighting');assert.equal(s.lightEditing,false);
  M.turn(s,-30);assert.equal(s.lightField,2);M.turn(s,30);assert.equal(s.lightField,0);
  assert.equal(M.primary(s),'edit');M.turn(s,15);assert.equal(s.lightEffect,0);M.turn(s,15);assert.equal(s.lightEffect,1);
  assert.equal(M.primary(s),'confirm');assert.equal(s.lightEditing,false);assert.equal(M.lightFieldText(s),'常亮');
});
test('lighting back exits editing before returning to the nine-item menu and retains values',()=>{
  const s=ready();M.setMode(s,'lighting');M.primary(s);M.turn(s,-30);assert.equal(s.lightEffect,3);
  assert.equal(M.back(s),'fields');assert.equal(s.page,'app');assert.equal(s.lightEditing,false);
  M.back(s);assert.equal(s.page,'menu');assert.equal(s.menuIndex,8);assert.equal(s.lightEffect,3);
  M.primary(s);assert.equal(s.mode,'lighting');assert.equal(s.lightEditing,false);assert.equal(s.lightEffect,3);
});
test('lighting color wraps and brightness clamps without negative adjustment debt',()=>{
  const s=ready();M.setMode(s,'lighting');M.selectLightField(s,1);M.primary(s);M.turn(s,-30);
  assert.equal(s.lightColor,5);M.turn(s,30);assert.equal(s.lightColor,0);
  M.selectLightField(s,2);M.primary(s);M.turn(s,-3000);assert.equal(s.lightBrightness,0);assert.equal(M.lighting(s).visible,false);assert.equal(s.off,false);
  M.turn(s,30);assert.equal(s.lightBrightness,5);M.turn(s,3000);assert.equal(s.lightBrightness,100);M.turn(s,-30);assert.equal(s.lightBrightness,95);
});
test('ambient steady breathing and flow are continuous with a blue head and reduced motion freezes them',()=>{
  const s=ready();M.setMode(s,'lighting');M.setLighting(s,1,3);M.setLighting(s,0,1);
  let light=M.lighting(s);assert.equal(light.color,M.COLORS[3]);assert.equal(light.fraction,1);assert.equal(light.intensity,1);assert.equal(light.head,M.BLUE);
  M.setLighting(s,0,2);refresh(s,0);near(M.lighting(s).intensity,.25);refresh(s,2000);near(M.lighting(s).intensity,1);
  refresh(s,4000);near(M.lighting(s).intensity,.25);near(M.lighting(s,true).intensity,1);
  M.setLighting(s,0,3);refresh(s,4500);light=M.lighting(s);near(light.angle,150);near(light.start,60);assert.equal(light.fraction,.25);assert.equal(light.head,M.BLUE);
  refresh(s,4800);near(M.lighting(s).angle,160);near(M.lighting(s,true).angle,0);
});
test('ambient settings persist on home but menu stays a single blue pointer and core feedback retains its colors',()=>{
  const s=ready();M.setMode(s,'lighting');M.setLighting(s,0,3);M.setLighting(s,1,5);M.setLighting(s,2,40);M.home(s);
  assert.equal(M.lighting(s).effect,'flow');assert.equal(M.lighting(s).color,M.COLORS[5]);near(M.lighting(s).brightness,.4);
  M.primary(s);const menu=M.lighting(s);assert.equal(menu.pointerOnly,true);assert.equal(menu.fraction,0);assert.equal(menu.previous,null);assert.equal(menu.head,M.BLUE);near(menu.brightness,.4);
  M.setMode(s,'timer');M.preset(s,90);const timer=M.lighting(s);assert.equal(timer.effect,'follow');assert.equal(timer.color,M.COLORS[1]);assert.equal(timer.fraction,.5);near(timer.brightness,.4);
  M.setMode(s,'volume');M.adjust(s,-9);near(M.lighting(s).angle,118.8);assert.equal(M.lighting(s).color,M.COLORS[0]);
});
test('lighting adjustments cannot alter simultaneous timer or stopwatch and completion closes its editor',()=>{
  const s=ready();M.preset(s,1);M.primary(s);M.setMode(s,'stopwatch');M.primary(s);M.setMode(s,'lighting');M.primary(s);M.turn(s,60);
  refresh(s,1000);assert.equal(s.phase,'running');assert.equal(s.remaining,59000);assert.equal(s.stopwatchElapsed,1000);assert.equal(s.lightEffect,2);
  refresh(s,60000);assert.equal(s.mode,'timer');assert.equal(s.phase,'done');assert.equal(s.lightEditing,false);assert.equal(s.lightEffect,2);assert.equal(s.stopwatchElapsed,60000);
  M.setLighting(s,0,1);assert.equal(s.lightEffect,2);
});
test('lighting controls reject invalid contexts inputs and faults while an off press only wakes',()=>{
  const s=ready();M.setLighting(s,0,2);assert.equal(s.lightEffect,0);M.setMode(s,'lighting');
  M.setLighting(s,0,NaN);M.setLighting(s,9,2);M.selectLightField(s,9);assert.equal(s.lightEffect,0);assert.equal(s.lightField,0);
  M.setFault(s,true);M.setLighting(s,2,10);assert.equal(s.lightBrightness,100);assert.equal(M.primary(s),'blocked');
  M.setFault(s,false);refresh(s,100);s.off=true;M.setLighting(s,0,3);M.turn(s,30);assert.equal(s.lightEffect,0);assert.equal(M.primary(s),'wake');assert.equal(s.lightEditing,false);
});
