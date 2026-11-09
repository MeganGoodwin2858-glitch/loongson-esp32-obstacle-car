#include "../main/car_control/range_core.h"
#include "../main/car_control/uart_receiver.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
using namespace car;
int main(){
    Core core(1,true);RangeMonitor range;UartReceiver receiver;uint32_t seq=0;uint64_t now=0;
    std::string line;
    while(std::getline(std::cin,line)){
        std::istringstream input(line);std::string command;input>>command;
        if(command=="CONNECT"){core.Connect(77);seq=0;}
        else if(command=="MODE"){int mode;input>>mode;core.Receive({Op::Mode,40,++seq,0,0,core.session(),static_cast<uint8_t>(mode)},Source::Ble,now);}
        else if(command=="ESTOP")core.Receive({Op::Estop,0,++seq,0,0,core.session(),0},Source::Ble,now);
        else if(command=="DISCONNECT")core.Disconnect();
        else if(command=="ARM_BLE")core.Receive({Op::Arm,40,++seq,0,0,core.session(),0},Source::Ble,now);
        else if(command=="BLE_DRIVE"){
            int x,y,speed;input>>x>>y>>speed>>now;
            core.Receive({Op::Drive,static_cast<uint8_t>(speed),++seq,static_cast<int16_t>(x),static_cast<int16_t>(y),core.session(),0},Source::Ble,now);
        }
        else if(command=="RANGE"){uint32_t pulse;input>>pulse>>now;range.Sample(pulse,now);}
        else if(command=="TICK")input>>now;
        else if(command=="UART"){
            std::string hex;input>>hex>>now;
            if(hex.size()%2!=0)return 2;
            for(size_t i=0;i<hex.size();i+=2){unsigned v=0;std::istringstream byte(hex.substr(i,2));byte>>std::hex>>v;
                if(byte.fail())return 2;
                receiver.Feed(static_cast<uint8_t>(v),now,core);}
        }else return 2;
        receiver.Expire(now,core);core.Tick(now);const auto r=range.reading();const bool healthy=Healthy(r,now);
        core.AutoSafety(healthy,!healthy||r.distance_mm<=300,!healthy||r.distance_mm<=120);
        const auto status=core.Status(),telemetry=Telemetry(r,now,core.auto_session(),true);
        for(auto b:status)std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<int>(b);
        std::cout<<' ';
        for(auto b:telemetry)std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<static_cast<int>(b);
        std::cout<<std::endl;
    }
}
