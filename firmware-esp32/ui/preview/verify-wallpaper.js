'use strict';
(async()=>{
  const $=id=>document.getElementById(id),checks=[],wait=ms=>new Promise(r=>setTimeout(r,ms));
  const check=(ok,name)=>{if(!ok)throw Error(name);checks.push(name);};
  const visible=el=>el.getClientRects().length>0&&getComputedStyle(el).display!=='none';
  const backdrop=$('homeBackdrop'),menu=$('menuScreen');
  check($('stage').dataset.page==='home','wallpaper acceptance starts on home');
  const source=new URL(backdrop.getAttribute('href'),location.href);
  check(source.href.includes('/preview/assets/')&&!source.href.includes('generated_images'),'wallpaper source is part of the portable project');
  const img=new Image();img.src=source.href;await img.decode();
  check(img.naturalWidth>=466&&img.naturalHeight>=466,'wallpaper supplies at least one source pixel per 466px panel pixel');
  check(img.naturalWidth===img.naturalHeight,'square wallpaper needs no anisotropic stretch for the circular face');
  check(!menu.querySelector('image')&&!$('menuBackdrop'),'menu contains no wallpaper while home retains the landscape');
  check($('display').contains(backdrop)&&$('display').getAttribute('clip-path')==='url(#screen-clip)','image and text share the existing round glass clip');
  check(!$('rotor').contains(backdrop)&&!$('rotor').contains($('homeTime')),'wallpaper and clock are fixed to the screen, outside the rotating rim');
  check(Number(backdrop.getAttribute('width'))>=294&&Number(backdrop.getAttribute('height'))>=294,'landscape covers the full screen area');
  check($('homeWeather').textContent.includes('示例'),'weather remains explicitly sample data');
  for(const id of ['homeTime','homeDate','homeSeconds','homeWeather','homeTimerText']){
    const r=$(id).getBBox();
    check(visible($(id))&&[[r.x,r.y],[r.x+r.width,r.y],[r.x,r.y+r.height],[r.x+r.width,r.y+r.height]].every(([x,y])=>Math.hypot(x-340,y-275)<147),id+' is visible and entirely inside the circular screen');
  }
  const before=$('homeSeconds').textContent;await wait(1100);check($('homeSeconds').textContent!==before,'seconds are live text rather than baked into the photograph');
  $('power').click();check(!visible($('display'))&&$('ledRing').dataset.visible==='false','off removes the entire photograph text and light display');
  $('primary').click();await wait(360);check($('stage').dataset.page==='home'&&visible($('display')),'wake returns to the photo home without opening menu');
  $('primary').click();await wait(360);check($('stage').dataset.page==='menu'&&visible(menu)&&!visible(backdrop),'black menu hides the home photograph');
  $('primary').click();await wait(360);check($('stage').dataset.page==='app'&&!visible(backdrop)&&!visible(menu),'timer app retains its clear dedicated screen');
  check(document.documentElement.scrollWidth===innerWidth,'wallpaper layouts have no horizontal overflow');
  return {passed:checks.length,checks,image:{width:img.naturalWidth,height:img.naturalHeight},scope:'browser photograph and SVG composition; no physical display qualification'};
})()
