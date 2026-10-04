#include "TemperatureSensor.h"
#include <esp_task_wdt.h>

TemperatureSensor::TemperatureSensor()
    : _sensor(pins().MAX_CS, pins().MAX_MOSI, pins().MAX_MISO, pins().MAX_CLK) {}

void TemperatureSensor::begin() {
    if (!Cfg::TEMP_SENSOR_PRESENT) {
        // 임시 격리 — 센서 init·태스크 생략. _mux=null이라 getter가 기본값
        // (TEMP_UNREAD / 0) 반환 → 디스플레이 "--.-", fault 스팸 없음.
        Serial.println("[MAX31865] 비활성 (TEMP_SENSOR_PRESENT=false) — 온도 기능 격리");
        return;
    }
    _mux = xSemaphoreCreateMutex();
    // CS 핀 idle 고정 — 부팅 직후 부유 상태 차단
    pinMode(pins().MAX_CS, OUTPUT);
    digitalWrite(pins().MAX_CS, HIGH);
    // 결선 모드는 Cfg::RTD_WIRES로 (보드 솔더점퍼와 일치). 2/4선은 칩 config가
    // 동일(D4=0), 3선만 D4=1. enum: 2WIRE=0, 3WIRE=1, 4WIRE=0.
    const max31865_numwires_t wm = (Cfg::RTD_WIRES == 3) ? MAX31865_3WIRE
                                 : (Cfg::RTD_WIRES == 4) ? MAX31865_4WIRE
                                                         : MAX31865_2WIRE;
    _sensor.begin(wm);
    Serial.printf("[MAX31865] 초기화 완료 (%dWIRE, RREF=%.1f)\n",
                  (int)Cfg::RTD_WIRES, (double)Cfg::RREF);

    // ── 진단 B: SPI 라운드트립 자가진단 (결과는 tempTask가 반복 출력) ──
    //   threshold 레지스터(읽기·쓰기 가능, RTD 아날로그단과 무관)에 알려진 값을
    //   write → read 비교. 일치하면 MOSI(쓰기)·CLK·CS·MISO(읽기) 전 경로 + 칩이
    //   모두 정상. 0x0000/0xFFFF/불일치면 버스 미통신 또는 칩 불량.
    //   ※ 테스트 후 반드시 기본값(0,0xFFFF)으로 복원 — 안 그러면 임계 fault 오탐.
    {
        const uint16_t TL = 0x1234, TH = 0x5678;   // 비트 패턴 섞인 짝수값
        _sensor.setThresholds(TL, TH);
        _spiRL = _sensor.getLowerThreshold();
        _spiRH = _sensor.getUpperThreshold();
        _spiOk = (_spiRL == TL && _spiRH == TH);
        _sensor.setThresholds(0, 0xFFFF);   // 기본값 복원 (오탐 방지)
        _sensor.clearFault();
    }

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
    int selfTestPrints = 0;      // USB-CDC 모니터 지각 대비 자가진단 반복 출력

    // ── Core 0 SPI 검증 — readRTD가 실제 도는 이 코어에서 라운드트립 재확인 ──
    //   begin()의 자가진단은 Core 1에서 돌았음. 여기(Core 0)서도 통과하면
    //   "SW SPI가 Core 0에선 안 됨" 가능성까지 배제 → 소프트웨어 완전 무죄.
    bool core0Ok = false;
    {
        _sensor.setThresholds(0x2A55, 0x55AA);
        uint16_t rl = _sensor.getLowerThreshold();
        uint16_t rh = _sensor.getUpperThreshold();
        core0Ok = (rl == 0x2A55 && rh == 0x55AA);
        _sensor.setThresholds(0, 0xFFFF);   // 복원 (오탐 방지)
        _sensor.clearFault();
        Serial.printf("[MAX31865] Core0 SPI 검증: write(0x2A55,0x55AA) read(0x%04X,0x%04X) → %s\n",
                      rl, rh, core0Ok ? "정상 — Core0도 SPI OK (SW 무죄)"
                                      : "실패 — Core0 SPI 문제!");
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_task_wdt_reset();

        // 부팅 SPI 자가진단 결과 — 모니터가 늦게 붙어도 잡히도록 첫 ~15초 반복
        if (selfTestPrints < 15) {
            Serial.printf("[MAX31865] SPI 자가진단: write(0x1234,0x5678) read(0x%04X,0x%04X) → %s\n",
                          _spiRL, _spiRH,
                          _spiOk ? "정상 — 칩·SPI OK"
                                 : ((_spiRL==0xFFFF||_spiRH==0xFFFF||_spiRL==0x0000||_spiRH==0x0000)
                                      ? "실패 — 버스 미통신/칩 불량"
                                      : "실패 — 비트 불일치(접촉/잡음)"));
            selfTestPrints++;
        }

        // raw RTD 레지스터까지 읽어 SPI 통신 두절과 실제 RTD 결함을 구분.
        //   raw 0x0000(SDO LOW 고정) / 0x7FFF(SDO HIGH·floating) → 미통신(배선/전원)
        //   raw 중간값 + fault → 실제 RTD 단선/단락 또는 임계값 초과
        uint16_t raw = _sensor.readRTD();
        float    t   = _sensor.calculateTemperature(raw, Cfg::RNOMINAL, Cfg::RREF);
        uint8_t  f   = _sensor.readFault();
        const bool noComm = (raw == 0x0000 || raw == 0x7FFF);
        // 미통신이면 칩 fault 레지스터도 못 믿음 → D0(칩 미사용 비트)을 SW 플래그로 fault 처리.
        //   (기존: fault=0이면 raw 0x7FFF → ~988°C가 정상값으로 표시됨)
        if (noComm) f |= 0x01;

        // 스파이크 제거 — 직전 정상값 대비 TEMP_MAX_STEP_C 초과 점프는 노이즈로 보고 버림(직전값 유지).
        //   연속 TEMP_SPIKE_ACCEPT_AFTER회 이어지면 실제 변화로 수용.
        bool spike = false;
        if (!f) {
            if (_lastGood != Cfg::TEMP_UNREAD && fabsf(t - _lastGood) > Cfg::TEMP_MAX_STEP_C &&
                ++_spikeCount < Cfg::TEMP_SPIKE_ACCEPT_AFTER) {
                spike = true;
                Serial.printf("[온도] 스파이크 무시: %.2f °C (기준 %.2f, %u회)\n",
                              t, _lastGood, (unsigned)_spikeCount);
                t = _lastGood;
            } else {
                _spikeCount = 0;
                _lastGood   = t;
            }
        }

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

        if (f) {
            // fault 비트 디코드 — 어느 단자가 개방인지 지목 (MAX31865 datasheet)
            char fb[112] = "";
            auto add = [&](const char* s){ strncat(fb, s, sizeof(fb) - strlen(fb) - 1); };
            if (f & 0x80) add("RTD>상한 ");
            if (f & 0x40) add("RTD<하한 ");
            if (f & 0x20) add("REFIN-과전압 ");
            if (f & 0x10) add("REFIN-개방(기준/-측) ");
            if (f & 0x08) add("RTDIN-개방(단일선/-측) ");
            if (f & 0x04) add("과·저전압(완전개방?) ");
            if (f & 0x01) add("SPI미통신 ");
            Serial.printf("[온도 오류] fault=0x%02X [%s] raw=0x%04X (연속 %u회)%s\n",
                          f, fb, raw, cnt, noComm ? " ← SPI미통신" : "");
        } else if (!spike) {
            Serial.printf("[온도] %.2f °C (raw=0x%04X)\n", t, raw);
        }
    }
}
