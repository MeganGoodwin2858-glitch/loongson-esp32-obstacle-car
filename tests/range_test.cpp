#include "../main/car_control/range_core.h"
#include "../main/car_control/uart_receiver.h"
#include <cstdio>
#include <cstdlib>
using namespace car;
static int checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    // Actual IDF/S3 hardware contract: glitch filter uses the 80MHz group clock,
    // with an 8-bit threshold, even though capture is configured to 1MHz.
    CHECK(RmtTimingValid(kRmtMinPulseNs,kRmtIdleNs,80000000,kRmtCaptureHz));
    CHECK(!RmtTimingValid(10000,25000000,80000000,1000000));
    RangeMonitor sensor;
    CHECK(sensor.reading().code==RangeCode::Disabled);
    sensor.Sample(5831,100);CHECK(sensor.reading().distance_mm==1000&&Healthy(sensor.reading(),280));
    CHECK(!Healthy(sensor.reading(),281));CHECK(!Healthy(sensor.reading(),99));
    sensor.Sample(0,200);CHECK(sensor.reading().code==RangeCode::Timeout);CHECK(!Healthy(sensor.reading(),200));
    sensor.Sample(100,300);CHECK(sensor.reading().code==RangeCode::OutOfRange);
    sensor.Sample(30000,400);CHECK(sensor.reading().code==RangeCode::OutOfRange);
    sensor.Sample(116,500);CHECK(sensor.reading().distance_mm==20);
    sensor.Sample(5831,600);sensor.Sample(5831,680);
    sensor.Sample(875,760);CHECK(sensor.reading().distance_mm==150); // near return cannot be hidden by a median filter
    sensor.Sample(5831,840);CHECK(sensor.reading().distance_mm<=1000);
    Pulse valid[]={{2,0,5831,1},{25000,0,0,0}};
    CHECK(EchoPulse(valid,2)==5831);
    Pulse noise[]={{2,0,500,1},{20,0,500,1}};
    CHECK(EchoPulse(noise,2)==0xffffffffu);CHECK(EchoPulse(valid,0)==0);
    auto r=sensor.reading();auto bytes=Telemetry(r,850,0x12345678,true);
    CHECK(bytes[0]==0x5b&&bytes[1]==1&&bytes[2]==0&&Get32(&bytes[4])==0x12345678);
    CHECK(Get16(&bytes[14])==10&&Get16(&bytes[18])==Crc(bytes.data(),18));
    auto stale=Telemetry(r,1021,1,true);CHECK(stale[2]==5&&Get16(&stale[12])==0xffff);
    auto disabled=Telemetry(r,850,1,false);CHECK(disabled[2]==4&&!(disabled[3]&1));
    Core c(10,true);c.Connect(20);
    Frame mode{Op::Mode,40,1,0,0,c.session(),1};CHECK(c.Receive(mode,Source::Ble,0));
    Frame arm{Op::Arm,40,1,0,0,c.auto_session(),0};CHECK(c.Receive(arm,Source::Uart,0));
    Frame drive{Op::Drive,30,2,0,1000,c.auto_session(),0};CHECK(c.Receive(drive,Source::Uart,1));
    c.AutoSafety(true,true,false);CHECK(c.wheels().left==0&&c.armed());
    Frame turn{Op::Drive,25,3,1000,0,c.auto_session(),0};CHECK(c.Receive(turn,Source::Uart,2));
    c.AutoSafety(true,true,false);CHECK(c.wheels().left==25&&c.wheels().right==-25);
    c.AutoSafety(true,false,true);CHECK(c.wheels().left==0&&c.armed());
    CHECK(c.Receive({Op::Drive,30,4,0,1000,c.auto_session(),0},Source::Uart,3));
    auto old=c.auto_session();c.AutoSafety(false,false,false);CHECK(c.wheels().left==0&&!c.armed()&&old!=c.auto_session());
    c.Disconnect();c.Connect(30);CHECK(c.Receive({Op::Arm,40,1,0,0,c.session(),0},Source::Ble,4));
    CHECK(c.Receive({Op::Drive,30,2,0,1000,c.session(),0},Source::Ble,5));
    c.AutoSafety(false,true,true);CHECK(c.wheels().left==30); // manual takeover remains possible
    UartReceiver receiver;
    receiver.Feed(0xa5,10,c);
    CHECK(!receiver.Expire(111,c));CHECK(c.wheels().left==30&&c.armed());
    CHECK(c.Receive({Op::Mode,40,3,0,0,c.session(),1},Source::Ble,112));
    CHECK(c.Receive({Op::Arm,0,1,0,0,c.auto_session(),0},Source::Uart,112));
    CHECK(c.Receive({Op::Drive,30,2,0,1000,c.auto_session(),0},Source::Uart,112));
    receiver.Feed(0xa5,113,c);
    CHECK(c.Receive({Op::Mode,40,4,0,0,c.session(),0},Source::Ble,114));
    CHECK(c.Receive({Op::Arm,40,5,0,0,c.session(),0},Source::Ble,114));
    CHECK(c.Receive({Op::Drive,40,6,0,1000,c.session(),0},Source::Ble,114));
    CHECK(!receiver.Expire(214,c));CHECK(c.wheels().left==40&&c.armed());
    CHECK(c.Receive({Op::Mode,40,7,0,0,c.session(),1},Source::Ble,215));
    CHECK(c.Receive({Op::Arm,0,1,0,0,c.auto_session(),0},Source::Uart,215));
    CHECK(c.Receive({Op::Drive,30,2,0,1000,c.auto_session(),0},Source::Uart,215));
    receiver.Feed(0xa5,216,c);CHECK(receiver.Expire(317,c));CHECK(!c.armed()&&c.wheels().left==0);
    std::printf("PASS: %d range/telemetry/auto-interlock checks\n",checks);
}
