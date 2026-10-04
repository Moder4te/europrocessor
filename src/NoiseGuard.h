// ================================================================
//  NoiseGuard.h — 전원/입력 노이즈 감시 + 모터 차단 판단
//  ----------------------------------------------------------------
//  ① 부팅: 직전 리셋 원인 집계. 브라운아웃/패닉/WDT 리셋이 연속
//     GUARD_BAD_RESETS회 → 모터 잠금 (전원 불안정·크래시 루프 중 구동 금지).
//     RTC_NOINIT 메모리라 소프트 리셋 간 유지, 전원 재투입 시 해제.
//  ② 런타임: DisplayUI가 입력 노이즈 버스트를 보고 → 윈도우 내 N회면
//     모터 정지 이벤트(takeTrip)를 loop(Core 1)에 전달.
//
//  ※ 공급전압(VM/5V) 직접 측정은 불가 — ADC 분압 배선 없음.
//    전원 노이즈는 칩 내장 브라운아웃 검출기의 리셋 기록으로 간접 감시.
// ================================================================
#pragma once
#include <Arduino.h>
#include <esp_system.h>

class NoiseGuard {
public:
    void begin();          // setup() 초반 1회 (Serial 이후)
    void update();         // Core 1 loop — 안정 가동 시 리셋 카운터 클리어
    void clearResets();    // OTA 롤백 직전 — 이전 펌웨어가 잠금을 물려받지 않게

    void inputNoise(const char* src);   // Core 0(displayTask) — 버스트 보고
    bool takeTrip();                    // Core 1 — 정지 이벤트 1회성 소비

    bool        motorLocked() const { return _locked; }
    bool        noiseRecent() const;    // 최근 INPUT_BLOCK_MS 내 버스트
    const char* status() const;         // "ok" / "noise" / "locked"
    int         resetReason() const { return (int)_resetReason; }

private:
    esp_reset_reason_t _resetReason = ESP_RST_UNKNOWN;
    bool               _locked = false;
    bool               _stable = false;

    volatile bool     _trip = false;
    volatile uint32_t _lastNoiseMs = 0;   // 0 = 노이즈 기록 없음
    uint32_t          _escWinMs = 0;
    uint8_t           _bursts = 0;
};
