#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>

namespace car {
enum class Mode : uint8_t { Manual=0, Auto=1, Legacy=2 };
enum class Source { Ble, Uart };
enum class Op : uint8_t { Drive=1, Stop=2, Arm=3, Estop=4, Clear=5, Mode=6 };
struct Wheels { int left=0, right=0; };
struct LegacyStep { int direction,speed,duration_ms; };
struct Frame { Op op{}; uint8_t speed=0; uint32_t seq=0; int16_t x=0,y=0; uint32_t session=0; uint8_t arg=0; };
inline Wheels Mix(int x,int y,int speed) {
    const double radius=std::hypot(static_cast<double>(x),static_cast<double>(y));
    if (radius<=120 || speed<=0) return {};
    const double magnitude=(std::min(radius,1000.0)-120.0)/880.0;
    const double turn=x/radius*magnitude, forward=y/radius*magnitude;
    double left=forward+turn, right=forward-turn;
    const double scale=std::max({1.0,std::abs(left),std::abs(right)});
    return {static_cast<int>(std::lround(left/scale*std::min(speed,100))),
            static_cast<int>(std::lround(right/scale*std::min(speed,100)))};
}
inline uint16_t Crc(const uint8_t* p,size_t n) {
    uint16_t crc=0xffff;
    for(size_t i=0;i<n;++i){crc^=static_cast<uint16_t>(p[i])<<8;
        for(int b=0;b<8;++b) crc=static_cast<uint16_t>((crc&0x8000)?(crc<<1)^0x1021:crc<<1);}
    return crc;
}
inline void Put16(uint8_t* p,uint16_t v){p[0]=static_cast<uint8_t>(v);p[1]=static_cast<uint8_t>(v>>8);}
inline void Put32(uint8_t* p,uint32_t v){for(int i=0;i<4;++i)p[i]=static_cast<uint8_t>(v>>(8*i));}
inline uint16_t Get16(const uint8_t* p){return static_cast<uint16_t>(p[0]|(p[1]<<8));}
inline uint32_t Get32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline std::array<uint8_t,20> Encode(const Frame& f) {
    std::array<uint8_t,20> b{}; b[0]=0xa5;b[1]=1;b[2]=static_cast<uint8_t>(f.op);b[3]=f.speed;
    Put32(&b[4],f.seq);Put16(&b[8],static_cast<uint16_t>(f.x));Put16(&b[10],static_cast<uint16_t>(f.y));
    Put32(&b[12],f.session);b[16]=f.arg;Put16(&b[18],Crc(b.data(),18));return b;
}
inline bool Valid(const Frame& f) {
    const auto op=static_cast<uint8_t>(f.op);
    if(op<1||op>6||f.speed>100||f.x < -1000||f.x>1000||f.y < -1000||f.y>1000||f.seq==0) return false;
    if(f.op!=Op::Drive&&(f.x!=0||f.y!=0)) return false;
    return f.op==Op::Mode ? f.arg<=2 : f.arg==0;
}
inline bool Decode(const uint8_t* b,size_t n,Frame& f) {
    if(!b||n!=20||b[0]!=0xa5||b[1]!=1||b[17]!=0||Get16(&b[18])!=Crc(b,18)) return false;
    f={static_cast<Op>(b[2]),b[3],Get32(&b[4]),static_cast<int16_t>(Get16(&b[8])),
        static_cast<int16_t>(Get16(&b[10])),Get32(&b[12]),b[16]};return Valid(f);
}

// This class has no hardware/RTOS dependencies. The service serializes every call.
class Core {
    struct Action { Wheels wheels; uint64_t duration; int priority; };
    Mode mode_=Mode::Manual;
    bool connected_=false,armed_=false,estop_=false,uart_available_=false,active_=false;
    bool web_owned_=false;
    uint32_t web_seq_=0;
    uint32_t session_,auto_session_,ble_seq_=0,uart_seq_=0;
    uint64_t last_drive_=0,deadline_=0;
    int priority_=0;
    Wheels wheels_{};
    std::deque<Action> queue_;
    void Park() {
        wheels_={};armed_=false;active_=false;web_owned_=false;queue_.clear();deadline_=0;priority_=0;
        if(++session_==0)++session_;
        if(++auto_session_==0)++auto_session_;
    }
    bool Reject(){Park();return false;}
public:
    explicit Core(uint32_t seed,bool uart_available=false):uart_available_(uart_available),
        session_(seed?seed:1),auto_session_((seed^0x65737461u)?(seed^0x65737461u):1){}
    bool WebArm(uint32_t seed,uint64_t now) {
        if(connected_||web_owned_||armed_||estop_||mode_!=Mode::Manual||seed==0)return false;
        Park();session_=seed;web_seq_=0;web_owned_=true;armed_=true;
        last_drive_=now;active_=true;return true;
    }
    bool WebClear(){if(connected_||web_owned_||armed_)return false;Park();estop_=false;return true;}
    bool WebReceive(const Frame& f,uint64_t now) {
        // Ignore old/foreign controllers before mutating the current owner's state.
        if(!web_owned_||connected_||f.session!=session_)return false;
        if(!Valid(f)||f.seq<=web_seq_)return Reject();
        web_seq_=f.seq;
        if(f.op==Op::Stop){Park();return true;}
        if(f.op!=Op::Drive||!armed_||estop_||mode_!=Mode::Manual)return Reject();
        wheels_=Mix(f.x,f.y,f.speed);last_drive_=now;active_=true;return true;
    }
    bool web_owned()const{return web_owned_;}
    bool connected()const{return connected_;}
    void Connect(uint32_t seed){Park();session_=seed?seed:1;ble_seq_=0;connected_=true;mode_=Mode::Manual;}
    void Disconnect(){Park();connected_=false;mode_=Mode::Manual;}
    void Fault(){Park();estop_=true;}
    void Invalid(){Park();}
    void AutoSafety(bool healthy,bool forward_blocked,bool hard_stop){
        if(mode_!=Mode::Auto||!armed_)return;
        if(!healthy){Park();return;}
        if(hard_stop||(forward_blocked&&wheels_.left+wheels_.right>0))wheels_={};
    }
    bool Receive(const Frame& f,Source source,uint64_t now) {
        if(!Valid(f)) return Reject();
        // A valid emergency stop is never suppressed by a stale sequence or token.
        if(f.op==Op::Estop){Fault();return true;}
        if(!connected_) return Reject();
        auto& seq=source==Source::Ble?ble_seq_:uart_seq_;
        const uint32_t session=source==Source::Ble?session_:auto_session_;
        if(f.session!=session||f.seq<=seq) return Reject();
        seq=f.seq;
        if(f.op==Op::Stop){Park();return true;}
        if(source==Source::Uart&&(mode_!=Mode::Auto||!uart_available_)) return Reject();
        if(f.op==Op::Clear){if(source!=Source::Ble)return Reject();Park();estop_=false;return true;}
        if(f.op==Op::Mode){
            if(source!=Source::Ble||estop_)return Reject();
            if(f.arg==1&&!uart_available_)return Reject();
            Park();mode_=static_cast<Mode>(f.arg);uart_seq_=0;return true;
        }
        if(estop_)return Reject();
        if(f.op==Op::Arm){
            if((source==Source::Ble&&mode_==Mode::Auto)||(source==Source::Uart&&mode_!=Mode::Auto))return Reject();
            wheels_={};queue_.clear();active_=false;armed_=true;return true;
        }
        if(f.op!=Op::Drive||!armed_||
            (source==Source::Ble&&mode_!=Mode::Manual)||(source==Source::Uart&&mode_!=Mode::Auto))return Reject();
        wheels_=Mix(f.x,f.y,f.speed);last_drive_=now;active_=true;return true;
    }
    bool Legacy(int direction,int speed,int duration,int priority,bool queued,uint64_t now) {
        if(direction==0){Park();return true;}
        if(mode_!=Mode::Legacy||!armed_||estop_||!connected_) return false;
        if(direction<1||direction>4||speed<0||speed>100||duration<1||duration>10000) return Reject();
        Wheels w{};
        switch(direction){case 1:w={speed,-speed};break;case 2:w={-speed,-speed};break;
            case 3:w={-speed,speed};break;case 4:w={speed,speed};break;}
        if(queued||(active_&&priority<priority_)){
            if(queue_.size()>=16)return Reject();
            queue_.push_back({w,static_cast<uint64_t>(duration),priority});return true;
        }
        queue_.clear();wheels_=w;deadline_=now+duration;priority_=priority;active_=true;return true;
    }
    bool Sequence(std::initializer_list<LegacyStep> steps,int priority,uint64_t now) {
        if(mode_!=Mode::Legacy||!armed_||estop_||!connected_)return false;
        if(steps.size()==0||steps.size()>16)return Reject();
        for(const auto& s:steps){
            if(s.direction<1||s.direction>4||s.speed<0||s.speed>100||s.duration_ms<1||s.duration_ms>10000)return Reject();
        }
        // Submit the whole choreography now. No sleeping caller is allowed to
        // submit its next segment after a STOP and subsequent re-arm.
        if(active_&&priority<priority_){
            if(queue_.size()+steps.size()>16)return Reject();
        }else{queue_.clear();wheels_={};active_=false;}
        for(const auto& s:steps){
            if(!Legacy(s.direction,s.speed,s.duration_ms,priority,true,now))return false;
        }
        Tick(now);return true;
    }
    void Tick(uint64_t now) {
        if(active_&&mode_!=Mode::Legacy&&now-last_drive_>=300){Park();return;}
        if(mode_==Mode::Legacy&&armed_&&!estop_){
            if(active_&&now>=deadline_){wheels_={};active_=false;priority_=0;}
            if(!active_&&!queue_.empty()) {auto a=queue_.front();queue_.pop_front();wheels_=a.wheels;
                deadline_=now+a.duration;priority_=a.priority;active_=true;}
        }
    }
    std::array<uint8_t,20> Status() const {
        std::array<uint8_t,20>b{};b[0]=0x5a;b[1]=1;b[2]=static_cast<uint8_t>(mode_);
        b[3]=(armed_?1:0)|(estop_?2:0)|(connected_?4:0)|(uart_available_?8:0);
        Put32(&b[4],session_);Put32(&b[8],auto_session_);
        Put16(&b[12],static_cast<uint16_t>(wheels_.left));Put16(&b[14],static_cast<uint16_t>(wheels_.right));
        Put16(&b[18],Crc(b.data(),18));return b;
    }
    Wheels wheels()const{return wheels_;} bool armed()const{return armed_;}
    bool estop()const{return estop_;} Mode mode()const{return mode_;}
    uint32_t session()const{return session_;} uint32_t auto_session()const{return auto_session_;}
    size_t pending()const{return queue_.size();}
};
}
