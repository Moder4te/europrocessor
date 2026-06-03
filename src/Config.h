
// ================================================================
//  Config.h — 핀 배정 / 하드웨어 상수 (단일 진실원)
//  ----------------------------------------------------------------
//  모든 핀·물리 상수를 namespace Pin / Cfg 로 모음.
//  #define 매크로 → constexpr 로 전환 (타입 안전, 스코프 격리).
//
//  [ESP32-S3 GPIO 회피] strapping 0/3/45/46, UART0 43/44,
//    USB 19/20, Flash 26~32, OPI PSRAM 33~37. 아래는 모두 회피함.
// ================================================================
#pragma once
#include <Arduino.h>

namespace Pin {
    // TMC2209 스테퍼 (출력 전용)
    constexpr uint8_t STEP = 5;
    constexpr uint8_t DIR  = 6;
    constexpr uint8_t EN   = 7;    // LOW=코일 활성, HIGH=전류 차단

    // MAX31865 PT100 (소프트웨어 SPI)
    constexpr uint8_t MAX_CS   = 2;
    constexpr uint8_t MAX_MOSI = 38;
    constexpr uint8_t MAX_MISO = 39;
    constexpr uint8_t MAX_CLK  = 40;

    // ST7789 TFT (하드웨어 FSPI: SCK=12, MOSI=11 IO-MUX 직결)
    constexpr uint8_t TFT_SCK  = 12;
    constexpr uint8_t TFT_MOSI = 11;
    constexpr uint8_t TFT_CS   = 10;
    constexpr uint8_t TFT_DC   = 9;
    constexpr uint8_t TFT_RST  = 14;
    constexpr uint8_t TFT_BL   = 21;

    // EC11 인코더 + 확정 버튼
    constexpr uint8_t ENC_A    = 15;
    constexpr uint8_t ENC_B    = 16;
    constexpr uint8_t ENC_PUSH = 17;
    constexpr uint8_t KEY_OK   = 18;
}

namespace Cfg {
    // 모터 — 마이크로스텝 8 (MS1=LOW, MS2=LOW) → 1600 step/rev
    constexpr int   STEPS_PER_REV = 1600;        // 200 × 8
    constexpr float GEAR_RATIO    = 3.71f;
    constexpr float MAX_OUTPUT_RPM = 80.0f;      // 탈조 한계 기반 상한
    constexpr float MIN_OUTPUT_RPM = 5.0f;       // 실속 방지 하한

    // step/s 환산
    constexpr float MAX_SPEED = MAX_OUTPUT_RPM * GEAR_RATIO / 60.0f * STEPS_PER_REV; // ≈7915
    constexpr float MIN_SPEED = MIN_OUTPUT_RPM * GEAR_RATIO / 60.0f * STEPS_PER_REV;
    constexpr float ACCEL     = 6000.0f;         // step/s² (0→80RPM ≈1.3s)

    // 타이밍
    constexpr uint32_t REST_MS = 1000;           // 방향전환 휴지 (TMC2209 열관리)

    // MAX31865 (RREF 실측 하드코딩)
    constexpr float RREF     = 412.0f;
    constexpr float RNOMINAL = 100.0f;
    constexpr uint16_t TEMP_FAULT_CLEAR_AFTER = 5;  // 연속 fault N회 후만 clear

    // 센티넬 온도값
    constexpr float TEMP_UNREAD = -999.0f;
}
