(async()=>{
const wait=ms=>new Promise(r=>setTimeout(r,ms));let checks=0;
const state=()=>window.gl30Preview.state;
const check=(condition,message)=>{if(!condition)throw Error(message+' '+JSON.stringify(state()));checks++};
async function until(predicate,message){for(let i=0;i<150;i++){if(predicate())return;await wait(30)}throw Error(message+' '+JSON.stringify(state()))}
async function api(data){const r=await fetch('/input',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)});if(!r.ok)throw Error('API');await wait(90)}
async function press(hold=70){await window.gl30Preview.idle();document.dispatchEvent(new KeyboardEvent('keydown',{code:'Space',bubbles:true}));await window.gl30Preview.idle();await wait(hold);document.dispatchEvent(new KeyboardEvent('keyup',{code:'Space',bubbles:true}));await window.gl30Preview.idle();await wait(60)}
async function single(){await press();await wait(350)}
async function back(){await press();await wait(40);await press();await wait(110)}
const right=document.querySelector('[aria-label="向右旋转"]');
const left=document.querySelector('[aria-label="向左旋转"]');
document.querySelector('details').open=true;document.getElementById('restart').click();await until(()=>state()?.page===0&&state().remaining_ms===0,'reset');
await press();check(state().page===0,'single must wait for double window');await until(()=>state().page===1,'enter menu');check(state().menu===0,'menu starts at timer');
left.click();await until(()=>state().menu===8,'negative seam');check(state().menu_position<0,'continuous position negative');
for(let i=0;i<10;i++)right.click();await until(()=>state().menu===0,'positive loop');check(state().menu_position>=9,'winding preserved');
let lit=state().leds.filter(p=>p.some(Boolean));check(lit.length===1,'one menu LED');
await single();check(state().page===2&&state().app===0,'open timer');
await api({type:'rotate',steps:90});check(state().time==='01:30:00','HMS timer');check(Math.abs(state().angle-540)<.01,'90 min angle');
await back();check(state().page===1&&state().phase===0,'double must not start timer');
await single();await single();check(state().phase===1,'timer start');let before=state().remaining_ms;
left.click();await until(()=>state().remaining_ms<before-59000,'running manual reverse');check(state().phase===1,'reverse keeps running');
await back();await back();check(state().page===0&&state().phase===1,'background timer navigation');
await api({type:'advance',ms:60000});check(state().remaining_ms<before-119000,'background progress');
document.querySelector('[data-shortcut="3"]').click();await until(()=>state().off,'sleep');check(state().leds.every(p=>p.every(v=>v===0)),'sleep LEDs');
await press(710);check(state().off,'long does not wake');await single();check(!state().off&&state().page===0,'wake only');
document.querySelector('[data-shortcut="0"]').click();await until(()=>state().page===2&&state().app===0,'timer shortcut');
await api({type:'rotate',steps:-360});check(state().phase===3&&state().remaining_ms===0,'zero stop');
left.click();await until(()=>state().endstop===-1,'zero opposing intent');check(state().remaining_ms===0,'no negative time');
right.click();await until(()=>state().remaining_ms===60000,'forward from zero');check(state().phase===0,'restart setting');
document.querySelector('[data-shortcut="1"]').click();await until(()=>state().app===1,'volume shortcut');await api({type:'rotate',steps:-9});
check(state().volume===33&&Math.abs(state().angle-118.8)<.01,'volume aligned');await single();check(state().muted,'mute');right.click();await until(()=>!state().muted,'rotate unmute');
await back();right.click();await until(()=>state().menu===2,'stopwatch select');await single();await single();check(state().stopwatch_running,'stopwatch start');
await back();let elapsed=state().stopwatch_ms;await api({type:'advance',ms:1020});check(state().stopwatch_ms>=elapsed+1020,'stopwatch background');
await api({type:'rotate',steps:6});await single();check(state().app===8,'lighting open');await single();right.click();await until(()=>state().light_effect===1,'effect edit');
await back();check(state().page===2&&!state().light_editing,'back exits edit first');right.click();await until(()=>state().light_field===1,'color field');await single();right.click();await until(()=>state().light_color===1,'color edit');await single();
right.click();await until(()=>state().light_field===2,'brightness field');await single();await api({type:'rotate',steps:-20});check(state().light_brightness===0&&!state().off,'brightness zero keeps screen on');check(state().leds.every(p=>p.every(v=>v===0)),'brightness zero extinguishes LEDs');
await api({type:'rotate',steps:20});await back();await back();check(state().page===1,'lighting return menu');
check(state().leds.filter(p=>p.some(Boolean)).length===1,'ambient does not override menu pointer');
for(const app of [3,4,5,6,7]){await api({type:'rotate',steps:app-state().menu});await single();check(state().app===app,'utility '+app);check(state().display_error===0,'render '+app);await back()}
await api({type:'reset'});await until(()=>state().page===0,'finish home');
check(window.gl30Preview.frames>10,'C frames delivered');check(state().display_error===0,'no display error');
document.querySelector('details').open=false;
return {checks,frames:window.gl30Preview.frames,result:'PASS',scope:'DOM keyboard/shortcuts, HTTP bulk rotations/time advance, same native C model and renderer'};
})()
