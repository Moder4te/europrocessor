// ================================================================
//  HwSafety.h — 드라이버 고장(DIAG) / 모터 전원(VM) 전압 감시
//  ----------------------------------------------------------------
//  핀은 보드별 PinMap.h(diagPin/vmPin/vmRatio). 0이면 해당 감시 비활성.
//  Core 1 loop에서 poll() — 고장 발생 시 새 고장 비트를 돌려주고 래치.
//  main이 모터 즉시 차단 + 레시피 일시정지 보존 + 모터 잠금, 사용자가 해제.
//
//  · DRIVER  : DIAG HIGH 연속 3회(≥60ms) — TMC2209 과열/단락 보호 동작
//  · VM_LOW  : 모터 구동 중 VM < VM_MIN_V 가 500ms 지속 (전원 꺼짐/어댑터 과전류 보호)
//              정지 중엔 판정 안 함 (USB로만 켰을 때 오경보 방지)
//  · VM_HIGH : VM > VM_MAX_V 즉시 (TMC2209 절대최대 29V 보호, 감속 역기전력 포함)
// ================================================================
#pragma once
#include <Arduino.h>

class HwSafety {
public:
    enum : uint8_t { DRIVER = 0x01, VM_LOW = 0x02, VM_HIGH = 0x04 };

    void    begin();
    uint8_t poll(bool motorActive);    // 새로 발생한 고장 비트 (없으면 0)
    bool    clear();                   // 원인이 사라졌으면 래치 해제 → true
    uint8_t fault() const { return _fault; }
    float   vm() const    { return _vm; }      // 측정 VM (V), 미배선 -1
    bool    hasVm() const { return _vmPin != 0; }
    static const char* text(uint8_t f);        // "driver" / "vm_low" / "vm_high" / ""

private:
    uint8_t  conditions(bool motorActive);     // 현재 순간의 이상 비트

    uint8_t  _diagPin = 0, _vmPin = 0;
    float    _vmRatio = 0;
    float    _vm = -1.0f;
    uint8_t  _fault = 0;
    uint8_t  _diagCount = 0;
    uint32_t _lowSinceMs = 0;
    uint32_t _lastPollMs = 0;
};
