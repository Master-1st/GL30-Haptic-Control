'use strict';

// Browser model: simulated motor target and read-only angle feedback.
// An encoder observation is never inferred to be a manual turn or force.
globalThis.GL30 = (() => {
  const MINUTE=60000, MAX_MINUTES=360, SAMPLE_TTL=250;
  const COLORS=['#ff9b51','#ffd166','#91db9f','#c29bff','#f390b6','#e5e99a'];
  const COLOR_NAMES=['橙色','金色','绿色','紫色','玫红','浅柠黄'], BLUE='#55c9ff';
  const clamp=(n,lo,hi)=>Math.min(hi,Math.max(lo,n)), wrap=n=>((n%360)+360)%360;
  const APPS=[
    {id:'timer',label:'计时器',icon:'timer',color:'#ffad68'},
    {id:'volume',label:'音量',icon:'volume',color:'#83dce8'},
    {id:'stopwatch',label:'秒表',icon:'stopwatch',color:'#e1e986'},
    {id:'alarm',label:'闹钟',icon:'alarm',color:'#f497aa'},
    {id:'weather',label:'天气',icon:'weather',color:'#ffd47c'},
    {id:'haptics',label:'手感',icon:'haptics',color:'#b5a1ff'},
    {id:'settings',label:'设置',icon:'settings',color:'#b9c9d9'},
    {id:'calendar',label:'日历',icon:'calendar',color:'#91dcab'},
    {id:'lighting',label:'灯效调整',icon:'lighting',color:'#ffc77d'}
  ];
  const MENU_STEP=360/APPS.length,LIGHT_STEP=30,cycle=(n,count=APPS.length)=>((n%count)+count)%count;
  const LIGHT_EFFECTS=[{id:'follow',label:'随功能'},{id:'steady',label:'常亮'},{id:'breathe',label:'呼吸'},{id:'flow',label:'流动'}];
  const LIGHT_FIELDS=['效果','颜色','亮度'];
  const appIndex=s=>APPS.findIndex(app=>app.id===s.mode);
  const adjustable=s=>s.mode==='timer'||s.mode==='volume';
  function create(now=0) {
    return {page:'home',menuIndex:0,menuPosition:0,mode:'timer',phase:'setting',duration:0,remaining:0,volume:42,muted:false,
      stopwatchElapsed:0,stopwatchRunning:false,
      lightEffect:0,lightColor:0,lightBrightness:100,lightField:0,lightEditing:false,lightTravel:0,
      off:false,fault:false,connected:true,angle:null,lastSample:null,last:now,
      rate:1,demo:false,endstop:null,stopIntent:0,stopUntil:0};
  }
  const timerValue=s=>s.phase==='setting'?s.duration:s.remaining;
  const value=s=>s.mode==='timer'?timerValue(s)/MINUTE:s.mode==='volume'?s.volume:0;
  const position=s=>s.page==='home'?0:s.page==='menu'?s.menuPosition*MENU_STEP:
    s.mode==='lighting'?(s.lightEditing?[s.lightEffect*90,s.lightColor*60,s.lightBrightness*3.6][s.lightField]:s.lightField*120):
    adjustable(s)?value(s)*(s.mode==='timer'?6:3.6):appIndex(s)*MENU_STEP;
  const healthy=s=>s.connected&&!s.fault&&s.angle!==null;
  function complete(s){
    s.remaining=0;s.phase='done';s.rate=1;s.mode='timer';s.page='app';s.menuIndex=0;
    s.lightEditing=false;s.lightTravel=0;
    s.stopIntent=Math.max(.15,s.endstop==='lower'?s.stopIntent:0);
    s.endstop='lower';s.stopUntil=s.last+450;
  }
  function tick(s,now) {
    if(!Number.isFinite(now)||now<s.last)return;
    const elapsed=now-s.last;s.last=now;
    if(s.stopwatchRunning)s.stopwatchElapsed+=elapsed;
    if(s.phase==='running'){
      s.remaining=Math.max(0,s.remaining-elapsed*s.rate);
      if(s.remaining<1e-6)complete(s);
    }
    if(s.lastSample!==null&&now-s.lastSample>SAMPLE_TTL)s.angle=null;
    if(now>=s.stopUntil){s.endstop=null;s.stopIntent=0;}
  }
  function stop(s,requested,accepted) {
    const overflow=requested-accepted;
    if(Math.abs(overflow)<1e-9){
      const maximum=s.mode==='timer'?MAX_MINUTES:100;
      const contact=accepted===0&&value(s)>0?'lower':accepted===maximum&&value(s)<maximum?'upper':null;
      s.endstop=contact;s.stopIntent=contact?(contact==='lower'?.15:-.15):0;
      if(contact)s.stopUntil=s.last+450;return;
    }
    s.endstop=overflow<0?'lower':'upper';
    // Normalized opposing intent, deliberately not amperes or newton-metres.
    s.stopIntent=clamp(-overflow/5,-1,1);s.stopUntil=s.last+450;
  }
  function adjust(s,steps) {
    if(!Number.isFinite(steps)||!steps||!healthy(s)||s.off)return;
    if(s.page==='home')return;
    if(s.page==='menu'){selectMenu(s,s.menuIndex+Math.sign(steps));return;}
    if(s.mode==='lighting'){adjustLighting(s,steps);return;}
    if(!adjustable(s))return;
    const before=value(s),maximum=s.mode==='timer'?MAX_MINUTES:100;
    const requested=before+steps,accepted=clamp(requested,0,maximum);
    stop(s,requested,accepted);
    if(s.mode==='volume'){s.volume=accepted;s.muted=false;return;}
    if(s.phase==='running'||s.phase==='paused'){
      s.remaining=Math.round(accepted*MINUTE);s.duration=Math.max(s.duration,s.remaining);
      if(s.remaining===0)complete(s);
    }else if(accepted!==before){
      s.duration=s.remaining=Math.round(accepted*MINUTE);s.phase='setting';s.demo=false;s.rate=1;
    }
  }
  function turn(s,degrees){
    if(!Number.isFinite(degrees)||!healthy(s)||s.off||s.page==='home')return;
    if(s.page==='menu'){
      s.menuPosition+=degrees/MENU_STEP;
      s.menuIndex=cycle(Math.round(s.menuPosition));
      return;
    }
    if(s.mode==='lighting'){
      s.lightTravel+=degrees;
      const steps=Math.trunc(s.lightTravel/LIGHT_STEP);
      if(steps){adjustLighting(s,steps);s.lightTravel%=LIGHT_STEP;}
      return;
    }
    if(!adjustable(s))return;
    adjust(s,degrees/(s.mode==='timer'?6:3.6));
  }
  function selectMenu(s,index){
    if(s.off||s.page!=='menu'||!Number.isFinite(index))return;
    s.menuIndex=cycle(Math.round(index));
    // Keep the winding count. Selecting across the seam takes the nearest route.
    s.menuPosition+=cycle(s.menuIndex-cycle(s.menuPosition)+APPS.length/2)-APPS.length/2;
  }
  function home(s){
    if(s.off){s.off=false;return 'wake';}
    s.page='home';s.lightEditing=false;s.lightTravel=0;s.endstop=null;s.stopIntent=0;return 'home';
  }
  function back(s){
    if(s.off){s.off=false;return 'wake';}
    if(s.page==='app'&&s.mode==='lighting'&&s.lightEditing){s.lightEditing=false;s.lightTravel=0;return 'fields';}
    if(s.page==='app'){s.page='menu';selectMenu(s,appIndex(s));}
    else s.page='home';
    s.endstop=null;s.stopIntent=0;return 'back';
  }
  function observe(s,degrees,now,valid=true) {
    if(!Number.isFinite(now)||now<s.last)return;
    tick(s,now);
    if(!valid||!Number.isFinite(degrees)||!s.connected||s.fault){
      s.angle=null;s.lastSample=null;return;
    }
    s.angle=wrap(degrees);s.lastSample=now;
    // Servo feedback cannot be counted a second time as user input.
  }
  function primary(s) {
    if(s.off){s.off=false;return 'wake';}
    if(s.page==='home'){s.page='menu';selectMenu(s,appIndex(s));return 'menu';}
    if(s.page==='menu'){setMode(s,APPS[s.menuIndex].id);return 'open';}
    if(s.mode==='volume'){
      if(!healthy(s))return 'blocked';
      s.muted=!s.muted;return 'mute';
    }
    if(s.mode==='timer')return timerPrimary(s);
    if(s.mode==='lighting'){
      if(!s.lightEditing&&!healthy(s))return 'blocked';
      s.lightEditing=!s.lightEditing;s.lightTravel=0;return s.lightEditing?'edit':'confirm';
    }
    if(s.mode==='stopwatch'){
      if(s.stopwatchRunning){s.stopwatchRunning=false;return 'pause';}
      if(!healthy(s))return 'blocked';
      s.stopwatchRunning=true;return s.stopwatchElapsed?'resume':'start';
    }
    return 'info';
  }
  function timerPrimary(s) {
    if(s.off){s.off=false;return 'wake';}
    if(s.phase==='running'){s.phase='paused';return 'pause';}
    if(!healthy(s))return 'blocked';
    if(s.phase==='done'){reset(s);return 'reset';}
    if(s.phase==='paused'&&s.remaining>0){s.phase='running';return 'resume';}
    if(s.duration>0){s.remaining=s.duration;s.phase='running';return 'start';}
    return 'empty';
  }
  function reset(s) {
    s.phase='setting';s.duration=s.remaining=0;s.rate=1;s.demo=false;
    s.endstop=null;s.stopIntent=0;
  }
  function resetStopwatch(s){s.stopwatchRunning=false;s.stopwatchElapsed=0;}
  function selectLightField(s,index){
    if(s.off||s.page!=='app'||s.mode!=='lighting'||!Number.isInteger(index)||index<0||index>=LIGHT_FIELDS.length)return;
    s.lightField=index;s.lightEditing=false;s.lightTravel=0;
  }
  function setLighting(s,field,value){
    if(s.off||!healthy(s)||s.page!=='app'||s.mode!=='lighting'||!Number.isFinite(value))return;
    if(field===0)s.lightEffect=clamp(Math.round(value),0,LIGHT_EFFECTS.length-1);
    else if(field===1)s.lightColor=clamp(Math.round(value),0,COLORS.length-1);
    else if(field===2)s.lightBrightness=clamp(Math.round(value/5)*5,0,100);
  }
  function adjustLighting(s,steps){
    if(!s.lightEditing){s.lightField=cycle(s.lightField+Math.trunc(steps),LIGHT_FIELDS.length);return;}
    if(s.lightField===0)setLighting(s,0,cycle(s.lightEffect+Math.trunc(steps),LIGHT_EFFECTS.length));
    else if(s.lightField===1)setLighting(s,1,cycle(s.lightColor+Math.trunc(steps),COLORS.length));
    else setLighting(s,2,s.lightBrightness+steps*5);
  }
  const lightFieldText=s=>[LIGHT_EFFECTS[s.lightEffect].label,COLOR_NAMES[s.lightColor],s.lightBrightness+'%'][s.lightField];
  function setMode(s,mode) {
    if(s.off){s.off=false;return;}
    if(!APPS.some(app=>app.id===mode))return;
    s.mode=mode;s.page='app';s.menuIndex=appIndex(s);s.lightEditing=false;s.lightTravel=0;s.endstop=null;s.stopIntent=0;
  }
  function preset(s,minutes) {
    if(!Number.isFinite(minutes)||!healthy(s)||s.off)return;
    s.mode='timer';s.page='app';s.menuIndex=0;s.lightEditing=false;s.lightTravel=0;
    if(s.phase==='running'||s.phase==='paused')adjust(s,minutes-value(s));
    else{
      s.duration=s.remaining=Math.round(clamp(minutes,0,MAX_MINUTES)*MINUTE);s.phase='setting';
      s.demo=false;s.rate=1;s.endstop=null;s.stopIntent=0;
    }
  }
  function setConnected(s,connected) {
    s.connected=Boolean(connected);s.angle=null;s.lastSample=null;s.endstop=null;s.stopIntent=0;
  }
  function setFault(s,fault) {
    s.fault=Boolean(fault);s.angle=null;s.lastSample=null;s.endstop=null;s.stopIntent=0;
    if(s.fault&&s.phase==='running')s.phase='paused';
    if(s.fault)s.stopwatchRunning=false;
  }
  function demo(s) {
    if(!healthy(s)||s.phase==='running'||s.phase==='paused')return;
    s.off=false;s.mode='timer';s.page='app';s.menuIndex=0;s.phase='running';s.duration=s.remaining=90*MINUTE;
    s.lightEditing=false;s.lightTravel=0;
    s.rate=450;s.demo=true;s.endstop=null;s.stopIntent=0;
  }
  function lighting(s,reduced=false) {
    const base={visible:!s.off&&healthy(s)&&s.lightBrightness>0,brightness:s.lightBrightness/100,effect:'follow',intensity:1,start:0};
    // Ambient effects apply on the home and utility screens. Menu and value feedback retain their meaning.
    if(s.page!=='menu'&&(s.page!=='app'||!adjustable(s))&&s.lightEffect!==0){
      const effect=LIGHT_EFFECTS[s.lightEffect].id,flow=effect==='flow',angle=flow&&!reduced?wrap(s.last/30):0;
      return {...base,effect,angle,turns:0,index:s.lightColor,fraction:flow?.25:1,start:flow?angle-90:0,
        color:COLORS[s.lightColor],colorName:COLOR_NAMES[s.lightColor],previous:null,head:BLUE,
        intensity:effect==='breathe'&&!reduced?.25+.75*(1-Math.cos(s.last*2*Math.PI/4000))/2:1};
    }
    if(s.page!=='app'||!adjustable(s))return {...base,angle:wrap(position(s)),
      turns:position(s)/360,index:0,fraction:0,color:BLUE,colorName:'蓝色指针',previous:null,head:BLUE,pointerOnly:true};
    const turns=position(s)/360,index=clamp(Math.ceil(turns)-1,0,5);
    return {...base,turns,angle:wrap(position(s)),index,
      fraction:clamp(turns-index,0,1),color:COLORS[index],colorName:COLOR_NAMES[index],
      previous:index>0?COLORS[index-1]:null,head:BLUE};
  }
  function rings(s) {
    if(s.page!=='app'||!adjustable(s))return Array(6).fill(0);
    const turns=position(s)/360;
    return Array.from({length:6},(_,i)=>clamp(turns-i,0,1));
  }
  function timeText(s) {
    const total=Math.ceil(Math.max(0,timerValue(s))/1000);
    return [Math.floor(total/3600),Math.floor(total/60)%60,total%60].map(n=>String(n).padStart(2,'0')).join(':');
  }
  function stopwatchText(s){
    const ticks=Math.floor(Math.max(0,s.stopwatchElapsed)/10),seconds=Math.floor(ticks/100);
    return [Math.floor(seconds/3600),Math.floor(seconds/60)%60,seconds%60].map(n=>String(n).padStart(2,'0')).join(':')+'.'+String(ticks%100).padStart(2,'0');
  }
  return {MINUTE,MAX_MINUTES,SAMPLE_TTL,COLORS,COLOR_NAMES,BLUE,APPS,MENU_STEP,LIGHT_STEP,LIGHT_EFFECTS,LIGHT_FIELDS,wrap,create,tick,healthy,timerValue,value,position,
    selectLightField,setLighting,lightFieldText,
    adjust,turn,observe,primary,timerPrimary,reset,resetStopwatch,setMode,selectMenu,home,back,preset,setConnected,setFault,demo,lighting,rings,timeText,stopwatchText};
})();
