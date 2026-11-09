#include "hcsr04.h"
#include "sdkconfig.h"
#include <mutex>
#include <driver/gpio.h>
#include <driver/rmt_rx.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

namespace car {
static std::mutex range_mutex;
static RangeMonitor monitor;
#ifdef CONFIG_CAR_HCSR04
static rmt_channel_handle_t rx=nullptr;
static QueueHandle_t events=nullptr;
static rmt_symbol_word_t symbols[64];
static bool OnReceive(rmt_channel_handle_t,const rmt_rx_done_event_data_t* data,void*){
    BaseType_t woken=pdFALSE;
    xQueueSendFromISR(events,data,&woken);
    return woken==pdTRUE;
}
static void Task(void*){
    int64_t next_allowed_us=0;
    while(true){
        const int64_t remaining_us=next_allowed_us-esp_timer_get_time();
        if(remaining_us>0){
            // A one-tick delay can wake early within its tick phase. Round up,
            // add one tick and recheck the absolute monotonic deadline.
            const auto ticks=(remaining_us*configTICK_RATE_HZ+999999)/1000000+1;
            vTaskDelay(static_cast<TickType_t>(ticks));
            continue;
        }
        const uint64_t trigger_ms=static_cast<uint64_t>(esp_timer_get_time()/1000);
        next_allowed_us=esp_timer_get_time()+80000;
        uint32_t pulse=0;
        rmt_receive_config_t config{};config.signal_range_min_ns=kRmtMinPulseNs;config.signal_range_max_ns=kRmtIdleNs;
        xQueueReset(events);
        if(gpio_get_level(static_cast<gpio_num_t>(CONFIG_CAR_HCSR04_ECHO))!=0){pulse=0xffffffffu;}
        else if(rmt_receive(rx,symbols,sizeof(symbols),&config)!=ESP_OK){pulse=0xffffffffu;}
        else{
            gpio_set_level(static_cast<gpio_num_t>(CONFIG_CAR_HCSR04_TRIG),1);
            next_allowed_us=esp_timer_get_time()+80000;
            esp_rom_delay_us(10);
            gpio_set_level(static_cast<gpio_num_t>(CONFIG_CAR_HCSR04_TRIG),0);
            rmt_rx_done_event_data_t event{};
            if(xQueueReceive(events,&event,pdMS_TO_TICKS(60))==pdTRUE){
                Pulse parsed[64]{};
                const size_t count=std::min(event.num_symbols,size_t(64));
                for(size_t i=0;i<count;++i){const auto& s=event.received_symbols[i];
                    parsed[i]={s.duration0,s.level0,s.duration1,s.level1};}
                pulse=EchoPulse(parsed,count);
            }else{
                // Abort pending capture before reusing the event buffer; a late
                // callback must never become a future sample.
                if(rmt_disable(rx)!=ESP_OK||rmt_enable(rx)!=ESP_OK)pulse=0xffffffffu;
            }
        }
        {std::lock_guard<std::mutex> lock(range_mutex);monitor.Sample(pulse,trigger_ms);}
    }
}
#endif
bool StartHcsr04(){
#ifdef CONFIG_CAR_HCSR04
    // app_main initializes RMT on its fixed core. Keep the capture task on that
    // IRQ core so disable/reset/rearm cannot race a callback on another CPU.
    static_assert(CONFIG_ESP_MAIN_TASK_AFFINITY==0||CONFIG_ESP_MAIN_TASK_AFFINITY==1,"RMT init requires a pinned app_main");
    static_assert(RmtTimingValid(kRmtMinPulseNs,kRmtIdleNs,80000000,kRmtCaptureHz),"RMT filter/capture hardware limits exceeded");
    static_assert(CONFIG_CAR_HCSR04_TRIG!=CONFIG_CAR_HCSR04_ECHO,"TRIG and ECHO must differ");
    static_assert(CONFIG_CAR_HCSR04_TRIG!=CONFIG_CAR_UART_TX&&CONFIG_CAR_HCSR04_TRIG!=CONFIG_CAR_UART_RX,"TRIG/UART conflict");
    static_assert(CONFIG_CAR_HCSR04_ECHO!=CONFIG_CAR_UART_TX&&CONFIG_CAR_HCSR04_ECHO!=CONFIG_CAR_UART_RX,"ECHO/UART conflict");
    gpio_config_t gpio{};gpio.mode=GPIO_MODE_OUTPUT;gpio.pin_bit_mask=1ULL<<CONFIG_CAR_HCSR04_TRIG;
    if(gpio_config(&gpio)!=ESP_OK)return false;
    gpio_set_level(static_cast<gpio_num_t>(CONFIG_CAR_HCSR04_TRIG),0);
    gpio={};gpio.mode=GPIO_MODE_INPUT;gpio.pin_bit_mask=1ULL<<CONFIG_CAR_HCSR04_ECHO;gpio.pull_down_en=GPIO_PULLDOWN_ENABLE;
    if(gpio_config(&gpio)!=ESP_OK)return false;
    rmt_rx_channel_config_t config{};config.gpio_num=static_cast<gpio_num_t>(CONFIG_CAR_HCSR04_ECHO);
    config.clk_src=RMT_CLK_SRC_DEFAULT;config.resolution_hz=kRmtCaptureHz;config.mem_block_symbols=64;
    events=xQueueCreate(1,sizeof(rmt_rx_done_event_data_t));
    if(!events)return false;
    rmt_rx_event_callbacks_t callbacks{};callbacks.on_recv_done=OnReceive;
    if(rmt_new_rx_channel(&config,&rx)!=ESP_OK||rmt_rx_register_event_callbacks(rx,&callbacks,nullptr)!=ESP_OK||rmt_enable(rx)!=ESP_OK)return false;
    if(xTaskCreatePinnedToCore(Task,"hcsr04",4096,nullptr,6,nullptr,xPortGetCoreID())!=pdPASS)return false;
    ESP_LOGI("HCSR04","RMT capture ready, 80ms interval; hardware measurements not validated");
#endif
    return true;
}
RangeReading LatestRange(){std::lock_guard<std::mutex> lock(range_mutex);return monitor.reading();}
}
