'use strict';
(async()=>{
  const $=id=>document.getElementById(id),checks=[],wait=ms=>new Promise(r=>setTimeout(r,ms));
  const check=(ok,name)=>{if(!ok)throw Error(name);checks.push(name);};
  const click=id=>$(id).click(),single=async()=>{click('primary');await wait(360);};
  const double=async()=>{click('primary');await wait(90);click('primary');await wait(360);};
  const page=()=>$('stage').dataset.page,mode=()=>$('stage').dataset.mode,selection=()=>Number($('stage').dataset.selection);
  const near=(a,b,t=.01)=>Math.abs(a-b)<=t,shown=id=>getComputedStyle($(id)).display!=='none';
  const key=key=>{for(const type of ['keydown','keyup'])$('knobControl').dispatchEvent(new KeyboardEvent(type,{key,code:key,bubbles:true}));};
  const range=v=>{$('target').value=v;$('target').dispatchEvent(new Event('input',{bubbles:true}));};
  const pointerOnly=()=>['lightPrevious','lightTrail'].every(id=>Number($(id).getAttribute('opacity'))===0)&&Number($('lightHead').getAttribute('opacity'))===1&&$('lightHead').getAttribute('stroke')==='#55c9ff';
  const appIds=GL30.APPS.map(app=>app.id),count=appIds.length,step=GL30.MENU_STEP,lastId=appIds.at(-1);
  const menuId=id=>'menu'+id[0].toUpperCase()+id.slice(1);
  const settle=async target=>{const deadline=performance.now()+3000;while(!near(Number($('menuOrbit').dataset.position),target,.001)&&performance.now()<deadline)await wait(25);};
  const bounds=id=>{const r=$(id).getBBox();return [[r.x,r.y],[r.x+r.width,r.y],[r.x,r.y+r.height],[r.x+r.width,r.y+r.height]].every(([x,y])=>Math.hypot(x-340,y-275)<147);};
  check(page()==='home','fresh launch retains the photo watch home');await single();
  check(page()==='menu'&&count===9&&$('menuOrbit').children.length===count&&$('menuChoices').children.length===count,'home confirmation opens nine distinct ring applications');
  check(!document.querySelector('#menuScreen image')&&['rgb(0, 0, 0)','rgb(2, 4, 6)'].includes(getComputedStyle(document.querySelector('.menu-surface')).fill),'menu is black without a background photograph');
  check(pointerOnly(),'menu displays only the blue pointer without previous or current trails');
  const front=$('svgMenuTimer'),back=$('svgMenuWeather');
  check(Number(front.dataset.scale)>2*Number(back.dataset.scale)&&$('menuOrbit').lastElementChild===front,'front item is more than twice the rear scale and is painted last');
  check(bounds('menuName')&&bounds('menuCaption'),'selected menu labels fit inside the circular screen');
  key('ArrowLeft');await new Promise(resolve=>requestAnimationFrame(resolve));
  const visual=Number($('menuOrbit').dataset.position),head=Number($('lightHead').dataset.angle);
  check(visual<0&&visual>-1&&near(head,GL30.wrap(visual*step))&&near(Number($('rotor').dataset.position),visual*step),'reverse animation pointer rim and orbit share the same continuous position');
  await wait(420);check(selection()===count-1&&$('menuName').textContent===GL30.APPS.at(-1).label,'reverse from first reaches the last application');
  key('ArrowRight');await wait(500);check(selection()===0,'forward from last returns to first');
  for(let i=0;i<count*5;i++)key('ArrowRight');await settle(count*5);
  check(selection()===0&&Number($('menuOrbit').dataset.position)>count*5-.01,'five forward turns preserve winding count without reaching an endstop');
  for(let i=0;i<count*6;i++)key('ArrowLeft');await settle(-count);
  check(selection()===0&&Number($('menuOrbit').dataset.position)<-count+.01,'six reverse turns pass through zero without losing motion');
  check(pointerOnly()&&$('stage').dataset.endstop==='','multiple menu turns never create a colored trail or endstop');
  click(menuId(lastId));check(selection()===count-1&&near(Number($('menuOrbit').dataset.position),-count-1),'direct selection crosses the seam by one slot and settles visibly before confirmation');
  check($('menuOrbit').lastElementChild===$('svgMenu'+lastId[0].toUpperCase()+lastId.slice(1)),'direct selection brings its actual icon to the front');
  click('menuTimer');const still=page();$('glass').dispatchEvent(new MouseEvent('click',{bubbles:true}));await wait(360);
  check(page()===still,'fixed central glass remains inert in the orbit menu');
  for(const [i,id] of appIds.entries()){
    click(menuId(id));await single();
    check(page()==='app'&&mode()===id&&shown(i<2?'appScreen':'utilityScreen')&&!shown('menuScreen'),id+' opens the correct dedicated view');
    if(i>=2){
      check(['utilityTitle','utilityValue','utilityDetail','utilityHint'].every(bounds),id+' labels remain inside the circular display');
      check($('appControls').hidden&&$('target').disabled&&$('sidePlus').disabled,id+' does not expose timer or volume adjustment controls');
    }
    if(['alarm','haptics','settings'].includes(id)){
      const before=$('stage').dataset.phase;await single();key('ArrowRight');
      check($('utilityDetail').textContent.includes('占位')&&$('stage').dataset.phase===before&&!$('stage').classList.contains('muted'),id+' is explicitly a placeholder and cannot activate a hidden timer or volume');
    }
    if(id==='weather')check($('utilityDetail').textContent.includes('示例')&&$('utilityHint').textContent.includes('尚未连接'),'weather is explicitly sample data');
    if(id==='calendar')check($('utilityValue').textContent===`${new Date().getMonth()+1}月${new Date().getDate()}日`,'calendar displays the actual local date');
    await double();check(page()==='menu'&&selection()===i&&pointerOnly(),id+' double returns to its selected icon and restores pointer-only lighting');
  }
  click('menuStopwatch');await single();await single();await wait(130);
  check(/^\d{2}:\d{2}:\d{2}\.\d{2}$/.test($('utilityValue').textContent)&&$('utilityDetail').textContent==='正在计时','stopwatch starts and displays hours minutes seconds and hundredths');
  const first=$('utilityValue').textContent;await double();await wait(400);await single();
  check(mode()==='stopwatch'&&$('utilityDetail').textContent==='正在计时'&&$('utilityValue').textContent>first,'double back does not pause the stopwatch and background time is retained');
  await single();const paused=$('utilityValue').textContent;await wait(180);
  check($('utilityDetail').textContent==='已暂停'&&$('utilityValue').textContent===paused,'stopwatch single pause freezes elapsed time');
  click('reset');check($('utilityValue').textContent==='00:00:00.00'&&$('utilityDetail').textContent==='准备就绪','stopwatch reset clears only its elapsed time and running state');
  await single();click('power');await wait(280);await single();
  check($('stage').dataset.off==='false'&&mode()==='stopwatch'&&$('utilityDetail').textContent==='正在计时','first off-screen single only wakes the still-running stopwatch');
  click('fault');const faultTime=$('utilityValue').textContent;await wait(220);click('fault');await wait(80);
  check($('utilityValue').textContent===faultTime&&$('utilityDetail').textContent==='已暂停','fault pauses stopwatch and clearing it does not auto-resume');
  click('reset');click('sideTimer');click('reset');range(1);await single();click('navBack');click('menuStopwatch');await single();await single();
  check($('stage').dataset.phase==='running'&&!$('backgroundTimer').hidden&&$('utilityDetail').textContent==='正在计时','countdown and stopwatch can run independently at the same time');
  const timerBefore=$('backgroundTime').textContent;click('reset');await wait(1100);
  check($('utilityValue').textContent==='00:00:00.00'&&$('stage').dataset.phase==='running'&&$('backgroundTime').textContent!==timerBefore,'resetting stopwatch preserves the running background countdown');
  click('sideTimer');click('reset');click('navHome');
  check(shown('homeScreen')&&!shown('utilityScreen')&&$('homeBackdrop').getAttribute('href').includes('summit-dawn'),'return home retains the landscape face and hides utility content');
  check(document.documentElement.scrollWidth===innerWidth,'expanded menu and utility panels have no horizontal overflow');
  return {passed:checks.length,checks,scope:'browser orbit menu, stopwatch and explicit information placeholders; no hardware or live weather'};
})()
