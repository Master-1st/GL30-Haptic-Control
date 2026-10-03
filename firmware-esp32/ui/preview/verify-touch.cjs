// Real Chromium touch input acceptance, via the dedicated agent-browser session.
// node verify-touch.cjs <browser-websocket-url>
const assert=require('node:assert/strict');
(async()=>{
  const ws=new WebSocket(process.argv[2]),pending=new Map();let id=0;
  await new Promise((resolve,reject)=>{ws.onopen=resolve;ws.onerror=reject;});
  ws.onmessage=e=>{const msg=JSON.parse(e.data);if(pending.has(msg.id)){const p=pending.get(msg.id);pending.delete(msg.id);msg.error?p.reject(Error(JSON.stringify(msg.error))):p.resolve(msg.result);}};
  function command(method,params={},sessionId){return new Promise((resolve,reject)=>{const callId=++id;const timeout=setTimeout(()=>{pending.delete(callId);reject(Error('CDP timeout: '+method));},15000);pending.set(callId,{resolve:value=>{clearTimeout(timeout);resolve(value);},reject:error=>{clearTimeout(timeout);reject(error);}});ws.send(JSON.stringify({id:callId,method,params,...(sessionId?{sessionId}:{})}));});}
  const {targetInfos}=await command('Target.getTargets');
  const target=targetInfos.find(t=>t.type==='page'&&t.url.includes('/firmware-esp32/ui/preview/'));
  assert.ok(target,'GL30 browser target exists');
  const {sessionId}=await command('Target.attachToTarget',{targetId:target.targetId,flatten:true});
  const send=(method,params)=>command(method,params,sessionId);
  const evaluate=async expression=>{const r=await send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw Error(r.exceptionDetails.text);return r.result.value;};
  const pause=ms=>new Promise(r=>setTimeout(r,ms)),checks=[];
  const check=(ok,name)=>{assert.ok(ok,name);checks.push(name);};
  try{
    await send('Emulation.setFocusEmulationEnabled',{enabled:true});
    await send('Page.bringToFront');
    await evaluate('document.getElementById("product").scrollIntoView({block:"center"})');await pause(100);
    assert.equal(await evaluate('document.hidden'),false,'touch target is foreground');
    await send('Emulation.setTouchEmulationEnabled',{enabled:true,maxTouchPoints:2});
    check(await evaluate('navigator.maxTouchPoints')===2,'Chromium touch emulation active');
    const r=await evaluate('document.getElementById("product").getBoundingClientRect().toJSON()');
    assert.ok(r.top>=0&&r.bottom<=await evaluate('innerHeight'),'product is fully inside touch viewport');
    const p=(degrees,radius=187)=>({x:r.x+(340+radius*Math.sin(degrees*Math.PI/180))*r.width/680,y:r.y+(275-radius*Math.cos(degrees*Math.PI/180))*r.height/600,id:1,radiusX:3,radiusY:3,force:1});
    const touch=(type,points)=>send('Input.dispatchTouchEvent',{type,touchPoints:points});
    const phase=()=>evaluate('document.getElementById("stage").dataset.phase');
    const page=()=>evaluate('document.getElementById("stage").dataset.page');
    const tap=async(waitAfter=360)=>{await touch('touchStart',[p(0)]);await pause(70);await touch('touchEnd',[]);await pause(waitAfter);};
    const double=async()=>{await tap(80);await tap(360);};
    check(await page()==='home','touch session starts on the watch home');
    await tap();check(await page()==='menu','real single touch opens the menu');
    const selections=[],menuCount=await evaluate('GL30.APPS.length'),menuStep=360/menuCount;
    await touch('touchStart',[p(0)]);await pause(50);
    for(const degrees of Array.from({length:menuCount},(_,i)=>-(i+1)*menuStep)){
      await touch('touchMove',[p(degrees)]);await pause(35);
      selections.push(await evaluate('Number(document.getElementById("stage").dataset.selection)'));
    }
    await touch('touchEnd',[]);await pause(80);
    check(JSON.stringify(selections)===JSON.stringify(Array.from({length:menuCount},(_,i)=>menuCount-i-1)),'real reverse menu drag selects all nine applications across the seam');
    check(await page()==='menu','menu drag release cannot confirm the selected application');
    check(Math.abs(await evaluate('Number(document.getElementById("menuOrbit").dataset.position)')+menuCount)<.001,'one reverse menu turn preserves its negative winding count');
    await touch('touchStart',[p(0)]);await pause(50);
    for(const degrees of Array.from({length:menuCount},(_,i)=>(i+1)*menuStep)){await touch('touchMove',[p(degrees)]);await pause(35);}
    await touch('touchEnd',[]);await pause(80);
    check(Math.abs(await evaluate('Number(document.getElementById("menuOrbit").dataset.position)'))<.001,'one forward menu turn returns continuously to the first application');
    check(await evaluate('["lightPrevious","lightTrail"].every(id=>Number(document.getElementById(id).getAttribute("opacity"))===0)&&Number(document.getElementById("lightHead").getAttribute("opacity"))===1'),'real menu turns retain only one blue position light');
    await tap();check(await page()==='app','second real single confirms the default timer');
    await touch('touchStart',[p(0)]);await pause(50);
    for(const degrees of [45,90,135,180,225,270,315,360]){await touch('touchMove',[p(degrees)]);await pause(40);}
    await touch('touchEnd',[]);await pause(80);
    check(await phase()==='setting','touch drag release does not start timer');
    check(await evaluate('document.getElementById("targetText").textContent')==='01:00:00','one full touch revolution sets one hour');
    await touch('touchStart',[p(0,0)]);await pause(55);await touch('touchEnd',[]);await pause(80);
    check(await phase()==='setting','real central glass touch does not start');
    await touch('touchStart',[p(0)]);await pause(80);check(await phase()==='setting','touch-down alone does not activate');
    await touch('touchMove',[{...p(0),x:p(0).x+3}]);await pause(30);
    await touch('touchEnd',[]);await pause(360);check(await phase()==='running','touch release with 3px jitter starts once');
    await touch('touchStart',[p(0)]);await pause(60);await touch('touchCancel',[]);await pause(80);
    check(await phase()==='running','cancelled touch cannot toggle timer');
    await double();check(await page()==='menu'&&await phase()==='running','real double returns to menu without pausing timer');
    await double();check(await page()==='home'&&await phase()==='running','real double from menu returns home with timer continuing');
    await touch('touchStart',[p(0)]);await pause(720);await touch('touchEnd',[]);await pause(360);
    check(await page()==='home','real long touch has no assigned action');
    await tap(80);await touch('touchStart',[p(0)]);await pause(720);await touch('touchEnd',[]);await pause(360);
    check(await page()==='home','real click then second long touch cannot open menu');
    await tap();await tap();check(await page()==='app'&&await phase()==='running','touch navigation reopens the running timer without toggling it');
    const beforeReverse=await evaluate('Number(document.getElementById("target").value)');
    await touch('touchStart',[p(0)]);await pause(45);
    for(const degrees of [315,270,225,180]){await touch('touchMove',[p(degrees)]);await pause(30);}
    await touch('touchEnd',[]);await pause(50);
    const afterReverse=await evaluate('Number(document.getElementById("target").value)');
    check(await phase()==='running','real reverse drag during countdown does not pause or restart');
    check(beforeReverse-afterReverse>=30&&beforeReverse-afterReverse<30.1,'real half-turn reverse removes thirty minutes while running');
    await touch('touchStart',[p(180)]);await pause(45);
    for(const degrees of [135,90,45,0]){await touch('touchMove',[p(degrees)]);await pause(30);}
    await touch('touchEnd',[]);await pause(50);
    check(await phase()==='done'&&await evaluate('Number(document.getElementById("target").value)')===0,'real reverse drag reaches zero and completes without negative value');
    await touch('touchStart',[p(0)]);await pause(45);await touch('touchMove',[p(45)]);await touch('touchEnd',[]);await pause(50);
    check(await phase()==='setting'&&Math.abs(await evaluate('Number(document.getElementById("target").value)')-7.5)<.001,'new forward touch leaves zero immediately and does not auto-start');
    check(!(await evaluate('document.documentElement.scrollWidth>innerWidth')),'390px mobile page has no horizontal overflow');
    const buttonTap=async(waitAfter=360,hold=70)=>{
      await evaluate('document.getElementById("primary").scrollIntoView({block:"center"})');await pause(60);
      const b=await evaluate('document.getElementById("primary").getBoundingClientRect().toJSON()');
      await touch('touchStart',[{x:b.x+b.width/2,y:b.y+b.height/2,id:1,radiusX:3,radiusY:3,force:1}]);
      await pause(hold);await touch('touchEnd',[]);await pause(waitAfter);
    };
    await buttonTap(80);await buttonTap();
    check(await page()==='menu'&&await phase()==='setting','real primary button double uses back without starting timer');
    await buttonTap(360,720);check(await page()==='menu','real primary button long hold is unassigned');
    await buttonTap();check(await page()==='app'&&await phase()==='setting','real primary button single opens the selected app');
    await buttonTap();check(await phase()==='running','real primary button single starts timer once');
    await buttonTap(80);await buttonTap();check(await page()==='menu'&&await phase()==='running','real primary button double never first pauses timer');
    await evaluate('document.getElementById("menuLighting").click();document.getElementById("product").scrollIntoView({block:"center"})');await pause(80);
    const lightRect=await evaluate('document.getElementById("product").getBoundingClientRect().toJSON()');
    const lightPoint=degrees=>({x:lightRect.x+(340+187*Math.sin(degrees*Math.PI/180))*lightRect.width/680,y:lightRect.y+(275-187*Math.cos(degrees*Math.PI/180))*lightRect.height/600,id:1,radiusX:3,radiusY:3,force:1});
    const lightTap=async waitAfter=>{await touch('touchStart',[lightPoint(0)]);await pause(70);await touch('touchEnd',[]);await pause(waitAfter);};
    await lightTap(360);await lightTap(360);
    check(await evaluate('document.getElementById("stage").dataset.mode==="lighting"&&document.getElementById("stage").dataset.lightEditing==="true"'),'real touch opens lighting and enters its effect editor');
    await touch('touchStart',[lightPoint(0)]);await pause(50);await touch('touchMove',[lightPoint(30)]);await pause(50);await touch('touchEnd',[]);await pause(80);
    check(await evaluate('document.getElementById("ledRing").dataset.effect==="steady"&&document.getElementById("stage").dataset.lightEditing==="true"')&&await phase()==='running','real rotary touch edits the light effect without confirming or disturbing the countdown');
    await lightTap(80);await lightTap(360);
    check(await page()==='app'&&await evaluate('document.getElementById("stage").dataset.lightEditing')==='false','real double touch first exits the light editor');
    await lightTap(80);await lightTap(360);
    check(await page()==='menu'&&await evaluate('Number(document.getElementById("lightTrail").getAttribute("opacity"))')===0,'next real double returns to the pointer-only menu');
    console.log(JSON.stringify({scope:'trusted Chromium Input.dispatchTouchEvent; not a physical phone',passed:checks.length,checks},null,2));
  }finally{await command('Target.detachFromTarget',{sessionId});ws.close();}
})().catch(error=>{console.error(error);process.exit(1);});
