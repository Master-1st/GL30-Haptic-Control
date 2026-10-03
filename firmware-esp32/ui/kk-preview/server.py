"""Loopback preview of the same compiled C UI used by the ESP32 firmware.

The browser only sends input and displays RGB565 frames. No duplicate JS model.
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import struct
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE=Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('--port',type=int,default=8770)
args=parser.parse_args()
library=HERE/'build/gl30_preview.dll'
if not library.exists():
    library=HERE/'build/libgl30_preview.so'
ui=C.CDLL(str(library))
ui.gl30_demo_init.argtypes=[C.c_uint32,C.c_uint64]
ui.gl30_demo_init.restype=C.c_bool
ui.gl30_demo_sample.argtypes=[C.c_uint32]
ui.gl30_demo_render.argtypes=[C.c_uint32]
ui.gl30_demo_rotate.argtypes=[C.c_int16]
ui.gl30_demo_button.argtypes=[C.c_bool,C.c_uint32]
ui.gl30_demo_touch.argtypes=[C.c_int16,C.c_int16,C.c_bool,C.c_uint32]
ui.gl30_demo_shortcut.argtypes=[C.c_int]
ui.gl30_demo_json.restype=C.c_char_p
ui.gl30_host_frame.restype=C.POINTER(C.c_uint16)
ui.gl30_host_frame_id.restype=C.c_uint32
lock=threading.Lock()
origin=time.monotonic()
advance=0
def now(): return (int((time.monotonic()-origin)*1000)+advance)&0xffffffff
def update_frame():
    at=now()
    ui.gl30_demo_sample(at)
    ui.gl30_demo_render(at)
def reset():
    # Clock display is explicit UTC+8 for this Chinese demo; no weather network.
    if not ui.gl30_demo_init(now(),int(time.time())):
        raise RuntimeError('KK display/UI initialization failed')
reset()
def tick():
    while True:
        before=time.monotonic()
        with lock: update_frame()
        time.sleep(max(0.002,0.010-(time.monotonic()-before)))
threading.Thread(target=tick,daemon=True).start()

class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args): pass
    def send(self,body,kind='application/json',code=200):
        self.send_response(code)
        self.send_header('Content-Type',kind)
        self.send_header('Content-Length',str(len(body)))
        self.send_header('Cache-Control','no-store')
        self.send_header('X-Content-Type-Options','nosniff')
        self.end_headers()
        try: self.wfile.write(body)
        except (BrokenPipeError,ConnectionResetError): pass
    def do_GET(self):
        path=self.path.split('?')[0]
        if path=='/frame':
            with lock:
                state=json.loads(ui.gl30_demo_json())
                state['frame_id']=ui.gl30_host_frame_id()
                header=json.dumps(state,separators=(',',':')).encode()
                frame=C.string_at(ui.gl30_host_frame(),466*466*2)
            self.send(struct.pack('<I',len(header))+header+frame,'application/octet-stream')
        elif path=='/state':
            with lock: data=ui.gl30_demo_json()
            self.send(data)
        elif path in ('/','/index.html','/preview.js','/style.css'):
            file=HERE/('index.html' if path=='/' else path[1:])
            kind={'html':'text/html; charset=utf-8','js':'text/javascript; charset=utf-8','css':'text/css; charset=utf-8'}[file.suffix[1:]]
            self.send(file.read_bytes(),kind)
        else: self.send(b'{}',code=404)
    def do_POST(self):
        global advance
        # Only same-origin loopback controls; reject cross-site browser requests.
        origin_header=self.headers.get('Origin')
        if origin_header and origin_header not in (f'http://127.0.0.1:{args.port}',f'http://localhost:{args.port}'):
            self.send(b'{"error":"origin"}',code=403); return
        try:
            length=int(self.headers.get('Content-Length','0'))
            if not 0<length<=1024: raise ValueError('size')
            data=json.loads(self.rfile.read(length))
            if self.path!='/input': raise ValueError('path')
            with lock:
                update_frame()
                action=data['type']
                if action=='rotate':
                    ui.gl30_demo_rotate(max(-360,min(360,int(data['steps']))))
                elif action=='button': ui.gl30_demo_button(bool(data['pressed']),now())
                elif action=='touch':
                    x=max(-100,min(566,int(data['x']))); y=max(-100,min(566,int(data['y'])))
                    ui.gl30_demo_touch(x,y,bool(data['pressed']),now())
                elif action=='shortcut':
                    command=int(data['command'])
                    if command not in range(4): raise ValueError('command')
                    ui.gl30_demo_shortcut(command)
                elif action=='advance':
                    advance+=max(0,min(21600000,int(data['ms'])))
                elif action=='reset': reset()
                else: raise ValueError('type')
                update_frame()
                result=ui.gl30_demo_json()
            self.send(result)
        except (ValueError,KeyError,TypeError,json.JSONDecodeError): self.send(b'{"error":"invalid input"}',code=400)

print(f'GL30 shared C preview: http://127.0.0.1:{args.port}',flush=True)
ThreadingHTTPServer(('127.0.0.1',args.port),Handler).serve_forever()
