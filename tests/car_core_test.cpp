#include "../main/car_control/car_core.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

static int checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
using namespace car;
static Frame f(Core& c, Op op, uint32_t seq, int x=0, int y=0, int speed=40, int arg=0, bool automatic=false) {
    return {op, static_cast<uint8_t>(speed), seq, static_cast<int16_t>(x), static_cast<int16_t>(y),
            automatic ? c.auto_session() : c.session(), static_cast<uint8_t>(arg)};
}
static void arm(Core& c, uint32_t& seq, uint64_t now=0) { CHECK(c.Receive(f(c,Op::Arm,++seq),Source::Ble,now)); }
int main() {
    // Wi-Fi is a standalone manual controller; it must not need a BLE connection.
    Core web(700,true);
    CHECK(web.WebArm(900,10)); CHECK(web.web_owned() && web.armed());
    Frame wf{Op::Drive,35,1,0,1000,web.session(),0};
    CHECK(web.WebReceive(wf,20)); CHECK(web.wheels().left==35);
    web.Tick(319); CHECK(web.armed()); web.Tick(320); CHECK(!web.armed() && !web.web_owned());
    CHECK(!web.WebReceive(wf,321)); CHECK(web.wheels().left==0);
    CHECK(web.WebArm(901,400)); CHECK(!web.WebArm(902,401));
    CHECK(!web.WebReceive(wf,402)); CHECK(web.armed()); // Old tab cannot stop a new lease.
    wf.session=web.session(); wf.seq=1; CHECK(web.WebReceive(wf,403));
    CHECK(!web.WebReceive(wf,404)); CHECK(!web.armed()); // Replay parks its own lease.
    CHECK(web.WebArm(903,500)); web.Tick(800); CHECK(!web.armed()); // Idle ARM also expires.
    CHECK(web.WebArm(904,900)); wf.session=web.session(); wf.seq=1;
    CHECK(web.WebReceive(wf,901)); web.Connect(1000);
    CHECK(!web.web_owned() && !web.armed() && web.wheels().left==0);
    CHECK(!web.WebArm(905,902));
    CHECK(web.Receive(f(web,Op::Arm,1),Source::Ble,903));
    CHECK(!web.WebReceive(wf,904)); CHECK(web.armed());
    CHECK(!web.WebClear()); CHECK(web.armed());
    web.Fault(); CHECK(!web.WebArm(906,905)); web.Disconnect();
    CHECK(!web.WebArm(907,906)); CHECK(web.WebClear()); CHECK(!web.armed());
    CHECK(web.WebArm(908,1000)); wf.session=web.session();wf.seq=1;wf.op=Op::Stop;wf.y=0;
    CHECK(web.WebReceive(wf,1001)); CHECK(!web.web_owned() && !web.armed());
    CHECK(web.WebArm(909,1100));
    CHECK(web.Receive(f(web,Op::Estop,1),Source::Uart,1101));
    CHECK(web.estop() && !web.armed()); CHECK(web.WebClear());
    CHECK(Mix(0,1000,100).left == 100 && Mix(0,1000,100).right == 100);
    CHECK(Mix(1000,0,100).left == 100 && Mix(1000,0,100).right == -100);
    CHECK(Mix(-1000,0,100).left == -100 && Mix(-1000,0,100).right == 100);
    CHECK(Mix(0,-1000,100).left == -100 && Mix(0,-1000,100).right == -100);
    CHECK(Mix(1000,1000,100).left == 100 && Mix(1000,1000,100).right == 0);
    CHECK(Mix(1000,-1000,100).left == 0 && Mix(1000,-1000,100).right == -100);
    CHECK(Mix(0,120,100).left == 0 && Mix(80,80,100).right == 0);
    CHECK(Mix(0,560,100).left == 50);
    CHECK(Mix(0,1000,0).left == 0 && Mix(0,1000,40).left == 40);
    for (int x=-1000;x<=1000;x+=25) for(int y=-1000;y<=1000;y+=25) {
        auto w=Mix(x,y,37); CHECK(std::abs(w.left)<=37 && std::abs(w.right)<=37);
    }
    Core c(100,true); uint32_t seq=0;
    CHECK(c.wheels().left==0 && !c.armed()); c.Connect(200);
    CHECK(!c.Receive(f(c,Op::Drive,++seq,0,1000),Source::Ble,0));
    arm(c,seq); CHECK(c.Receive(f(c,Op::Drive,++seq,0,1000,60),Source::Ble,10));
    CHECK(c.wheels().left==60); c.Tick(309); CHECK(c.wheels().left==60);
    c.Tick(310); CHECK(c.wheels().left==0 && !c.armed());
    arm(c,seq,311); CHECK(c.Receive(f(c,Op::Drive,++seq,0,1000),Source::Ble,312));
    auto stale=f(c,Op::Drive,seq+2,0,1000); CHECK(c.Receive(f(c,Op::Stop,++seq),Source::Ble,313));
    CHECK(!c.Receive(stale,Source::Ble,314)); CHECK(!c.armed() && c.wheels().left==0);
    arm(c,seq,315); CHECK(c.Receive(f(c,Op::Drive,++seq,0,1000),Source::Ble,316));
    auto replay=f(c,Op::Drive,seq,0,1000); CHECK(!c.Receive(replay,Source::Ble,317));
    CHECK(c.wheels().left==0 && !c.armed());
    CHECK(c.Receive(f(c,Op::Estop,++seq),Source::Ble,318)); CHECK(c.estop());
    CHECK(!c.Receive(f(c,Op::Arm,++seq),Source::Ble,319));
    CHECK(c.Receive(f(c,Op::Clear,++seq),Source::Ble,320)); CHECK(!c.estop() && !c.armed());
    CHECK(c.Receive(f(c,Op::Mode,++seq,0,0,40,1),Source::Ble,321)); CHECK(c.mode()==Mode::Auto);
    CHECK(!c.Receive(f(c,Op::Drive,++seq,0,1000),Source::Ble,322));
    uint32_t aseq=1; CHECK(c.Receive(f(c,Op::Arm,aseq,0,0,40,0,true),Source::Uart,323));
    CHECK(c.Receive(f(c,Op::Drive,++aseq,0,1000,30,0,true),Source::Uart,324)); CHECK(c.wheels().left==30);
    CHECK(!c.Legacy(4,100,1000,1,false,325)); CHECK(c.wheels().left==30);
    c.Disconnect(); CHECK(c.wheels().left==0 && !c.armed());
    CHECK(!c.Receive(f(c,Op::Drive,++aseq,0,1000,30,0,true),Source::Uart,326));
    c.Connect(400); seq=0;
    CHECK(c.Receive(f(c,Op::Mode,++seq,0,0,40,2),Source::Ble,327)); arm(c,seq,328);
    CHECK(c.Legacy(4,50,100,1,true,330)); CHECK(c.Legacy(2,50,100,1,true,330));
    c.Tick(330); CHECK(c.wheels().left==50); c.Tick(430); c.Tick(431); CHECK(c.wheels().left==-50);
    CHECK(c.Receive(f(c,Op::Stop,++seq),Source::Ble,432)); c.Tick(1000); CHECK(c.wheels().left==0 && c.pending()==0);
    arm(c,seq,1001); CHECK(c.Legacy(4,50,100,1,false,1002));
    CHECK(c.Legacy(2,70,200,2,false,1050)); c.Tick(1102); CHECK(c.wheels().left==-70);
    c.Tick(1250); CHECK(c.wheels().left==0);
    CHECK(!c.Receive(f(c,Op::Arm,++seq,1,0),Source::Ble,1300));
    Core no_uart(1,false); no_uart.Connect(2);
    CHECK(!no_uart.Receive(f(no_uart,Op::Mode,1,0,0,40,1),Source::Ble,0)); CHECK(no_uart.mode()==Mode::Manual);
    Frame original{Op::Drive,75,0x12345678,-999,700,0x87654321,0};
    auto bytes=Encode(original); Frame decoded{}; CHECK(Decode(bytes.data(),bytes.size(),decoded));
    CHECK(Crc(reinterpret_cast<const uint8_t*>("123456789"),9)==0x29b1);
    const uint8_t golden[]={0xa5,1,1,75,0x78,0x56,0x34,0x12,0x19,0xfc,0xbc,2,0x21,0x43,0x65,0x87,0,0};
    CHECK(std::equal(bytes.begin(),bytes.begin()+18,golden));
    CHECK(decoded.x==-999 && decoded.seq==0x12345678 && decoded.session==0x87654321);
    CHECK(!Decode(bytes.data(),19,decoded));
    for(size_t i=0;i<bytes.size();++i){auto bad=bytes;bad[i]^=1;CHECK(!Decode(bad.data(),bad.size(),decoded));}
    auto invalid=original;invalid.x=1001;auto bad=Encode(invalid);CHECK(!Decode(bad.data(),bad.size(),decoded));
    invalid=original;invalid.speed=101;bad=Encode(invalid);CHECK(!Decode(bad.data(),bad.size(),decoded));
    invalid=original;invalid.op=static_cast<Op>(99);bad=Encode(invalid);CHECK(!Decode(bad.data(),bad.size(),decoded));
    c.Connect(1000);seq=0;CHECK(c.Receive(f(c,Op::Mode,++seq,0,0,40,2),Source::Ble,2000));arm(c,seq,2001);
    CHECK(c.Sequence({{4,60,300},{4,0,50},{3,60,250}},2,2002));
    CHECK(c.wheels().left==60&&c.pending()==2);
    c.Tick(2302);CHECK(c.wheels().left==0);c.Tick(2352);CHECK(c.wheels().left==-60);
    CHECK(c.Receive(f(c,Op::Stop,++seq),Source::Ble,2353));arm(c,seq,2354);
    c.Tick(9999);CHECK(c.wheels().left==0&&c.pending()==0);
    CHECK(!c.Sequence({{4,101,100}},2,10000));CHECK(c.wheels().left==0&&!c.armed());
    arm(c,seq,10001);CHECK(c.Legacy(4,90,500,2,false,10001));
    CHECK(c.Sequence({{3,40,100},{1,40,100}},1,10002));CHECK(c.wheels().left==90);
    c.Tick(10501);CHECK(c.wheels().left==-40);
    c.Fault(); CHECK(c.estop() && c.wheels().left==0);
    std::printf("PASS: %d behavioral checks (including 6561 mixer bounds)\n", checks);
}
