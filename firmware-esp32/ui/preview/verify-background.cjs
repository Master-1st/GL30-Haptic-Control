// Verify actual Chromium freeze -> resume -> foreground rendering, not just a fake clock.
const assert=require('node:assert/strict');
(async()=>{
  const ws=new WebSocket(process.argv[2]);await new Promise((r,j)=>{ws.onopen=r;ws.onerror=j;});let id=0;const pending=new Map();
  ws.onmessage=e=>{const m=JSON.parse(e.data),p=pending.get(m.id);if(p){pending.delete(m.id);m.error?p.reject(Error(JSON.stringify(m.error))):p.resolve(m.result);}};
  const call=(method,params={},sessionId)=>new Promise((resolve,reject)=>{const n=++id;pending.set(n,{resolve,reject});ws.send(JSON.stringify({id:n,method,params,sessionId}));});
  const {targetInfos}=await call('Target.getTargets');const target=targetInfos.find(t=>t.type==='page'&&t.url.includes('/firmware-esp32/ui/preview/'));
  const {sessionId}=await call('Target.attachToTarget',{targetId:target.targetId,flatten:true});
  const send=(method,params={})=>call(method,params,sessionId);
  const evaluate=async expression=>{const r=await send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw Error(r.exceptionDetails.text);return r.result.value;};
  const delay=ms=>new Promise(r=>setTimeout(r,ms));
  try{
    await send('Emulation.setFocusEmulationEnabled',{enabled:true});
    await send('Page.bringToFront');await delay(80);
    assert.equal(await evaluate('document.hidden'),false,'baseline is foreground');
    await evaluate('document.getElementById("sideTimer").click();document.getElementById("reset").click();document.querySelector("[data-minutes=\\"25\\"]").click();document.getElementById("primary").click();');
    await delay(360);
    assert.equal(await evaluate('document.getElementById("stage").dataset.phase'),'running');
    await evaluate('document.getElementById("navHome").click()');
    const before=await evaluate('document.getElementById("screenTime").textContent');
    await send('Emulation.setFocusEmulationEnabled',{enabled:false});
    await send('Page.setWebLifecycleState',{state:'frozen'});await delay(2200);
    await send('Page.setWebLifecycleState',{state:'active'});await send('Emulation.setFocusEmulationEnabled',{enabled:true});await send('Page.bringToFront');await delay(100);
    assert.equal(await evaluate('document.hidden'),false,'resumed page is visible');
    const after=await evaluate('document.getElementById("screenTime").textContent');const seconds=t=>{const a=t.split(':').map(Number);return a[0]*3600+a[1]*60+a[2];};
    assert.equal(await evaluate('document.getElementById("stage").dataset.page'),'home');
    const result={scope:'Chromium freeze/resume with CDP focus emulation; not physical device sleep',before,after,passed:seconds(before)-seconds(after)>=2&&seconds(before)-seconds(after)<=3};
    assert.ok(result.passed,'whole background elapsed time is consumed on foreground return');console.log(JSON.stringify(result,null,2));
  }finally{await send('Page.setWebLifecycleState',{state:'active'});await send('Page.bringToFront');await call('Target.detachFromTarget',{sessionId});ws.close();}
})().catch(e=>{console.error(e);process.exit(1);});
