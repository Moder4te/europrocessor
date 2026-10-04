#include "HwSafety.h"
#include "Config.h"

void HwSafety::begin() {
    _diagPin = boardDiagPin();
    _vmPin   = boardVmPin();
    _vmRatio = boardVmRatio();
    if (_diagPin) pinMode(_diagPin, INPUT_PULLDOWN);   // 미연결·단선 시 정상(LOW)으로 — 외부 풀다운 권장
    if (_vmPin)   { analogSetPinAttenuation(_vmPin, ADC_11db); pinMode(_vmPin, ANALOG); }
    Serial.printf("[HwSafety] DIAG %s, VM %s\n",
                  _diagPin ? String("GPIO" + String(_diagPin)).c_str() : "미배선",
                  _vmPin ? String("GPIO" + String(_vmPin) + " ×" + String(_vmRatio, 2)).c_str() : "미배선");
}

uint8_t HwSafety::conditions(bool motorActive) {
    uint8_t c = 0;
    if (_diagPin) {
        if (digitalRead(_diagPin) == HIGH) { if (_diagCount < 255) _diagCount++; }
        else _diagCount = 0;
        if (_diagCount >= Cfg::DIAG_DEBOUNCE) c |= DRIVER;
    }
    if (_vmPin) {
        const float v = analogReadMilliVolts(_vmPin) / 1000.0f * _vmRatio;
        _vm = (_vm < 0) ? v : _vm * 0.7f + v * 0.3f;          // 가벼운 평활 (노이즈 오경보 방지)
        if (_vm > Cfg::VM_MAX_V) c |= VM_HIGH;
        if (motorActive && _vm < Cfg::VM_MIN_V) {
            if (!_lowSinceMs) _lowSinceMs = millis() ? millis() : 1;
            if (millis() - _lowSinceMs >= Cfg::VM_LOW_GRACE_MS) c |= VM_LOW;
        } else {
            _lowSinceMs = 0;
        }
    }
    return c;
}

uint8_t HwSafety::poll(bool motorActive) {
    if (!_diagPin && !_vmPin) return 0;
    const uint32_t now = millis();
    if (now - _lastPollMs < 20) return 0;   // 50Hz면 충분 (ADC 부하 최소화)
    _lastPollMs = now;
    const uint8_t c = conditions(motorActive);
    const uint8_t fresh = c & ~_fault;
    if (fresh) {
        _fault |= fresh;
        Serial.printf("[HwSafety] ★고장 감지: %s (VM %.1fV)★ — 모터 차단·잠금\n", text(fresh), (double)_vm);
    }
    return fresh;
}

bool HwSafety::clear() {
    _diagCount = 0; _lowSinceMs = 0;
    const uint8_t c = conditions(false);   // 정지 상태 기준으로 재확인 (VM_LOW는 구동 중에만 판정)
    if (c) { Serial.printf("[HwSafety] 해제 거부 — 원인 지속: %s\n", text(c)); return false; }
    _fault = 0;
    Serial.println("[HwSafety] 고장 해제");
    return true;
}

const char* HwSafety::text(uint8_t f) {
    if (f & DRIVER)  return "driver";
    if (f & VM_HIGH) return "vm_high";
    if (f & VM_LOW)  return "vm_low";
    return "";
}
