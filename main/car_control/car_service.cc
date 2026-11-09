#include "car_service.h"
#include "car_core.h"
#include "hcsr04.h"
#include "uart_receiver.h"
#include "sdkconfig.h"
#include "boards/qebabe-xiaoche/config.h"
#include <mutex>
#include <cstring>
#include <cstdio>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
extern "C" {
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
}

namespace car {
static constexpr bool kUart =
#ifdef CONFIG_CAR_LOONGSON_UART
true;
#else
false;
#endif
static std::mutex mutex;
static Core core(1,kUart);
static bool ready=false, pwm_ready=false, hardware_fault=false;
static uint16_t connection=BLE_HS_CONN_HANDLE_NONE;
static uint8_t address_type;
static constexpr bool kRange=
#ifdef CONFIG_CAR_HCSR04
true;
#else
false;
#endif
static constexpr gpio_num_t pins[]={MOTOR_LF_GPIO,MOTOR_LB_GPIO,MOTOR_RF_GPIO,MOTOR_RB_GPIO};
static const ble_uuid128_t service_uuid=BLE_UUID128_INIT(0x01,0x00,0x00,0x00,0x36,0x36,0x45,0x42,0x8d,0x4a,0x6c,0x18,0x00,0x36,0xc0,0x7b);
static const ble_uuid128_t command_uuid=BLE_UUID128_INIT(0x02,0x00,0x00,0x00,0x36,0x36,0x45,0x42,0x8d,0x4a,0x6c,0x18,0x00,0x36,0xc0,0x7b);
static const ble_uuid128_t status_uuid=BLE_UUID128_INIT(0x03,0x00,0x00,0x00,0x36,0x36,0x45,0x42,0x8d,0x4a,0x6c,0x18,0x00,0x36,0xc0,0x7b);
static uint64_t Now(){return static_cast<uint64_t>(esp_timer_get_time()/1000);}
static bool Zero() {
    bool ok=true;
    for(int i=0;i<4;++i){
        // ledc_stop drives the idle pin LOW even after a duty-update failure.
        if(ledc_stop(LEDC_LOW_SPEED_MODE,static_cast<ledc_channel_t>(i),0)!=ESP_OK)ok=false;
        gpio_set_level(pins[i],0);
    }
    return ok;
}
static void Apply() {
    if(kRange){const auto r=LatestRange();const bool healthy=Healthy(r,Now());
        core.AutoSafety(healthy,!healthy||r.distance_mm<=300,!healthy||r.distance_mm<=120);}
    if(!pwm_ready)return;
    static Wheels previous{1000,1000};
    auto w=core.wheels();
    if(hardware_fault){core.Fault();Zero();return;}
    if(w.left==previous.left&&w.right==previous.right)return;
    bool ok=Zero();
    // Break before make, including a short all-LOW interval on direction changes.
    if((w.left*previous.left<0)||(w.right*previous.right<0)) esp_rom_delay_us(100);
    const int values[]={std::max(w.left,0),std::max(-w.left,0),std::max(w.right,0),std::max(-w.right,0)};
    for(int i=0;i<4;++i){auto channel=static_cast<ledc_channel_t>(i);
        if(ledc_set_duty(LEDC_LOW_SPEED_MODE,channel,values[i]*1023/100)!=ESP_OK||
           ledc_update_duty(LEDC_LOW_SPEED_MODE,channel)!=ESP_OK)ok=false;}
    if(!ok){hardware_fault=true;core.Fault();Zero();ESP_LOGE("Car","PWM fault; reboot required after diagnosis");}
    previous=ok?w:Wheels{};
}
static int Access(uint16_t conn,uint16_t,ble_gatt_access_ctxt* ctxt,void*) {
    std::lock_guard<std::mutex> lock(mutex);
    if(conn!=connection)return BLE_ATT_ERR_UNLIKELY;
    if(ble_uuid_cmp(ctxt->chr->uuid,&status_uuid.u)==0){
        auto b=core.Status();return os_mbuf_append(ctxt->om,b.data(),b.size())==0?0:BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    uint8_t bytes[20];Frame frame{};
    if(OS_MBUF_PKTLEN(ctxt->om)!=20||os_mbuf_copydata(ctxt->om,0,20,bytes)!=0||!Decode(bytes,20,frame)){
        core.Invalid();Apply();return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    // A hardware failure cannot be cleared over radio.
    bool accepted=!hardware_fault&&core.Receive(frame,Source::Ble,Now());Apply();
    return accepted?0:BLE_ATT_ERR_VALUE_NOT_ALLOWED;
}
static const ble_gatt_chr_def characteristics[]={
    { .uuid=&command_uuid.u,.access_cb=Access,.arg=nullptr,.descriptors=nullptr,.flags=BLE_GATT_CHR_F_WRITE,.min_key_size=0,.val_handle=nullptr },
    { .uuid=&status_uuid.u,.access_cb=Access,.arg=nullptr,.descriptors=nullptr,.flags=BLE_GATT_CHR_F_READ,.min_key_size=0,.val_handle=nullptr },
    {}
};
static const ble_gatt_svc_def services[]={
    { .type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service_uuid.u,.includes=nullptr,.characteristics=characteristics },{}
};
static void Advertise();
static int Gap(ble_gap_event* event,void*) {
    bool advertise=false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if(event->type==BLE_GAP_EVENT_CONNECT){
            if(event->connect.status==0){connection=event->connect.conn_handle;core.Connect(esp_random());Apply();}
            else advertise=true;
        }else if(event->type==BLE_GAP_EVENT_DISCONNECT){
            connection=BLE_HS_CONN_HANDLE_NONE;core.Disconnect();Apply();advertise=true;
        }else if(event->type==BLE_GAP_EVENT_ADV_COMPLETE)advertise=true;
    }
    if(advertise)Advertise();
    return 0;
}
static void Advertise(){
    ble_hs_adv_fields fields{};fields.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    auto uuid=service_uuid;fields.uuids128=&uuid;fields.num_uuids128=1;fields.uuids128_is_complete=1;
    if(ble_gap_adv_set_fields(&fields)!=0){std::lock_guard<std::mutex> lock(mutex);core.Fault();Apply();return;}
    ble_hs_adv_fields response{};const char* name=ble_svc_gap_device_name();
    response.name=reinterpret_cast<const uint8_t*>(name);response.name_len=static_cast<uint8_t>(strlen(name));response.name_is_complete=1;
    ble_gap_adv_rsp_set_fields(&response);
    ble_gap_adv_params params{};params.conn_mode=BLE_GAP_CONN_MODE_UND;params.disc_mode=BLE_GAP_DISC_MODE_GEN;
    if(ble_gap_adv_start(address_type,nullptr,BLE_HS_FOREVER,&params,Gap,nullptr)!=0){
        std::lock_guard<std::mutex> lock(mutex);core.Fault();Apply();}
}
static void Sync(){if(ble_hs_id_infer_auto(0,&address_type)==0)Advertise();else {std::lock_guard<std::mutex> lock(mutex);core.Fault();Apply();}}
static void Reset(int){std::lock_guard<std::mutex> lock(mutex);connection=BLE_HS_CONN_HANDLE_NONE;core.Disconnect();Apply();}
static void Host(void*){nimble_port_run();nimble_port_freertos_deinit();}
static void Worker(void*) {
    UartReceiver receiver;uint64_t last_status=0,last_diag=0;
    uint32_t tx_bytes=0,tx_short=0,rx_bytes=0;
    while(true){
        uint8_t byte=0;
        if(kUart){
            const uint64_t now=Now();
            {std::lock_guard<std::mutex> lock(mutex);if(receiver.Expire(now,core))Apply();}
            // Bounded processing so a noisy UART cannot starve the watchdog.
            for(int count=0;count<64&&uart_read_bytes(UART_NUM_1,&byte,1,0)==1;++count){
                ++rx_bytes;
                std::lock_guard<std::mutex> lock(mutex);
                if(!hardware_fault&&receiver.Feed(byte,Now(),core))Apply();
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex);core.Tick(Now());Apply();
            if(kUart&&Now()-last_status>=50){auto b=core.Status();
                int sent=uart_tx_chars(UART_NUM_1,reinterpret_cast<const char*>(b.data()),20);
                if(sent!=20)++tx_short;
                if(sent>0)tx_bytes+=sent;
                auto range=Telemetry(LatestRange(),Now(),core.auto_session(),kRange);
                sent=uart_tx_chars(UART_NUM_1,reinterpret_cast<const char*>(range.data()),20);
                if(sent!=20)++tx_short;
                if(sent>0)tx_bytes+=sent;
                last_status=Now();}
        }
        if(kUart&&Now()-last_diag>=3000){
            uint32_t baud=0;uart_get_baudrate(UART_NUM_1,&baud);
            ESP_LOGI("Car","UART1 TX=IO%d RX=IO%d baud=%lu bytes_out=%lu short_writes=%lu bytes_in=%lu",
                CONFIG_CAR_UART_TX,CONFIG_CAR_UART_RX,static_cast<unsigned long>(baud),
                static_cast<unsigned long>(tx_bytes),static_cast<unsigned long>(tx_short),static_cast<unsigned long>(rx_bytes));
            if(last_diag==0)gpio_dump_io_configuration(stdout,(1ULL<<CONFIG_CAR_UART_TX)|(1ULL<<CONFIG_CAR_UART_RX));
            last_diag=Now();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
bool Start() {
    std::lock_guard<std::mutex> lock(mutex);
    if(ready)return true;
    gpio_config_t gpio{};gpio.mode=GPIO_MODE_OUTPUT;
    for(auto pin:pins)gpio.pin_bit_mask|=1ULL<<pin;
    if(gpio_config(&gpio)!=ESP_OK){core.Fault();return false;}
    for(auto pin:pins)gpio_set_level(pin,0);
    ledc_timer_config_t timer{};timer.speed_mode=LEDC_LOW_SPEED_MODE;timer.duty_resolution=LEDC_TIMER_10_BIT;
    timer.timer_num=LEDC_TIMER_0;timer.freq_hz=20000;timer.clk_cfg=LEDC_AUTO_CLK;
    if(ledc_timer_config(&timer)!=ESP_OK){core.Fault();return false;}
    for(int i=0;i<4;++i){ledc_channel_config_t ch{};ch.gpio_num=pins[i];ch.speed_mode=LEDC_LOW_SPEED_MODE;
        ch.channel=static_cast<ledc_channel_t>(i);ch.timer_sel=LEDC_TIMER_0;
        if(ledc_channel_config(&ch)!=ESP_OK){Zero();core.Fault();return false;}}
    pwm_ready=true;core=Core(esp_random(),kUart);Apply();
#ifdef CONFIG_CAR_LOONGSON_UART
    static_assert(CONFIG_CAR_UART_TX!=CONFIG_CAR_UART_RX,"UART TX and RX must use different GPIOs");
    uart_config_t uart{};uart.baud_rate=115200;uart.data_bits=UART_DATA_8_BITS;uart.parity=UART_PARITY_DISABLE;
    uart.stop_bits=UART_STOP_BITS_1;uart.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;uart.source_clk=UART_SCLK_DEFAULT;
    if(uart_param_config(UART_NUM_1,&uart)!=ESP_OK||
       uart_set_pin(UART_NUM_1,CONFIG_CAR_UART_TX,CONFIG_CAR_UART_RX,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK||
       uart_driver_install(UART_NUM_1,1024,0,0,nullptr,0)!=ESP_OK){core.Fault();Apply();return false;}
#endif
    if(!StartHcsr04()){core.Fault();Apply();return false;}
    if(nimble_port_init()!=ESP_OK){core.Fault();Apply();return false;}
    ble_hs_cfg.sync_cb=Sync;ble_hs_cfg.reset_cb=Reset;
    ble_svc_gap_init();ble_svc_gatt_init();ble_svc_gap_device_name_set("Xiaozhi-Car-360");
    if(ble_gatts_count_cfg(services)!=0||ble_gatts_add_svcs(services)!=0){core.Fault();Apply();return false;}
    if(xTaskCreate(Worker,"car_watchdog",4096,nullptr,8,nullptr)!=pdPASS){core.Fault();Apply();return false;}
    ready=true;nimble_port_freertos_init(Host);return true;
}
bool LegacyCommand(int direction,int speed,int duration_ms,int priority,bool queued){
    std::lock_guard<std::mutex> lock(mutex);
    if(!ready||hardware_fault)return false;
    bool accepted=core.Legacy(direction,speed,duration_ms,priority,queued,Now());Apply();return accepted;
}
bool LegacySequence(std::initializer_list<LegacyStep> steps,int priority){
    std::lock_guard<std::mutex> lock(mutex);
    if(!ready||hardware_fault)return false;
    bool accepted=core.Sequence(steps,priority,Now());Apply();return accepted;
}
void Stop(){std::lock_guard<std::mutex> lock(mutex);core.Invalid();Apply();}
WebState GetWebState(){
    std::lock_guard<std::mutex> lock(mutex);core.Tick(Now());Apply();auto w=core.wheels();
    return {ready&&!hardware_fault,core.armed(),core.estop(),core.connected(),core.web_owned(),
        static_cast<int>(core.mode()),w.left,w.right};
}
uint32_t WebArm(){
    std::lock_guard<std::mutex> lock(mutex);
    // Modem sleep can delay HTTP beyond the 300 ms control watchdog.
    if(!ready||hardware_fault||esp_wifi_set_ps(WIFI_PS_NONE)!=ESP_OK)return 0;
    core.Tick(Now());uint32_t token=esp_random();if(!token)token=1;
    bool accepted=core.WebArm(token,Now());Apply();return accepted?token:0;
}
bool WebCommand(const uint8_t* bytes,size_t length){
    std::lock_guard<std::mutex> lock(mutex);Frame frame{};
    if(!ready||hardware_fault||!Decode(bytes,length,frame))return false;
    core.Tick(Now());bool accepted=core.WebReceive(frame,Now());Apply();return accepted;
}
bool WebClear(){std::lock_guard<std::mutex> lock(mutex);
    if(!ready||hardware_fault)return false;
    bool accepted=core.WebClear();Apply();return accepted;
}
void WebEstop(){std::lock_guard<std::mutex> lock(mutex);core.Fault();Apply();}
}
