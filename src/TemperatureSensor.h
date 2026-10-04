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

    // ── 보정 (2점/1점) — 측정 저항에 선형 보정: R_cal = gain·R_meas + offset(Ω) ──
    //   RREF 오차·PT100 공차·2선식 리드선 저항을 함께 흡수. NVS "tcal"에 저장(보드별).
    float rtdOhm() const;                              // 보정 전 측정 저항 (Ω), 미측정 0
    float calGain()   const { return _calGain; }
    float calOffset() const { return _calOffset; }
    bool  setCalibration(float gain, float offsetOhm); // 범위 밖이면 false. NVS 저장(운전 중 호출 금지)
    static float celsiusToOhm(float t);                // PT100 Callendar–Van Dusen
    static float ohmToCelsius(float r);

private:
    static void taskTrampoline(void* self);
    void        taskLoop();       // Core 0 루프 (1Hz)

    Adafruit_MAX31865 _sensor;
    SemaphoreHandle_t _mux = nullptr;

    float    _temp       = Cfg::TEMP_UNREAD;
    float    _ohm        = 0.0f;                  // 보정 전 측정 저항
    volatile float _calGain   = 1.0f;
    volatile float _calOffset = 0.0f;
    uint8_t  _fault      = 0;
    uint16_t _faultCount = 0;     // 연속 fault 카운터
    float    _lastGood   = Cfg::TEMP_UNREAD;   // 스파이크 판정 기준 (tempTask 전용)
    uint8_t  _spikeCount = 0;

    // 부팅 SPI 자가진단 결과 (begin서 1회 측정 → tempTask가 반복 출력)
    bool     _spiOk      = false;
    uint16_t _spiRL      = 0;
    uint16_t _spiRH      = 0;
};
