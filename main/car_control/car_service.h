#pragma once
#include "car_core.h"
namespace car {
bool Start();
bool LegacyCommand(int direction,int speed,int duration_ms,int priority,bool queued=false);
bool LegacySequence(std::initializer_list<LegacyStep> steps,int priority);
void Stop();
struct WebState {
    bool ready,armed,estop,ble,web;
    int mode,left,right;
};
WebState GetWebState();
uint32_t WebArm();
bool WebCommand(const uint8_t* bytes,size_t length);
bool WebClear();
void WebEstop();
}
