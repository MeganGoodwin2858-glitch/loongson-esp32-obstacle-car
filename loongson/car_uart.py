"""Loongson/Linux UART adapter, not an obstacle detection algorithm.

Default: monitor only. --enable-motion accepts fresh JSON decisions on stdin.
No device is opened when this module is imported or tested.
"""
import argparse
import json
import math
import select
import struct
import sys
import time

def crc(data):
    value=0xffff
    for byte in data:
        value^=byte<<8
        for _ in range(8):
            value=((value<<1)^0x1021 if value&0x8000 else value<<1)&0xffff
    return value

def encode(op,speed,seq,x,y,session,arg=0):
    values=(op,speed,seq,x,y,session,arg)
    if not all(isinstance(v,int) and not isinstance(v,bool) for v in values):
        raise ValueError('integer fields required')
    if not (1<=op<=6 and 0<=speed<=100 and 1<=seq<=0xffffffff and
            -1000<=x<=1000 and -1000<=y<=1000 and 1<=session<=0xffffffff):
        raise ValueError('field out of range')
    if (op!=1 and (x or y)) or (op==6 and not 0<=arg<=2) or (op!=6 and arg):
        raise ValueError('invalid operation arguments')
    data=struct.pack('<BBBBIhhIBB',0xa5,1,op,speed,seq,x,y,session,arg,0)
    return data+struct.pack('<H',crc(data))

def parse_status(data):
    if len(data)!=20 or data[:2]!=b'\x5a\x01' or data[2]>2 or crc(data[:18])!=int.from_bytes(data[18:],'little'):
        raise ValueError('invalid status frame')
    mode,flags,session,auto_session,left,right=struct.unpack_from('<BBIIhh',data,2)
    return dict(mode=mode,flags=flags,session=session,auto_session=auto_session,left=left,right=right)

def validate_decision(decision,now):
    stamp=decision.get('monotonic')
    if isinstance(stamp,bool) or not isinstance(stamp,(int,float)) or not math.isfinite(stamp) or not 0<=now-stamp<=0.15:
        raise ValueError('stale or future decision')
    values=(decision.get('x'),decision.get('y'),decision.get('speed'))
    if not all(isinstance(v,int) and not isinstance(v,bool) for v in values):
        raise ValueError('integer motion fields required')
    x,y,speed=values
    if not (-1000<=x<=1000 and -1000<=y<=1000 and 0<=speed<=100):
        raise ValueError('motion out of range')
    return values

def read_status(port,timeout=0.5):
    deadline=time.monotonic()+timeout
    buffer=bytearray()
    while time.monotonic()<deadline:
        buffer.extend(port.read(20))
        while buffer:
            if buffer[0]!=0x5a:
                del buffer[0]
                continue
            if len(buffer)<20:
                break
            try:
                return parse_status(buffer[:20])
            except ValueError:
                del buffer[0]
    raise TimeoutError('no valid ESP32 status')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--enable-motion',action='store_true')
    args=parser.parse_args()
    # Install pyserial on the Loongson separately. Tests use the stdlib only.
    import serial
    seq=0
    last_status=None
    with serial.Serial(args.port,115200,timeout=0.03,write_timeout=0.1) as port:
        def send(op,x=0,y=0,speed=0):
            nonlocal seq
            seq+=1
            port.write(encode(op,speed,seq,x,y,last_status['auto_session']))
        try:
            while True:
                port.reset_input_buffer() # discard queued historical status before handshaking
                last_status=read_status(port)
                if not args.enable_motion:
                    print(json.dumps(last_status),flush=True)
                    continue
                if last_status['mode']!=1 or not (last_status['flags']&4) or last_status['flags']&2:
                    raise RuntimeError('select Auto on the connected phone; emergency stop must be clear')
                token=last_status['auto_session']
                send(3) # only after explicit --enable-motion and phone Auto selection
                while True:
                    if not select.select([sys.stdin],[],[],0.15)[0]:
                        raise TimeoutError('avoidance input stale; stopping')
                    line=sys.stdin.readline()
                    if not line:
                        raise EOFError('avoidance producer closed')
                    decision=json.loads(line)
                    # Linux monotonic timestamp must be supplied by the sensor/decision process.
                    validate_decision(decision,time.monotonic())
                    last_status=read_status(port)
                    if last_status['mode']!=1 or last_status['auto_session']!=token or not(last_status['flags']&1):
                        raise RuntimeError('mode/session/arming changed; explicit restart required')
                    # Recheck freshness after waiting for the UART status.
                    send(1,*validate_decision(decision,time.monotonic()))
        except (KeyboardInterrupt,Exception) as error:
            print(f'Stopped: {error}',file=sys.stderr)
        finally:
            if args.enable_motion and last_status:
                try:send(2)
                except Exception:pass # ESP32's 300ms watchdog remains the fallback.

if __name__=='__main__':
    main()
