// ================================================================
//  TemperatureSensor.h — MAX31865 + PT100, Core 0 비차단 폴링
//  ----------------------------------------------------------------
//  소프트웨어 SPI를 Core 0 전용 FreeRTOS 태스크에서 1Hz 폴링.
//  → Core 1의 FastAccelStepper ISR 펄스 생성에 영향 없음.
//  읽기 값은 뮤텍스로 크로스코어 보호. fault는 연속 N회 후만 clear.
// ================================================================
#pragma once
#include <Adafruit_MAX31865.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "Config.h"

class TemperatureSensor {
public:
    TemperatureSensor();

    void begin();                 // 센서 init + Core 0 태스크 기동

    // 스냅샷 읽기 (뮤텍스 보호). 미측정 시 Cfg::TEMP_UNREAD.
    float   temperature() const;
    uint8_t fault() const;

private:
    static void taskTrampoline(void* self);
    void        taskLoop();       // Core 0 루프 (1Hz)

    Adafruit_MAX31865 _sensor;
    SemaphoreHandle_t _mux = nullptr;

    float    _temp       = Cfg::TEMP_UNREAD;
    uint8_t  _fault      = 0;
    uint16_t _faultCount = 0;     // 연속 fault 카운터
};
