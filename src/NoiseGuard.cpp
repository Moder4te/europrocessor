#include "NoiseGuard.h"
#include "Config.h"

// 소프트 리셋(패닉/WDT/브라운아웃)을 넘어 유지되는 카운터. 전원 투입 직후엔 쓰레기값 → MAGIC 검사.
static RTC_NOINIT_ATTR uint32_t s_magic;
static RTC_NOINIT_ATTR uint32_t s_badResets;
static constexpr uint32_t MAGIC = 0x4E475244;   // "NGRD"

static bool isAbnormal(esp_reset_reason_t r) {
    return r == ESP_RST_BROWNOUT || r == ESP_RST_PANIC ||
           r == ESP_RST_INT_WDT  || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
}

void NoiseGuard::begin() {
    _resetReason = esp_reset_reason();
    if (s_magic != MAGIC || _resetReason == ESP_RST_POWERON) { s_magic = MAGIC; s_badResets = 0; }
    if (isAbnormal(_resetReason)) s_badResets++;
    _locked = s_badResets >= Cfg::GUARD_BAD_RESETS;

    if (_resetReason == ESP_RST_BROWNOUT)
        Serial.println("[Guard] ⚠ 직전 리셋 = 브라운아웃 (전원 강하/노이즈) — 전원부 점검 필요");
    Serial.printf("[Guard] reset=%d 연속 비정상 리셋 %u/%u%s\n", (int)_resetReason,
                  (unsigned)s_badResets, (unsigned)Cfg::GUARD_BAD_RESETS,
                  _locked ? " → ★모터 잠금★ (전원 재투입으로 해제)" : "");
}

void NoiseGuard::update() {
    if (!_stable && millis() >= Cfg::GUARD_STABLE_MS) {
        s_badResets = 0;   // 정상 가동 확인 — 다음 리셋부터 새로 집계 (현재 잠금은 유지)
        _stable = true;
    }
}

void NoiseGuard::clearResets() { s_badResets = 0; }

void NoiseGuard::inputNoise(const char* src) {
    uint32_t now = millis();
    _lastNoiseMs = now ? now : 1;
    if (_bursts == 0 || now - _escWinMs > Cfg::NOISE_ESCALATE_WIN_MS) { _escWinMs = now; _bursts = 0; }
    _bursts++;
    Serial.printf("[Guard] 입력 노이즈 버스트: %s (%u/%u)\n", src,
                  (unsigned)_bursts, (unsigned)Cfg::NOISE_ESCALATE_BURSTS);
    if (_bursts >= Cfg::NOISE_ESCALATE_BURSTS) {
        _bursts = 0;
        _trip   = true;
        Serial.println("[Guard] 노이즈 지속 → 모터 정지 요청");
    }
}

bool NoiseGuard::takeTrip() {
    if (!_trip) return false;
    _trip = false;
    return true;
}

bool NoiseGuard::noiseRecent() const {
    uint32_t t = _lastNoiseMs;
    return t && millis() - t < Cfg::INPUT_BLOCK_MS;
}

const char* NoiseGuard::status() const {
    if (_locked)       return "locked";
    if (noiseRecent()) return "noise";
    return "ok";
}
