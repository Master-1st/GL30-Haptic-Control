'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
require('./gesture.js');const G=globalThis.GL30Gesture;
const down=(p,t)=>{assert.equal(G.sample(p,true,t),null);return G.sample(p,true,t+20);};
const up=(p,t)=>{assert.equal(G.sample(p,false,t),null);return G.sample(p,false,t+20);};
test('single waits for a stable release and the full double window',()=>{
  const p=G.create();assert.equal(down(p,10),null);assert.equal(up(p,80),null);
  assert.equal(G.sample(p,false,379),null);assert.equal(G.sample(p,false,380),'single');assert.equal(G.sample(p,false,1000),null);
});
test('double emits only back and never a preliminary or trailing single',()=>{
  const p=G.create();down(p,10);up(p,80);down(p,180);assert.equal(up(p,240),'double');
  assert.equal(G.sample(p,false,1000),null);
});
test('second raw down at the double deadline suspends single while debouncing',()=>{
  for(const secondAt of [379,380]){
    const p=G.create();down(p,10);up(p,80);assert.equal(G.sample(p,true,secondAt),null);
    assert.equal(G.sample(p,true,secondAt+19),null);assert.equal(G.sample(p,true,secondAt+20),null);
    assert.equal(up(p,500),'double');assert.equal(G.sample(p,false,1000),null);
  }
});
test('second down outside the window cannot turn a single into a double',()=>{
  const p=G.create();down(p,10);up(p,80);assert.equal(G.sample(p,false,380),'single');
  down(p,381);assert.equal(up(p,440),null);assert.equal(G.sample(p,false,740),'single');
});
test('long emits once while held, consumes release and never falls back to a single',()=>{
  const p=G.create();down(p,10);assert.equal(G.sample(p,true,659),null);assert.equal(G.sample(p,true,660),'long');
  assert.equal(G.sample(p,true,3000),null);assert.equal(up(p,3100),null);assert.equal(G.sample(p,false,5000),null);
});
test('release before long threshold is short even when release debounce crosses threshold',()=>{
  const p=G.create();down(p,10);assert.equal(G.sample(p,false,659),null);assert.equal(G.sample(p,false,678),null);
  assert.equal(G.sample(p,false,679),null);assert.equal(G.sample(p,false,959),'single');
});
test('release at long threshold works even without intermediate samples',()=>{
  const p=G.create();down(p,10);assert.equal(up(p,660),'long');assert.equal(G.sample(p,false,1000),null);
});
test('short first click followed by a long second press consumes both single and double',()=>{
  const p=G.create();down(p,10);up(p,80);down(p,180);assert.equal(G.sample(p,true,700),null);
  assert.equal(G.sample(p,true,830),'long');assert.equal(up(p,900),null);assert.equal(G.sample(p,false,1500),null);
});
test('startup held release arms input without producing an action',()=>{
  const p=G.create(true,0);assert.equal(G.sample(p,true,2000),null);assert.equal(up(p,2100),null);
  assert.equal(G.sample(p,false,3000),null);down(p,3100);up(p,3180);assert.equal(G.sample(p,false,3480),'single');
});
test('contact bounce below twenty milliseconds cannot make a click',()=>{
  const p=G.create();G.sample(p,true,10);G.sample(p,false,29);assert.equal(G.sample(p,false,49),null);
  assert.equal(G.sample(p,false,1000),null);
});
test('release bounce does not duplicate a completed press',()=>{
  const p=G.create();down(p,10);G.sample(p,false,80);G.sample(p,true,90);G.sample(p,false,100);
  assert.equal(G.sample(p,false,120),null);assert.equal(G.sample(p,false,400),'single');assert.equal(G.sample(p,false,1000),null);
});
test('a bouncing second down does not swallow the first single indefinitely',()=>{
  const p=G.create();down(p,10);up(p,80);G.sample(p,true,379);G.sample(p,false,389);
  assert.equal(G.sample(p,false,410),null); // The due single was emitted at the raw release.
  const q=G.create();down(q,10);up(q,80);G.sample(q,true,379);assert.equal(G.sample(q,false,389),'single');
});
test('recreating the gate on context change cancels pending clicks and held releases',()=>{
  let p=G.create();down(p,10);up(p,80);p=G.create(false,100);assert.equal(G.sample(p,false,1000),null);
  down(p,1100);p=G.create(false,1200);assert.equal(G.sample(p,false,1300),null);assert.equal(G.sample(p,false,2000),null);
});
test('invalid and backward timestamps do not change pending classification',()=>{
  const p=G.create();down(p,10);up(p,80);const before=JSON.stringify(p);
  for(const now of [NaN,Infinity,99])assert.equal(G.sample(p,true,now),null);
  assert.equal(JSON.stringify(p),before);assert.equal(G.sample(p,false,380),'single');
});
test('semantic accessibility clicks use the same exclusive single and double window',()=>{
  const p=G.create();assert.equal(G.click(p,10),null);assert.equal(G.sample(p,false,309),null);
  assert.equal(G.click(p,310),'double');assert.equal(G.sample(p,false,1000),null);
  assert.equal(G.click(p,1100),null);assert.equal(G.sample(p,false,1400),'single');
});
test('a delayed semantic event settles the previous single instead of losing it',()=>{
  const p=G.create();G.click(p,10);assert.equal(G.click(p,311),'single');assert.equal(G.sample(p,false,1000),null);
});
