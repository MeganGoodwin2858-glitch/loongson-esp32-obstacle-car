#pragma once
#include "car_core.h"
namespace car {
// A partially received command belongs to the session in which its first byte
// arrived. Expiring an old fragment cannot stop a newly armed manual owner.
class UartReceiver {
    std::array<uint8_t,20> bytes_{};
    size_t used_=0;
    uint64_t last_byte_=0;
    uint32_t start_session_=0;
public:
    bool Expire(uint64_t now,Core& core){
        if(!used_||now-last_byte_<=100)return false;
        used_=0;
        if(core.mode()==Mode::Auto&&core.auto_session()==start_session_){core.Invalid();return true;}
        return false;
    }
    bool Feed(uint8_t byte,uint64_t now,Core& core){
        if(used_==0){if(byte!=0xa5)return false;start_session_=core.auto_session();}
        last_byte_=now;bytes_[used_++]=byte;
        if(used_<bytes_.size())return false;
        used_=0;Frame frame{};
        const bool valid=Decode(bytes_.data(),bytes_.size(),frame);
        if(valid&&frame.op==Op::Estop){core.Receive(frame,Source::Uart,now);return true;}
        if(core.mode()!=Mode::Auto||core.auto_session()!=start_session_)return false;
        if(valid)core.Receive(frame,Source::Uart,now);else core.Invalid();
        return true;
    }
};
}
