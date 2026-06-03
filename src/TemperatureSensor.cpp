#include "TemperatureSensor.h"
#include <esp_task_wdt.h>

TemperatureSensor::TemperatureSensor()
    : _sensor(Pin::MAX_CS, Pin::MAX_MOSI, Pin::MAX_MISO, Pin::MAX_CLK) {}

void TemperatureSensor::begin() {
    _mux = xSemaphoreCreateMutex();
    // CS 핀 idle 고정 — 부팅 직후 부유 상태 차단
    pinMode(Pin::MAX_CS, OUTPUT);
    digitalWrite(Pin::MAX_CS, HIGH);
    // PT100 3선식 (보드 솔더점퍼 3선식 일치 확인 필요)
    _sensor.begin(MAX31865_3WIRE);
    Serial.println("[MAX31865] 초기화 완료 (3WIRE, RREF=412.0)");

    // Core 0 전용 태스크 — MAX31865 SPI를 모터 코어(Core 1)와 분리
    xTaskCreatePinnedToCore(taskTrampoline, "tempTask", 4096, this, 1, nullptr, 0);
}

float TemperatureSensor::temperature() const {
    float t = Cfg::TEMP_UNREAD;
    if (_mux && xSemaphoreTake(_mux, pdMS_TO_TICKS(5)) == pdTRUE) {
        t = _temp;
        xSemaphoreGive(_mux);
    }
    return t;
}

uint8_t TemperatureSensor::fault() const {
    uint8_t f = 0;
    if (_mux && xSemaphoreTake(_mux, pdMS_TO_TICKS(5)) == pdTRUE) {
        f = _fault;
        xSemaphoreGive(_mux);
    }
    return f;
}

void TemperatureSensor::taskTrampoline(void* self) {
    static_cast<TemperatureSensor*>(self)->taskLoop();
}

void TemperatureSensor::taskLoop() {
    esp_task_wdt_add(nullptr);   // SPI 행(hang) 시 시스템 리셋
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_task_wdt_reset();

        float   t = _sensor.temperature(Cfg::RNOMINAL, Cfg::RREF);
        uint8_t f = _sensor.readFault();

        // fault 즉시 clear 금지 — 연속 N회 누적 후에만 (단선 가시화)
        bool     shouldClear = false;
        uint16_t cnt = 0;
        if (_mux && xSemaphoreTake(_mux, pdMS_TO_TICKS(10)) == pdTRUE) {
            _temp  = t;
            _fault = f;
            if (f) {
                if (++_faultCount >= Cfg::TEMP_FAULT_CLEAR_AFTER) {
                    shouldClear = true;
                    _faultCount = 0;
                }
            } else {
                _faultCount = 0;
            }
            cnt = _faultCount;
            xSemaphoreGive(_mux);
        }
        if (shouldClear) _sensor.clearFault();

        if (f) Serial.printf("[온도 오류] 0x%02X (연속 %u회)\n", f, cnt);
        else   Serial.printf("[온도] %.2f °C\n", t);
    }
}
