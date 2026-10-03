"""Compare complete native-RGB565 host presentations for identical C model snapshots.
Requires two already-built preview libraries; no board access or motor commands.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path


def load(path):
    lib=C.CDLL(str(Path(path).resolve()))
    signatures={
        'gl30_demo_init':([C.c_uint32,C.c_uint64],C.c_bool),
        'gl30_demo_state':([],C.c_void_p),
        'gl30_demo_sample':([C.c_uint32],None),
        'gl30_demo_rotate':([C.c_int16],None),
        'gl30_demo_button':([C.c_bool,C.c_uint32],None),
        'gl30_demo_shortcut':([C.c_int],None),
        'gl30_demo_json':([],C.c_char_p),
        'gl30_draw_scene':([C.c_void_p,C.c_int16,C.c_int16,C.c_uint16],None),
        'OLED_SetColor':([C.c_uint16,C.c_uint16],None),
        'OLED_Clear':([],None),'OLED_Update':([],C.c_int),
        'gl30_host_frame':([],C.c_void_p),
    }
    for name,(args,result) in signatures.items():
        fn=getattr(lib,name);fn.argtypes=args;fn.restype=result
    return lib


def run(before,after):
    ref,new=load(before),load(after)
    assert ref.gl30_demo_init(0,1800000000) and new.gl30_demo_init(0,1800000000)
    state=ref.gl30_demo_state(); records=[]
    def capture(name):
        images=[]
        for lib in (ref,new):
            lib.OLED_SetColor(0xffff,0);lib.OLED_Clear()
            lib.gl30_draw_scene(state,0,0,466)
            assert lib.OLED_Update()==0
            images.append(C.string_at(lib.gl30_host_frame(),466*466*2))
        assert images[0]==images[1], 'Pixel mismatch: '+name
        records.append(dict(scene=name,sha256=hashlib.sha256(images[0]).hexdigest()))
    now=0
    def advance(ms):
        nonlocal now
        now+=ms;ref.gl30_demo_sample(now)
    def button(value):
        nonlocal now
        now+=30;ref.gl30_demo_button(value,now);advance(30)
    def click():
        button(True);button(False);advance(330)
    def back():
        for value in (True,False,True,False):button(value)
        advance(100)
    capture('home');click();capture('menu-open')
    for index in range(9):
        ref.gl30_demo_rotate(1)
        for step in range(1,7):
            advance(40);capture(f'menu-{index}-phase-{step}')
    for app in range(9):
        # Isolate each app: the running timer in one case must not navigate
        # the next case asynchronously through its DONE state.
        assert ref.gl30_demo_init(0,1800000000)
        now=0;click()
        current=json.loads(ref.gl30_demo_json())
        assert current['page']==1, (app,current)
        ref.gl30_demo_rotate(app-current['menu']);advance(250);click()
        current=json.loads(ref.gl30_demo_json());assert current['app']==app and current['page']==2
        capture(f'app-{app}')
        ref.gl30_demo_rotate(3);advance(40);capture(f'app-{app}-rotate')
        click();capture(f'app-{app}-primary')
        for offset in range(1,4):
            advance(100);capture(f'app-{app}-time-{offset}')
        back()
    current=json.loads(ref.gl30_demo_json())
    ref.gl30_demo_rotate(8-current['menu']);advance(250);click();click()
    ref.gl30_demo_rotate(3)
    for index in range(10):
        advance(100);capture(f'lighting-phase-{index}')
    ref.gl30_demo_shortcut(3);capture('power-off')
    ref.gl30_demo_shortcut(3);capture('power-restored')
    return dict(passed=True,frames=len(records),bytes_per_frame=434312,records=records)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before',required=True)
    parser.add_argument('--after',required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    result=run(args.before,args.after)
    args.output.write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(f"{result['frames']} complete frames byte-for-byte identical")
