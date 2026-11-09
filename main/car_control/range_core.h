#pragma once
#include "car_core.h"
#include <limits>
namespace car {
constexpr uint32_t kRmtMinPulseNs=1000,kRmtIdleNs=25000000,kRmtCaptureHz=1000000;
constexpr bool RmtTimingValid(uint32_t filter_ns,uint32_t idle_ns,uint32_t filter_clock_hz,uint32_t capture_hz){
    return uint64_t(filter_ns)*filter_clock_hz/1000000000<=255&&
           uint64_t(filter_ns)*filter_clock_hz/1000000000>=1&&
           uint64_t(idle_ns)*capture_hz/1000000000<=32767;
}
enum class RangeCode : uint8_t { Valid=0,Timeout=1,OutOfRange=2,Noise=3,Disabled=4,Stale=5 };
struct RangeReading {
    uint32_t sequence=0;
    uint64_t sampled_ms=0;
    uint16_t distance_mm=0xffff,echo_us=0;
    RangeCode code=RangeCode::Disabled;
};
struct Pulse { uint32_t duration0,level0,duration1,level1; };
inline uint32_t EchoPulse(const Pulse* symbols,size_t count){
    uint32_t high=0;int pulses=0;
    for(size_t i=0;i<count;++i){
        if(symbols[i].level0&&symbols[i].duration0){high=symbols[i].duration0;++pulses;}
        if(symbols[i].level1&&symbols[i].duration1){high=symbols[i].duration1;++pulses;}
    }
    return pulses>1?0xffffffffu:high;
}
inline bool Healthy(const RangeReading& r,uint64_t now){
    return r.sequence!=0&&r.code==RangeCode::Valid&&now>=r.sampled_ms&&now-r.sampled_ms<=180;
}
class RangeMonitor {
    RangeReading reading_{};
    std::array<uint16_t,3> history_{};
    size_t count_=0,index_=0;
public:
    void Sample(uint32_t pulse_us,uint64_t sampled_ms){
        if(++reading_.sequence==0)++reading_.sequence;
        reading_.sampled_ms=sampled_ms;reading_.distance_mm=0xffff;
        reading_.echo_us=static_cast<uint16_t>(std::min<uint32_t>(pulse_us,65535));
        if(pulse_us==0){reading_.code=RangeCode::Timeout;count_=index_=0;return;}
        if(pulse_us==0xffffffffu){reading_.code=RangeCode::Noise;count_=index_=0;return;}
        const uint64_t distance=(uint64_t(pulse_us)*343+1000)/2000;
        if(distance<20||distance>4000){reading_.code=RangeCode::OutOfRange;count_=index_=0;return;}
        const auto raw=static_cast<uint16_t>(distance);
        history_[index_]=raw;index_=(index_+1)%3;if(count_<3)++count_;
        auto sorted=history_;
        if(count_==3)std::sort(sorted.begin(),sorted.end());
        // A near return is applied immediately; far clearance needs stable samples.
        reading_.distance_mm=count_==3?std::min(raw,sorted[1]):raw;
        reading_.code=RangeCode::Valid;
    }
    const RangeReading& reading()const{return reading_;}
};
inline std::array<uint8_t,20> Telemetry(const RangeReading& r,uint64_t now,uint32_t session,bool enabled){
    const bool valid=enabled&&Healthy(r,now);
    RangeCode code=enabled?r.code:RangeCode::Disabled;
    if(enabled&&r.code==RangeCode::Valid&&!valid)code=RangeCode::Stale;
    const uint16_t age=(r.sequence&&now>=r.sampled_ms)?static_cast<uint16_t>(std::min<uint64_t>(now-r.sampled_ms,65535)):65535;
    std::array<uint8_t,20>b{};b[0]=0x5b;b[1]=1;b[2]=static_cast<uint8_t>(code);
    b[3]=(enabled?1:0)|(!valid||r.distance_mm<=300?2:0)|(!valid||r.distance_mm<=120?4:0);
    Put32(&b[4],session);Put32(&b[8],r.sequence);Put16(&b[12],valid?r.distance_mm:0xffff);
    Put16(&b[14],age);Put16(&b[16],r.echo_us);Put16(&b[18],Crc(b.data(),18));return b;
}
}
