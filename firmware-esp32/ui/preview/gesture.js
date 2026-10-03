'use strict';

// Mutually exclusive single / double / unassigned-long gestures.
// Waiting for a second click avoids executing a single and trying to undo it.
globalThis.GL30Gesture=(()=>{
  const DEBOUNCE=20,DOUBLE=300,LONG=650;
  function create(initialDown=false,now=0){
    return {raw:initialDown,stable:initialDown,since:now,last:now,armed:!initialDown,
      downAt:null,pending:null,second:false,long:false};
  }
  function click(p,now){
    if(!Number.isFinite(now)||now<p.last)return null;
    p.last=now;
    if(p.pending!==null){const event=now-p.pending<=DOUBLE?'double':'single';p.pending=null;return event;}
    p.pending=now;return null;
  }
  function sample(p,down,now){
    if(!Number.isFinite(now)||now<p.last)return null;
    p.last=now;down=Boolean(down);
    if(down!==p.raw){p.raw=down;p.since=now;}
    if(p.stable!==p.raw&&now-p.since>=DEBOUNCE){
      p.stable=p.raw;
      if(p.stable){
        p.downAt=p.since;p.long=false;
        p.second=p.pending!==null&&p.downAt-p.pending<=DOUBLE;
      }else{
        if(!p.armed){p.armed=true;p.downAt=null;p.pending=null;return null;}
        if(p.downAt!==null&&!p.long){
          if(p.since-p.downAt>=LONG){p.pending=null;p.downAt=null;p.second=false;return 'long';}
          if(p.second){p.pending=null;p.downAt=null;p.second=false;return 'double';}
          p.pending=p.since;
        }
        p.downAt=null;p.second=false;
      }
    }
    if(p.armed&&p.raw&&p.stable&&p.downAt!==null&&!p.long&&now-p.downAt>=LONG){
      p.long=true;p.pending=null;return 'long';
    }
    // Raw second-down inside the window also suspends timeout while debouncing.
    const candidate=p.raw&&p.pending!==null&&p.since<=p.pending+DOUBLE;
    if(p.pending!==null&&!p.second&&!candidate&&now-p.pending>=DOUBLE){
      p.pending=null;return 'single';
    }
    return null;
  }
  return {DEBOUNCE,DOUBLE,LONG,create,click,sample};
})();
