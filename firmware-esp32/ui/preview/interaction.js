'use strict';
(() => {
  const M = globalThis.GL30, G=globalThis.GL30Gesture, $ = id => document.getElementById(id);
  const s = M.create(performance.now()), systemReduced = matchMedia('(prefers-reduced-motion: reduce)');
  const svgNS = 'http://www.w3.org/2000/svg', stage = $('stage'), knob = $('knobControl');
  let shaft = 0, rawDown = false, gate = G.create(false,s.last), drag = null, heldKey = null, previewPointer=null;
  let manualReduced = false, activity = 0, lastFrame = s.last, lastAnnouncement = '';
  const heldArrows=new Set();
  const ringNodes = [];
  let menuVisual=0,menuWasVisible=false,menuOrder='';
  const menuNodes=[];
  const fmt = n => Math.abs(n-Math.round(n)) < .001 ? String(Math.round(n)) : n.toFixed(1);
  const text = (id,value) => {const el=$(id), str=String(value); if(el.textContent!==str)el.textContent=str;};
  const html = (id,value) => {const el=$(id);if(el.innerHTML!==value)el.innerHTML=value;};
  const attr = (el,name,value) => {const str=String(value); if(el.getAttribute(name)!==str)el.setAttribute(name,str);};
  function element(tag, attrs, parent) { const el=document.createElementNS(svgNS,tag); Object.entries(attrs).forEach(([k,v])=>el.setAttribute(k,v)); parent.append(el); return el; }
  function polar(radius,degrees,cy=275) {const a=(degrees-90)*Math.PI/180;return [340+radius*Math.cos(a),cy+radius*Math.sin(a)];}
  function arc(radius,start,end,cy=275) {
    const a=polar(radius,start,cy),b=polar(radius,end,cy);
    if(end-start>=359.999){const mid=polar(radius,start+180,cy);return `M${a.join(' ')}A${radius} ${radius} 0 1 1 ${mid.join(' ')}A${radius} ${radius} 0 1 1 ${a.join(' ')}`;}
    return `M${a.join(' ')}A${radius} ${radius} 0 ${end-start>180?1:0} 1 ${b.join(' ')}`;
  }
  for(let i=0;i<120;i++) {
    const a=polar(198,i*3),b=polar(208,i*3+.45);
    element('path',{d:`M${a.join(' ')}L${b.join(' ')}`,stroke:i%2?'#9da6a5':'#030505','stroke-opacity':i%2?.2:.65,'stroke-width':i%5?1:1.4},$('knurl'));
  }
  // Ideal diffuser appearance: continuous trails and a blue leading light.
  // This is not a claim that an untested physical 24-LED ring has these optics.
  const stripParts=['Previous','Trail','Head'].map(part=>({
    line:element('path',{id:'light'+part,fill:'none','stroke-width':part==='Head'?5.2:4.2,'stroke-linecap':'round'},$('ledRing')),
    glow:element('path',{fill:'none','stroke-width':part==='Head'?10:8,'stroke-linecap':'round'},$('ledGlow'))
  }));
  for(let i=0;i<6;i++) {
    const r=139-i*6, circumference=2*Math.PI*r;
    element('circle',{cx:340,cy:275,r,fill:'none',stroke:'#74543e','stroke-opacity':.13,'stroke-width':1.2},$('timeRings'));
    const el=element('circle',{cx:340,cy:275,r,fill:'none',stroke:'#ff9b51','stroke-width':3,'stroke-linecap':'round','data-time-ring':i},$('timeRings'));
    ringNodes.push({el,circumference});
  }
  for(let i=0;i<10;i++) {const a=i*Math.PI/5; element('path',{d:`M${Math.cos(a)*38} ${Math.sin(a)*38}l${Math.cos(a)*7} ${Math.sin(a)*7}`,stroke:'#ffc68b','stroke-width':2,'stroke-linecap':'round'},$('finishRays'));}
  M.APPS.forEach((app,index)=>{
    const suffix=app.id[0].toUpperCase()+app.id.slice(1);
    const node=element('g',{id:'svgMenu'+suffix,class:'orbit-app','data-app':app.id},$('menuOrbit'));
    node.style.setProperty('--app-color',app.color);
    element('circle',{r:23},node);
    element('use',{href:'#i-'+app.icon,x:-12,y:-12,width:24,height:24},node);
    menuNodes.push(node);
    const button=document.createElement('button');button.id='menu'+suffix;button.type='button';
    button.style.setProperty('--app-color',app.color);
    button.innerHTML=`<svg viewBox="0 0 24 24" aria-hidden="true"><use href="#i-${app.icon}"/></svg><span>${app.label}</span>`;
    button.onclick=()=>action(()=>{M.selectMenu(s,index);menuVisual=s.menuPosition;});$('menuChoices').append(button);
  });
  document.querySelectorAll('[data-light-field]').forEach(button=>button.onclick=()=>action(()=>M.selectLightField(s,Number(button.dataset.lightField))));
  M.LIGHT_EFFECTS.forEach((effect,index)=>{
    const button=document.createElement('button');button.type='button';button.dataset.lightEffect=index;button.textContent=effect.label;
    button.onclick=()=>action(()=>M.setLighting(s,0,index));$('lightEffectChoices').append(button);
  });
  M.COLORS.forEach((color,index)=>{
    const button=document.createElement('button');button.type='button';button.dataset.lightColor=index;
    button.style.setProperty('--swatch',color);button.setAttribute('aria-label',M.COLOR_NAMES[index]);button.title=M.COLOR_NAMES[index];
    button.onclick=()=>action(()=>M.setLighting(s,1,index));$('lightColorChoices').append(button);
  });
  $('lightBrightness').oninput=()=>action(()=>M.setLighting(s,2,Number($('lightBrightness').value)));

  function renderMenu(dt,reduced){
    const visible=s.page==='menu';
    if(!visible){menuWasVisible=false;return;}
    if(!menuWasVisible||reduced||drag?.moved)menuVisual=s.menuPosition;
    else menuVisual+=(s.menuPosition-menuVisual)*(1-Math.exp(-dt/65));
    if(Math.abs(menuVisual-s.menuPosition)<.002)menuVisual=s.menuPosition;
    menuWasVisible=true;
    const order=menuNodes.map((node,i)=>{
      const theta=(i-menuVisual)*2*Math.PI/M.APPS.length,depth=(Math.cos(theta)+1)/2;
      const scale=.52+.62*depth;
      attr(node,'transform',`translate(${340+93*Math.sin(theta)} ${260+57*Math.cos(theta)}) scale(${scale})`);
      attr(node,'opacity',.32+.68*depth);attr(node,'data-depth',depth);attr(node,'data-scale',scale);
      node.classList.toggle('selected',s.menuIndex===i);
      attr($('menu'+M.APPS[i].id[0].toUpperCase()+M.APPS[i].id.slice(1)),'aria-pressed',s.menuIndex===i);
      return {node,i,depth};
    }).sort((a,b)=>a.depth-b.depth);
    const key=order.map(item=>item.i).join(',');
    if(key!==menuOrder){order.forEach(item=>$('menuOrbit').append(item.node));menuOrder=key;}
    attr($('menuOrbit'),'data-position',menuVisual);
    text('menuName',M.APPS[s.menuIndex].label);
    text('menuCaption',`${String(s.menuIndex+1).padStart(2,'0')} / ${M.APPS.length} · 单击打开`);
  }

  function utilityData(now){
    switch(s.mode){
      case 'stopwatch':return {value:M.stopwatchText(s),detail:s.stopwatchRunning?'正在计时':s.stopwatchElapsed?'已暂停':'准备就绪',
        hint:'单击启停 · 双击返回',help:'单击开始或暂停，清零可重新计时。离开秒表或熄屏后，运行中的秒表继续计时。'};
      case 'weather':return {value:'24°',detail:'晴朗 · 天气示例',hint:'尚未连接实时天气',help:'示例天气：晴朗，24°C。当前未接入位置与实时天气数据。'};
      case 'calendar':return {value:`${now.getMonth()+1}月${now.getDate()}日`,detail:new Intl.DateTimeFormat('zh-CN',{year:'numeric',weekday:'long'}).format(now),
        hint:'本地日期 · 双击返回',help:'显示当前电脑的本地日期。'};
      case 'alarm':return {value:'尚未设置',detail:'占位 · 闹钟',hint:'暂不触发提醒',help:'闹钟占位页。时间设定、重复日期和到点提醒尚未接入。'};
      case 'haptics':return {value:'手感预设',detail:'占位 · 段落感与阻尼',hint:'尚未接入电机调节',help:'手感调整占位页。段落感、阻尼和止挡强度尚未接入硬件控制。'};
      case 'lighting':return {value:M.lightFieldText(s),detail:(s.lightEditing?'正在调整 · ':`${s.lightField+1} / 3 · `)+M.LIGHT_FIELDS[s.lightField],
        hint:s.lightEditing?'旋转调值 · 单击确认':'旋转选项目 · 单击调整',
        help:'颜色用于常亮、呼吸和流动效果，返回桌面后继续显示。菜单保留蓝色指针，计时与音量显示各自进度；亮度统一生效。'};
      default:return {value:'偏好设置',detail:'占位 · 显示与连接',hint:'设置项待接入',help:'设置占位页。亮度、声音和连接配置尚未接入。'};
    }
  }

  function sample(now) {
    const before=s.phase;M.tick(s,now);M.observe(s,M.position(s),now,s.connected&&!s.fault);
    const completed=before!=='done'&&s.phase==='done';
    if(completed)cancelGesture();
    return completed;
  }
  function action(fn) {
    if(!sample(performance.now())){cancelGesture();const before=s.phase;fn();M.observe(s,M.position(s),s.last,s.connected&&!s.fault);if(before!=='done'&&s.phase==='done')cancelGesture();}
    render(0);
  }
  function pulse() {activity=1;stage.classList.add('pressing');setTimeout(()=>{if(!rawDown)stage.classList.remove('pressing');},120);}
  function primary() {M.primary(s);pulse();}
  function dispatchPress(event){
    if(event==='single')action(primary);
    else if(event==='double')action(()=>{M.back(s);pulse();});
    // Long press deliberately has no application action.
  }
  function cancelPress(){rawDown=false;heldKey=null;gate=G.create(false,performance.now());stage.classList.remove('pressing');}
  function cancelGesture(){cancelPress();drag=null;previewPointer=null;heldArrows.clear();}
  function turn(degrees) {
    if(!Number.isFinite(degrees))return;
    const now=performance.now();if(sample(now)){render(0);return;}
    cancelPress();const before=s.phase;M.turn(s,degrees);M.observe(s,M.position(s),now,s.connected&&!s.fault);
    if(before!=='done'&&s.phase==='done')cancelGesture();
    activity=Math.min(1,activity+Math.abs(degrees)/30);render(0);
  }
  function rawPress(down,now) {
    if(!down&&rawDown)dispatchPress(G.sample(gate,true,now));
    rawDown=down;dispatchPress(G.sample(gate,down,now));stage.classList.toggle('pressing',rawDown);
  }
  function mode(name) {cancelGesture();M.setMode(s,name);activity=.8;}

  $('sideTimer').onclick=()=>action(()=>mode('timer'));
  $('sideVolume').onclick=()=>action(()=>mode('volume'));
  $('navHome').onclick=()=>action(()=>M.home(s));
  $('navBack').onclick=()=>action(()=>M.back(s));
  // Accessibility/programmatic activation has no raw pointer sequence.
  $('primary').onclick=e=>{
    if(e.detail!==0)return;
    const now=performance.now();if(sample(now))return;
    dispatchPress(G.click(gate,now));render(0);
  };
  $('backgroundPrimary').onclick=()=>action(()=>{M.timerPrimary(s);pulse();});
  $('backToTimer').onclick=()=>action(()=>mode('timer'));
  $('plus').onclick=$('sidePlus').onclick=()=>action(()=>{M.adjust(s,1);activity=.7;});
  $('minus').onclick=$('sideMinus').onclick=()=>action(()=>{M.adjust(s,-1);activity=.7;});
  $('target').oninput=()=>action(()=>{
    const value=Number($('target').value);
    if(s.mode==='timer')M.preset(s,value);else if(s.mode==='volume')M.adjust(s,value-s.volume);
    activity=.5;
  });
  document.querySelectorAll('[data-minutes]').forEach(el=>el.onclick=()=>action(()=>{M.preset(s,Number(el.dataset.minutes));activity=1;}));
  $('reset').onclick=()=>action(()=>{if(s.off){s.off=false;return;}if(!s.fault){if(s.mode==='stopwatch')M.resetStopwatch(s);else if(s.mode==='timer')M.reset(s);activity=.6;}});
  $('power').onclick=$('rearPower').onclick=()=>action(()=>{cancelGesture();s.off=!s.off;});
  $('lost').onclick=()=>action(()=>{cancelGesture();M.setConnected(s,!s.connected);});
  $('fault').onclick=()=>action(()=>{cancelGesture();M.setFault(s,!s.fault);});
  $('reduce').onclick=()=>{manualReduced=!manualReduced;render(0);};
  $('demo').onclick=()=>action(()=>{cancelGesture();if(s.demo&&(s.phase==='running'||s.phase==='paused'))M.reset(s);else M.demo(s);activity=1;});

  // Only the outer metal ring accepts drag/press; the glass has no activation handler.
  const point=e=>{const p=$('product').createSVGPoint();p.x=e.clientX;p.y=e.clientY;return p.matrixTransform($('product').getScreenCTM().inverse());};
  knob.addEventListener('pointerdown',e=>{
    if(!e.isPrimary||e.button!==0||drag||heldKey||previewPointer!==null)return;
    e.preventDefault();knob.focus({preventScroll:true});knob.setPointerCapture(e.pointerId);
    const p=point(e),now=performance.now();if(sample(now)){knob.releasePointerCapture(e.pointerId);render(0);return;}
    drag={id:e.pointerId,angle:Math.atan2(p.y-275,p.x-340),x:e.clientX,y:e.clientY,moved:false};
    rawPress(true,now);
  });
  knob.addEventListener('pointermove',e=>{
    if(!drag||drag.id!==e.pointerId)return;
    const p=point(e);if(Math.hypot(e.clientX-drag.x,e.clientY-drag.y)>6&&!drag.moved){drag.moved=true;cancelPress();}
    if(Math.hypot(p.x-340,p.y-275)<120)return;
    const a=Math.atan2(p.y-275,p.x-340),delta=Math.atan2(Math.sin(a-drag.angle),Math.cos(a-drag.angle))*180/Math.PI;
    drag.angle=a;if(drag.moved)turn(delta);
  });
  knob.addEventListener('pointerup',e=>{
    if(!drag||drag.id!==e.pointerId)return;
    if(!drag.moved)rawPress(false,performance.now());
    drag=null;if(knob.hasPointerCapture(e.pointerId))knob.releasePointerCapture(e.pointerId);
  });
  knob.addEventListener('pointercancel',cancelGesture);
  knob.addEventListener('lostpointercapture',()=>{if(drag)cancelGesture();});
  const rotaryStep=()=>s.page==='menu'?M.MENU_STEP:s.mode==='lighting'?M.LIGHT_STEP:s.mode==='timer'?6:3.6;
  knob.addEventListener('wheel',e=>{e.preventDefault();if(e.deltaY)turn(-Math.sign(e.deltaY)*rotaryStep());},{passive:false});
  function keyDown(e) {
    if(['ArrowUp','ArrowDown','ArrowLeft','ArrowRight'].includes(e.key)) {
      e.preventDefault();const code=e.code||e.key;
      if(e.repeat&&!heldArrows.has(code))return;
      heldArrows.add(code);turn((['ArrowUp','ArrowRight'].includes(e.key)?1:-1)*rotaryStep());return;
    }
    if(e.key!==' '&&e.key!=='Enter')return;
    e.preventDefault();if(e.repeat||heldKey||drag||previewPointer!==null)return;
    if(sample(performance.now()))return;
    heldKey=e.code;rawPress(true,performance.now());
  }
  function keyUp(e) {heldArrows.delete(e.code||e.key);if(e.code===heldKey){e.preventDefault();rawPress(false,performance.now());heldKey=null;}}
  knob.addEventListener('keydown',keyDown);knob.addEventListener('keyup',keyUp);knob.addEventListener('blur',cancelGesture);
  const preview=$('primary');
  preview.addEventListener('pointerdown',e=>{
    if(!e.isPrimary||e.button!==0||drag||heldKey||previewPointer!==null)return;
    e.preventDefault();preview.focus({preventScroll:true});
    if(sample(performance.now()))return;
    previewPointer=e.pointerId;preview.setPointerCapture(e.pointerId);rawPress(true,performance.now());
  });
  preview.addEventListener('pointerup',e=>{
    if(previewPointer!==e.pointerId)return;
    rawPress(false,performance.now());previewPointer=null;
    if(preview.hasPointerCapture(e.pointerId))preview.releasePointerCapture(e.pointerId);
  });
  preview.addEventListener('pointercancel',cancelGesture);
  preview.addEventListener('lostpointercapture',()=>{if(previewPointer!==null)cancelGesture();});
  preview.addEventListener('keydown',keyDown);preview.addEventListener('keyup',keyUp);preview.addEventListener('blur',cancelGesture);
  document.addEventListener('keydown',e=>{
    if(e.key==='Escape'&&!e.repeat){e.preventDefault();action(()=>M.back(s));return;}
    if(e.repeat&&e.target.closest('button')&&['Enter',' '].includes(e.key)){e.preventDefault();return;}
    if(e.key===' '&&!e.target.closest('button,input,summary,a,#knobControl'))keyDown(e);
  });
  document.addEventListener('keyup',keyUp);
  window.addEventListener('blur',cancelGesture);
  document.addEventListener('visibilitychange',()=>{cancelGesture();M.tick(s,performance.now());render(0);});

  function render(dt) {
    const app=s.page==='app',timer=s.mode==='timer',volume=s.mode==='volume',core=timer||volume,
      stopwatch=s.mode==='stopwatch',lighting=s.mode==='lighting',selectedApp=M.APPS.find(item=>item.id===s.mode),reduced=manualReduced||systemReduced.matches;
    const backgroundTimer=(!app||!timer)&&['running','paused'].includes(s.phase);
    const editable=app&&core&&M.healthy(s)&&!s.off;
    const value=M.value(s),light=M.lighting(s,reduced);
    const status=s.off?'OFF':s.fault?'FAULT':!s.connected?'NO SIGNAL':s.angle===null?'ANGLE UNKNOWN':timer?({setting:'READY',running:'RUNNING',paused:'PAUSED',done:'DONE'}[s.phase]):volume?(s.muted?'MUTED':'VOLUME'):stopwatch?(s.stopwatchRunning?'RUNNING':'PAUSED'):s.mode.toUpperCase();
    document.body.classList.toggle('reduced',reduced);
    stage.classList.toggle('screen-off',s.off);stage.classList.toggle('volume-mode',app&&volume);stage.classList.toggle('muted',s.muted);
    stage.classList.toggle('faulted',s.fault);['running','paused','done'].forEach(p=>stage.classList.toggle('is-'+p,app&&timer&&s.phase===p));
    stage.dataset.page=s.page;stage.dataset.selection=s.menuIndex;stage.dataset.appView=core?'core':'utility';
    stage.dataset.lightField=s.lightField;stage.dataset.lightEditing=s.lightEditing;
    stage.dataset.phase=s.phase;stage.dataset.mode=s.mode;stage.dataset.off=s.off;stage.dataset.input=s.connected?(s.fault?'fault':s.angle===null?'unknown':'simulated'):'disconnected';
    $('homePanel').hidden=s.page!=='home';$('menuPanel').hidden=s.page!=='menu';$('appControls').hidden=!app||!core;
    $('utilityPanel').hidden=!app||core;
    $('lightingControls').hidden=!app||!lighting;
    document.querySelectorAll('[data-light-field]').forEach(button=>{attr(button,'aria-pressed',Number(button.dataset.lightField)===s.lightField);button.disabled=s.off;});
    for(const [selector,selected] of [['[data-light-effect]',s.lightEffect],['[data-light-color]',s.lightColor]]){
      document.querySelectorAll(selector).forEach((button,i)=>{attr(button,'aria-pressed',i===selected);button.disabled=!app||!lighting||s.off||!M.healthy(s);});
    }
    $('lightEffectChoices').hidden=s.lightField!==0;$('lightColorChoices').hidden=s.lightField!==1;$('lightBrightnessControl').hidden=s.lightField!==2;
    $('lightBrightness').disabled=!app||!lighting||s.off||!M.healthy(s);$('lightBrightness').value=s.lightBrightness;text('lightBrightnessText',s.lightBrightness+'%');
    $('navBack').disabled=s.page==='home'&&!s.off;
    text('navTitle',s.page==='home'?'桌面':s.page==='menu'?'应用菜单':selectedApp.label);
    const nowDate=new Date(),clock=[nowDate.getHours(),nowDate.getMinutes()].map(n=>String(n).padStart(2,'0')).join(':');
    text('homeTime',clock);text('desktopTime',clock);text('homeSeconds',String(nowDate.getSeconds()).padStart(2,'0'));
    text('homeDate',new Intl.DateTimeFormat('zh-CN',{month:'long',day:'numeric',weekday:'long'}).format(nowDate));
    text('homeTimerText',['running','paused'].includes(s.phase)?`${s.phase==='running'?'计时中':'已暂停'} ${M.timeText(s)}`:'计时器 · 待设定');
    renderMenu(dt,reduced);
    if(s.page==='menu'){
      // The pointer and simulated rim follow the same visible orbit during easing.
      light.angle=M.wrap(menuVisual*M.MENU_STEP);light.turns=menuVisual/M.APPS.length;
    }
    const utility=utilityData(nowDate);
    if(app&&!core){
      attr($('utilityIcon'),'href','#i-'+selectedApp.icon);$('utilityScreen').style.setProperty('--app-color',lighting?M.COLORS[s.lightColor]:selectedApp.color);
      text('utilityTitle',selectedApp.label);text('utilityValue',utility.value);text('utilityDetail',utility.detail);text('utilityHint',utility.hint);
      text('utilityPanelTitle',selectedApp.label);text('utilityPanelValue',utility.value);text('utilityPanelHint',utility.help);
    }
    text('sceneState',s.off?'屏幕与灯环已熄灭':s.fault?'模拟输入故障':!s.connected?'模拟角度输入中断':!timer?(backgroundTimer?'音量 · 后台计时':'模拟音量控制'):({setting:'等待旋转',running:'专注进行中',paused:'休息一下',done:'这一刻，完成了'}[s.phase]));
    text('screenMode',timer?(s.demo?'12 SECOND PREVIEW':'FOCUS TIMER'):'VOLUME CONTROL');
    text('screenTime',timer?M.timeText(s):Math.round(s.volume)+'%');
    text('screenPrompt',s.fault?'解除故障后 · 手动继续':!s.connected?'角度未知 · 计时独立运行':timer?({setting:'转动设时 · 按压开始',running:'轻按旋环 · 暂停片刻',paused:'轻按旋环 · 继续专注',done:'轻按旋环 · 重新设定'}[s.phase]):s.muted?'轻按旋环 · 取消静音':'轻按旋环 · 静音');
    if(backgroundTimer&&!s.fault&&s.connected)text('screenPrompt',`计时 ${M.timeText(s)} · ${s.phase==='running'?'运行中':'已暂停'}`);
    $('backgroundTimer').hidden=!backgroundTimer;
    text('backgroundTime',M.timeText(s));
    text('backgroundPrimary',s.off?'唤醒':s.phase==='running'?'暂停计时':'继续计时');
    $('backgroundPrimary').disabled=!s.off&&s.phase!=='running'&&!M.healthy(s);
    text('screenStatus',status);
    text('targetText',timer?M.timeText(s):fmt(value));$('targetText').classList.toggle('timer-value',timer);
    text('unit',timer?'时:分:秒':'%');text('valueLabel',timer?(s.phase==='setting'?'设定时间':'剩余时间'):'模拟音量');
    text('phaseText',timer?({setting:'待设定',running:'自动回转 · 可手动调整',paused:'已暂停 · 可手动调整',done:'已回到零点'}[s.phase]):s.muted?'已静音 · 保留音量':'手动调节');
    attr($('target'),'max',timer?360:100);attr($('target'),'aria-label',timer?'设定时长（分钟）':'模拟音量（百分比）');
    if(Number($('target').value)!==value)$('target').value=value;
    [$('target'),$('plus'),$('minus'),$('sidePlus'),$('sideMinus')].forEach(el=>el.disabled=!editable);
    attr(knob,'role',s.page==='home'||(app&&!core&&!lighting)?'button':'slider');
    attr(knob,'aria-label',s.page==='home'?'旋环，单击进入菜单':s.page==='menu'?'旋环，循环选择应用，单击打开，双击返回':!core?`${selectedApp.label}，${utility.hint}`:'旋环，转动调节，单击确定，双击返回');
    if(s.page==='home'||(app&&!core&&!lighting))['aria-valuemin','aria-valuemax','aria-valuenow','aria-valuetext'].forEach(name=>knob.removeAttribute(name));
    else if(app&&lighting){
      attr(knob,'aria-valuemin',0);attr(knob,'aria-valuemax',s.lightEditing?[M.LIGHT_EFFECTS.length-1,M.COLORS.length-1,100][s.lightField]:M.LIGHT_FIELDS.length-1);
      attr(knob,'aria-valuenow',s.lightEditing?[s.lightEffect,s.lightColor,s.lightBrightness][s.lightField]:s.lightField);
      attr(knob,'aria-valuetext',`${M.LIGHT_FIELDS[s.lightField]} ${M.lightFieldText(s)}，${s.lightEditing?'调整中':'单击调整'}`);
    }
    else{
      attr(knob,'aria-valuemin',0);attr(knob,'aria-valuemax',app?(timer?360:100):M.APPS.length-1);
      attr(knob,'aria-valuenow',app?Number(value.toFixed(2)):s.menuIndex);
      attr(knob,'aria-valuetext',app?`${timer?'设定 '+fmt(value)+' 分钟':'音量 '+fmt(value)+'%'}，${status}，${s.angle===null?'角度未知':'模拟角度 '+s.angle.toFixed(1)+' 度'}`:M.APPS[s.menuIndex].label);
    }
    text('scaleMin',timer?'0 min':'0 %');text('scaleMax',timer?'360 min':'100 %');
    $('presets').hidden=!timer;
    document.querySelectorAll('[data-minutes]').forEach(el=>{el.disabled=!editable;attr(el,'aria-pressed',Math.abs(value-Number(el.dataset.minutes))<.001);});
    text('primaryText',s.off?'唤醒屏幕':timer?({setting:value>0?'按压旋环，开始计时':'先转动，设定时间',running:'按压旋环，暂停',paused:'按压旋环，继续',done:'完成 · 重新设定'}[s.phase]):s.muted?'按压旋环，取消静音':'按压旋环，静音');
    if(!app)text('primaryText',s.off?'单击唤醒屏幕':s.page==='home'?'单击，进入菜单':`单击，打开${M.APPS[s.menuIndex].label}`);
    else if(!core)text('primaryText',s.off?'单击唤醒屏幕':stopwatch?`单击，${s.stopwatchRunning?'暂停':s.stopwatchElapsed?'继续':'启动'}秒表`:'双击，返回菜单');
    if(app&&lighting&&!s.off)text('primaryText',`单击，${s.lightEditing?'确认':'调整'}${M.LIGHT_FIELDS[s.lightField]}`);
    // Even at zero or during a fault, double press must remain available to go back.
    $('primary').disabled=false;
    attr($('primaryIcon').firstElementChild,'href',s.off?'#i-power':app&&((timer&&s.phase==='running')||(stopwatch&&s.stopwatchRunning))?'#i-pause':app&&!core&&!stopwatch&&!lighting?'#i-back':'#i-play');
    $('power').querySelector('span').textContent=s.off?'唤醒':'熄屏';attr($('power'),'aria-pressed',s.off);
    $('reset').disabled=s.fault;
    $('reset').hidden=!app||(!timer&&!stopwatch);
    attr($('lost'),'aria-pressed',!s.connected);text('lost',s.connected?'模拟断连':'恢复连接');attr($('fault'),'aria-pressed',s.fault);text('fault',s.fault?'解除故障':'模拟故障');attr($('reduce'),'aria-pressed',reduced);
    $('demo').disabled=!M.healthy(s)||(!s.demo&&['running','paused'].includes(s.phase));
    $('demo').querySelector('span').firstChild.textContent=s.demo&&['running','paused'].includes(s.phase)?'退出加速演示':'看一次完整演示';
    $('demoBadge').hidden=!s.demo;text('demoBadge',s.phase==='done'?'加速演示已完成 · 可清零后体验正常计时':'加速演示 · 450× · 非真实 90 分钟');
    let title,description;
    if(s.off){title='屏幕已熄灭';description='屏幕与灯带同时熄灭，运行中的计时继续。下一次单击或双击仅唤醒。';}
    else if(s.fault){title='模拟故障，等待处理';description='计时已暂停，旋转与启动被锁定。解除故障后，手动按压继续。';}
    else if(!s.connected||s.angle===null){title='角度未知，不猜测位置';description='位置光已熄灭，计时独立运行。恢复输入后首帧只同步角度，不补算丢失圈数。';}
    else if(!core){title=selectedApp.label+' · '+utility.detail;description=utility.help;}
    else if(volume){title=s.muted?'暂时安静下来':'声波随调节展开';description=backgroundTimer?'旋转与按压只控制模拟音量。下方可暂停后台计时，到时自动回到完成画面。':'旋转调节模拟音量，按压静音。声波表达调节反馈，不是音频频谱，也不改变电脑音量。';}
    else if(s.phase==='running'){title='像烤箱旋钮一样，慢慢回转';description='旋环随剩余时间自动回到零点。随时手动往回减少时间，也可顺时针增加；运行不中断。';}
    else if(s.phase==='paused'){title='时间停住，依然可以调节';description='自动回转已暂停。手动旋转可改剩余时间，按压后从调整的位置继续。';}
    else if(s.phase==='done'){title='回到零点，这段时间完成了';description='蓝色前端停在零点，继续往回会遇到止挡。顺时针设定下一段时间，再按压开始。';}
    else{title=value?'为自己留出 '+M.timeText(s):'从一次小小的转动开始';description='一圈 = 1 小时，每圈一种颜色。蓝色标示最前端，后方连续铺色；中央玻璃保持固定。';}
    if(!app&&!s.off){
      title=s.page==='home'?'一块安静的桌面':'旋转选择，单击打开';
      description=s.page==='home'?'日照金山照片表盘，叠加本地时间和天气示例。单击进入菜单，双击逐级返回；长按暂未设置。':'连续转动圆环，把应用带到前方。单击打开，双击回到桌面；后台计时与秒表会继续。';
    }
    text('feedbackTitle',title);text('feedbackText',description);
    $('feedbackDot').style.background=s.fault?'#ff9e8c':s.off?'#747d78':!s.connected?'#e8c885':'#a4d5dd';
    text('angleText',s.angle===null?'未知':(s.page==='menu'?light.angle:s.angle).toFixed(1).padStart(5,'0')+'°');
    html('ledText',s.off?'熄灭':s.angle===null?'未知':s.lightBrightness===0?'熄灭 <em>· 亮度 0%</em>':light.pointerOnly?'蓝色指针':light.colorName+' <em>· 蓝色前端</em>');
    html('mappingText',!app?(s.page==='home'?'单击 <em>进入菜单</em>':`${M.MENU_STEP}° <em>· 循环选择</em>`):lighting?`旋转 <em>${s.lightEditing?'调节'+M.LIGHT_FIELDS[s.lightField]:'选择项目'}</em>`:!core?'双击 <em>返回菜单</em>':timer?'360° <em>= 60 min</em>':'360° <em>= 100 %</em>');
    if(!s.off&&!s.fault&&s.connected){
      if(!app)text('sceneState',s.page==='home'?'桌面 · 时间与天气':'应用菜单 · '+M.APPS[s.menuIndex].label);
      else if(!core)text('sceneState',selectedApp.label+' · '+utility.detail);
    }
    const partData=[{fraction:1,color:light.previous,opacity:.65},{fraction:light.fraction,color:light.color,opacity:light.intensity},{color:light.head,opacity:1}];
    stripParts.forEach(({line,glow},i)=>{
      const p=partData[i],visible=light.visible&&p.color&&(i===2||p.fraction>0);
      const start=i===1?light.start:0;
      const d=i===2?arc(222,light.angle-1.4,light.angle+1.4,284):arc(222,start,start+360*p.fraction,284);
      [line,glow].forEach(el=>{attr(el,'d',d);attr(el,'stroke',p.color||light.color);});
      attr(line,'opacity',visible?p.opacity*light.brightness:0);attr(glow,'opacity',visible?p.opacity*light.brightness*.55:0);
    });
    attr($('screenLightTrail'),'d',arc(124,light.start,light.start+360*light.fraction));attr($('screenLightTrail'),'stroke',light.color);
    attr($('screenLightTrail'),'opacity',light.visible&&light.fraction>0?light.brightness*light.intensity:0);
    attr($('screenLightHead'),'d',arc(124,light.angle-1.4,light.angle+1.4));attr($('screenLightHead'),'stroke',M.BLUE);
    attr($('screenLightHead'),'opacity',light.visible?light.brightness:0);
    attr($('lightHead'),'data-angle',light.angle);attr($('lightTrail'),'data-fraction',light.fraction);
    attr($('ledRing'),'data-turns',light.turns);attr($('ledRing'),'data-visible',light.visible);
    attr($('ledRing'),'data-effect',light.effect);attr($('ledRing'),'data-brightness',s.lightBrightness);
    if(M.healthy(s))shaft=s.page==='menu'?menuVisual*M.MENU_STEP:M.position(s);
    const rebound=s.endstop&&!reduced?s.stopIntent*Math.sin((s.stopUntil-s.last)/450*Math.PI*2)*1.4:0;
    $('rotor').style.transform=`rotate(${M.wrap(shaft)+rebound}deg)`;
    attr($('rotor'),'data-angle',M.wrap(shaft));attr($('rotor'),'data-position',shaft);
    stage.classList.toggle('is-endstop-lower',s.endstop==='lower');stage.classList.toggle('is-endstop-upper',s.endstop==='upper');
    stage.dataset.endstop=s.endstop||'';stage.dataset.stopIntent=s.stopIntent;
    $('endstopHint').hidden=!s.endstop;
    text('endstopHint',s.endstop==='lower'?'零点止挡 · 不能再减小':'上限止挡 · 不能再增加');
    const targets=M.rings(s);
    targets.forEach((target,i)=>{
      const {el,circumference:c}=ringNodes[i];
      attr(el,'stroke-dasharray',`${target*c} ${c}`);attr(el,'opacity',target>0?1:0);
      attr(el,'stroke',M.COLORS[i]);attr(el,'data-fraction',target.toFixed(6));
    });
    const fill=s.duration?Math.max(0,Math.min(1,s.remaining/s.duration)):1,upper=Math.sqrt(fill),lower=Math.sqrt(1-fill);
    attr($('upperSand'),'d',`M${-13*upper} ${-4-15*upper}H${13*upper}L0 -4Z`);
    attr($('lowerSand'),'d',`M${-13*lower} 21H${13*lower}L0 ${21-16*lower}Z`);
    $('soundWaves').children[0].style.opacity=String(Math.min(1,s.volume/33));$('soundWaves').children[1].style.opacity=String(Math.max(.1,Math.min(1,(s.volume-33)/33)));$('soundWaves').children[2].style.opacity=String(Math.max(.1,(s.volume-66)/34));
    activity*=Math.exp(-dt/220);
    const spring=reduced?0:activity*Math.sin((1-activity)*9);
    attr($('timerArt'),'transform',`translate(340 244) rotate(${spring*9}) scale(${1+spring*.09} ${1-spring*.09})`);
    attr($('volumeArt'),'transform',`translate(340 242) rotate(${spring*7}) scale(${1+spring*.08})`);
    $('screenContent').style.transform=reduced?'none':`translate(0,${-activity*2}px)`;
    const announcement=[s.page,s.menuIndex,status,s.mode,Math.round(value),s.lightField,s.lightEditing,s.lightEffect,s.lightColor,s.lightBrightness].join('|');
    if(announcement!==lastAnnouncement){lastAnnouncement=announcement;text('status',app?`${selectedApp.label} ${timer?M.timeText(s):volume?fmt(s.volume)+'%':utility.value}，${title}`:s.page==='home'?'桌面，单击进入菜单':`菜单，已选${M.APPS[s.menuIndex].label}，单击打开，双击返回`);}
  }

  function frame(now) {
    const dt=Math.min(100,Math.max(0,now-lastFrame));lastFrame=now;
    sample(now);
    dispatchPress(G.sample(gate,rawDown,now));
    render(dt);requestAnimationFrame(frame);
  }
  sample(s.last);render(0);requestAnimationFrame(frame);
})();
