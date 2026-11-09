(function(root){
'use strict';
const SERVICE='7bc03600-186c-4a8d-4245-363600000001';
const COMMAND='7bc03600-186c-4a8d-4245-363600000002';
const STATUS='7bc03600-186c-4a8d-4245-363600000003';
function crc(bytes){let c=0xffff;for(const b of bytes){c^=b<<8;for(let i=0;i<8;i++)c=((c&0x8000)?(c<<1)^0x1021:c<<1)&0xffff;}return c;}
function frame(op,speed,seq,x,y,session,arg=0){
  if(![op,speed,seq,x,y,session,arg].every(Number.isInteger)||op<1||op>6||speed<0||speed>100||
     x < -1000||x>1000||y < -1000||y>1000||seq<1||seq>0xffffffff||session<1||session>0xffffffff||
     (op!==1&&(x!==0||y!==0))||(op===6?arg<0||arg>2:arg!==0))throw Error('控制参数无效');
  const b=new Uint8Array(20),v=new DataView(b.buffer);b.set([0xa5,1,op,speed]);v.setUint32(4,seq,true);
  v.setInt16(8,x,true);v.setInt16(10,y,true);v.setUint32(12,session,true);b[16]=arg;v.setUint16(18,crc(b.subarray(0,18)),true);return b;
}
function parseStatus(value){
  const b=value instanceof DataView?new Uint8Array(value.buffer,value.byteOffset,value.byteLength):new Uint8Array(value);
  if(b.length!==20||b[0]!==0x5a||b[1]!==1||b[2]>2)throw Error('状态帧无效');
  const v=new DataView(b.buffer,b.byteOffset,b.byteLength);
  if(v.getUint16(18,true)!==crc(b.subarray(0,18)))throw Error('状态校验失败');
  return {mode:b[2],flags:b[3],session:v.getUint32(4,true),autoSession:v.getUint32(8,true),left:v.getInt16(12,true),right:v.getInt16(14,true)};
}
// One outstanding GATT operation, one latest motion, and a bounded control queue.
class Sender {
  constructor(write,read,clock=()=>Date.now()){
    this.write=write;this.read=read;this.clock=clock;this.seq=0;this.queue=[];this.pending=null;
    this.busy=false;this.closed=false;this.state=null;this.running=Promise.resolve();this.onstate=()=>{};this.onerror=()=>{};
  }
  drive(x,y,speed){if(this.closed)return;this.pending={x,y,speed,time:this.clock()};this.kick();}
  control(op,arg=0){
    if(this.closed)return Promise.reject(Error('连接已关闭'));
    this.pending=null;
    // An emergency cannot be displaced by a later release, arm or mode click.
    if(this.queue.some(q=>q.op===4))return Promise.resolve({cancelled:true});
    if(op===4||op===2){for(const q of this.queue)q.resolve({cancelled:true});this.queue=[];}
    if(this.queue.length>=4)return Promise.reject(Error('操作过快，请等待'));
    const p=new Promise((resolve,reject)=>this.queue.push({op,arg,resolve,reject}));this.kick();return p;
  }
  refresh(){if(this.busy||this.closed)return this.running;this.queue.push({op:0,resolve:()=>{},reject:()=>{}});this.kick();return this.running;}
  kick(){if(this.busy)return;this.busy=true;this.running=this.pump();}
  async pump(){
    let current;
    try{
      while(!this.closed&&(this.queue.length||this.pending)){
        current=this.queue.shift();let motion;
        if(!current){motion=this.pending;this.pending=null;if(this.clock()-motion.time>150)continue;}
        this.state=await this.read();this.onstate(this.state);
        // Release/mode changes that arrived during the read cancel that motion.
        if(motion&&this.queue.length)continue;
        if(motion&&this.clock()-motion.time>150)continue;
        const op=current?current.op:1;
        if(op){const b=frame(op,motion?motion.speed:40,++this.seq,motion?motion.x:0,motion?motion.y:0,this.state.session,current?current.arg:0);
          await this.write(b);this.state=await this.read();this.onstate(this.state);}
        if(current)current.resolve(this.state);current=null;
      }
    }catch(e){
      if(current)current.reject(e);
      this.closed=true;this.pending=null;for(const q of this.queue)q.reject(e);this.queue=[];this.onerror(e);
    }finally{this.busy=false;}
  }
  idle(){return this.running;}
  close(){this.closed=true;this.pending=null;for(const q of this.queue)q.reject(Error('连接已关闭'));this.queue=[];}
}
const api={frame,parseStatus,Sender,crc};if(typeof module!=='undefined')module.exports=api;
if(typeof document==='undefined')return;
let device,sender,held=false,pointer=null,x=0,y=0;
const $=id=>document.getElementById(id),pad=$('pad'),knob=$('knob');
function message(s){$('message').textContent=s;}
function resetPad(){held=false;pointer=null;x=y=0;knob.style.transform='translate(-50%,-50%)';}
async function control(op,arg=0){resetPad();if(!sender)return;try{await sender.control(op,arg);}catch(e){fail(e);}}
function fail(e){resetPad();if(sender)sender.close();if(device?.gatt.connected)device.gatt.disconnect();message(e.message+'；连接关闭，车辆按超时规则停车');}
$('connect').onclick=async()=>{
  try{
    if(!navigator.bluetooth)throw Error('当前浏览器不支持 Web Bluetooth；请使用支持的 Android Chromium 浏览器');
    device=await navigator.bluetooth.requestDevice({filters:[{services:[SERVICE]}]});
    device.addEventListener('gattserverdisconnected',()=>{resetPad();if(sender)sender.close();message('已断开；重新连接后需重新选择模式并使能');});
    const server=await device.gatt.connect(),service=await server.getPrimaryService(SERVICE);
    const command=await service.getCharacteristic(COMMAND),status=await service.getCharacteristic(STATUS);
    sender=new Sender(b=>command.writeValueWithResponse(b),async()=>parseStatus(await status.readValue()));
    sender.onerror=fail;
    sender.onstate=s=>{
      $('state').textContent=`${['手动','龙芯自动','原功能'][s.mode]} · ${s.flags&2?'急停锁定':s.flags&1?'已使能':'已停车，等待使能'} · 左 ${s.left}% / 右 ${s.right}%`;
      $('mode').value=String(s.mode);$('auto').disabled=!(s.flags&8);
      if(!(s.flags&1)||(s.flags&2))resetPad();
    };
    await sender.refresh();message('已连接。选择模式，点击使能，再拖动摇杆。松手后需重新使能。');
  }catch(e){fail(e);}
};
$('arm').onclick=()=>control(3);$('stop').onclick=()=>control(2);$('estop').onclick=()=>control(4);
$('clear').onclick=()=>control(5);$('mode').onchange=()=>control(6,Number($('mode').value));
$('disconnect').onclick=async()=>{await control(2);if(device?.gatt.connected)device.gatt.disconnect();};
$('speed').oninput=()=>{$('speedValue').textContent=$('speed').value+'%';};
function point(e){const r=pad.getBoundingClientRect(),radius=r.width/2-28;
  let px=e.clientX-r.left-r.width/2,py=r.top+r.height/2-e.clientY;const length=Math.hypot(px,py);
  if(length>radius){px*=radius/length;py*=radius/length;}x=Math.round(px/radius*1000);y=Math.round(py/radius*1000);
  knob.style.transform=`translate(calc(-50% + ${px}px),calc(-50% - ${py}px))`;
}
pad.onpointerdown=e=>{
  if(held||!sender||sender.closed||sender.state?.mode!==0||!(sender.state.flags&1))return;
  held=true;pointer=e.pointerId;pad.setPointerCapture(pointer);point(e);sender.drive(x,y,Number($('speed').value));
};
pad.onpointermove=e=>{if(held&&e.pointerId===pointer){point(e);sender.drive(x,y,Number($('speed').value));}};
function release(e){if(held&&(!e||e.pointerId===pointer)){resetPad();control(2);}}
pad.onpointerup=release;pad.onpointercancel=release;pad.onlostpointercapture=release;
window.addEventListener('blur',()=>{if(held)release();else if(sender&&!sender.closed)control(2);});
document.addEventListener('visibilitychange',()=>{if(document.hidden&&sender&&!sender.closed)control(2);});
window.addEventListener('pagehide',()=>{if(device?.gatt.connected)device.gatt.disconnect();});
setInterval(()=>{if(held&&sender&&!sender.closed)sender.drive(x,y,Number($('speed').value));},80);
setInterval(()=>{if(sender&&!held)sender.refresh();},200);
})(typeof globalThis!=='undefined'?globalThis:this);
