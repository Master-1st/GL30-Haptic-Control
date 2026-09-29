'use strict';
const screen=document.getElementById('screen'), ctx=screen.getContext('2d');
const light=document.getElementById('light'), lctx=light.getContext('2d');
const pixels=ctx.createImageData(466,466), names=['计时器','音量','秒表','闹钟','天气','手感','设置','日历','灯效调整'];
let state=null, frameCount=0, chain=Promise.resolve();
window.gl30Preview={get state(){return state},get frames(){return frameCount},idle:()=>chain};
function send(data){chain=chain.catch(()=>{}).then(()=>fetch('/input',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)}).then(r=>{if(!r.ok)throw Error('input');return r.json()}));return chain}
function lightRing(s){
  lctx.clearRect(0,0,620,620);if(s.off||s.light_brightness===0||s.fault)return;
  const r=294,angle=(s.angle%360+360)%360,brightness=s.light_brightness/100;
  const palette=['#ff9b51','#ffd166','#91db9f','#c29bff','#f390b6','#e5e99a'];
  const stroke=(start,sweep,color,width=5)=>{if(!sweep)return;lctx.beginPath();lctx.lineWidth=width;lctx.lineCap='round';lctx.strokeStyle=color;lctx.shadowColor=color;lctx.shadowBlur=9;lctx.arc(310,310,r,(start-90)*Math.PI/180,(start+sweep-90)*Math.PI/180);lctx.stroke()};
  lctx.globalAlpha=brightness;
  const adjustable=s.page===2&&(s.app===0||s.app===1);
  if(s.page!==1&&!adjustable&&s.light_effect!==0){
    // Native C provides the 24 LED targets; interpolate the diffuser illustration.
    for(let i=0;i<24;i++){const rgb=s.leds[i];if(rgb.some(v=>v))stroke(i*15,15,`rgb(${rgb})`,5)}
  }else if(adjustable){let turns=s.angle/360,index=Math.max(0,Math.min(5,Math.ceil(turns)-1));stroke(0,Math.max(0,Math.min(1,turns-index))*360,palette[index]);}
  stroke(angle-1.9,3.8,'#55c9ff',6);lctx.shadowBlur=0;lctx.globalAlpha=1;
}
async function frame(){
 try{
   const response=await fetch('/frame'), buffer=await response.arrayBuffer(), view=new DataView(buffer);
   const n=view.getUint32(0,true);state=JSON.parse(new TextDecoder().decode(new Uint8Array(buffer,4,n)));
   const raw=new DataView(buffer,4+n);
   for(let i=0;i<466*466;i++){let p=raw.getUint16(i*2,true),j=i*4;pixels.data[j]=(p>>11)*255/31;pixels.data[j+1]=((p>>5)&63)*255/63;pixels.data[j+2]=(p&31)*255/31;pixels.data[j+3]=255}
   if(state.off){ctx.fillStyle='#000';ctx.fillRect(0,0,466,466)}else ctx.putImageData(pixels,0,0);
   screen.style.filter=`brightness(${state.screen_brightness/100})`;
   lightRing(state);frameCount++;
   document.getElementById('pageName').textContent=state.off?'屏幕休眠':state.page===0?'主页':state.page===1?'环形菜单':names[state.app];
   document.getElementById('value').textContent=state.page===1?names[state.menu]:state.page===2&&state.app===0?state.time:state.page===2&&state.app===1?state.volume+'%':state.page===2&&state.app===2?state.stopwatch:state.off?'轻按唤醒':'GL30';
   document.getElementById('feedback').textContent=state.endstop?'已到'+(state.endstop<0?'下':'上')+'限 · 力反馈意图':state.page===2&&state.app===0&&state.phase===1?'自动回旋 · 运行中可以手动调整':'单击确定，双击后退';
   document.getElementById('technical').textContent=`C frame ${state.frame_id} · display_error ${state.display_error} · target ${state.angle.toFixed(1)}°`;
   document.getElementById('connection').textContent='C RENDERER CONNECTED';
 }catch(e){document.getElementById('connection').textContent='预览服务未连接'}
 setTimeout(frame,33);
}
document.querySelectorAll('[data-rotate]').forEach(b=>b.addEventListener('click',()=>send({type:'rotate',steps:+b.dataset.rotate})));
document.querySelectorAll('[data-shortcut]').forEach(b=>b.addEventListener('click',()=>send({type:'shortcut',command:+b.dataset.shortcut})));
const press=document.getElementById('press');
press.addEventListener('pointerdown',e=>{e.preventDefault();press.setPointerCapture(e.pointerId);send({type:'button',pressed:true})});
press.addEventListener('pointerup',()=>send({type:'button',pressed:false}));
press.addEventListener('pointercancel',()=>send({type:'button',pressed:false}));
let pointer=null;
function touch(e,pressed){const r=screen.getBoundingClientRect();send({type:'touch',x:Math.round((e.clientX-r.left)*466/r.width),y:Math.round((e.clientY-r.top)*466/r.height),pressed})}
screen.addEventListener('pointerdown',e=>{e.preventDefault();pointer=e.pointerId;screen.setPointerCapture(pointer);touch(e,true)});
screen.addEventListener('pointermove',e=>{if(pointer===e.pointerId)touch(e,true)});
screen.addEventListener('pointerup',e=>{if(pointer===e.pointerId){touch(e,false);pointer=null}});
screen.addEventListener('pointercancel',e=>{if(pointer===e.pointerId){touch(e,false);pointer=null}});
screen.addEventListener('wheel',e=>{e.preventDefault();send({type:'rotate',steps:e.deltaY>0?1:-1})},{passive:false});
document.addEventListener('keydown',e=>{if(e.code==='Space'){e.preventDefault();if(!e.repeat)send({type:'button',pressed:true})}if(e.code==='ArrowLeft'||e.code==='ArrowRight'){e.preventDefault();send({type:'rotate',steps:e.code==='ArrowRight'?1:-1})}});
document.addEventListener('keyup',e=>{if(e.code==='Space'){e.preventDefault();send({type:'button',pressed:false})}});
window.addEventListener('blur',()=>send({type:'button',pressed:false}));
document.getElementById('advance').addEventListener('click',()=>send({type:'advance',ms:60000}));
document.getElementById('restart').addEventListener('click',()=>send({type:'reset'}));
frame();
