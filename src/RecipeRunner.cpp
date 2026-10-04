#include "RecipeRunner.h"
#include <Preferences.h>
#include <ArduinoJson.h>

static const char* NVS_NS = "rec";

void RecipeRunner::begin() {
    _mux = xSemaphoreCreateMutex();

    // 이전 부팅에서 끝나지 않은 레시피 → 복구 대기 (모터는 돌리지 않음)
    Preferences p;
    if (!p.begin(NVS_NS, true)) return;
    if (p.getBool("active", false)) {
        JsonDocument doc;
        if (!deserializeJson(doc, p.getString("steps", "[]"))) {
            for (JsonObject s : doc.as<JsonArray>()) {
                StepInfo si;
                si.name = s["n"] | String("Step");
                si.speedRpm = s["r"] | 50; si.durSec = s["d"] | 60; si.rotIntSec = s["i"] | 30;
                _recovSteps.push_back(si);
            }
        }
        _recov.name      = p.getString("name", "");
        _recov.stepIdx   = p.getInt("idx", 0);
        _recov.elapsedMs = p.getUInt("el", 0);
        _recov.state     = p.getUChar("st", 0);
        _recov.total     = (int)_recovSteps.size();
        if (_recov.total > 0 && _recov.stepIdx >= 0 && _recov.stepIdx < _recov.total) {
            _recov.stepName   = _recovSteps[_recov.stepIdx].name;
            _recov.stepDurSec = _recovSteps[_recov.stepIdx].durSec;
            _recov.valid      = true;
            Serial.printf("[Recover] 미완료 레시피 발견: %s %d/%d 단계, 경과 %us (상태 %u) — 사용자 선택 대기\n",
                          _recov.name.c_str(), _recov.stepIdx + 1, _recov.total,
                          (unsigned)(_recov.elapsedMs / 1000), (unsigned)_recov.state);
        }
    }
    p.end();
}

// ──────────────────────────────────────────────────────────────
// 영구 저장 (NVS) — 모터가 멈춘 순간에만
// ──────────────────────────────────────────────────────────────
bool RecipeRunner::motorQuiet() const {
    const MotorState s = _motion.state();
    return s == MotorState::IDLE || s == MotorState::REST;
}

uint32_t RecipeRunner::currentElapsedMs() const {
    if (_waitConfirm) {   // 단계 완료 상태 = 단계 시간 전부
        int d = 0;
        if (_stepIdx < (int)_steps.size()) d = _steps[_stepIdx].durSec;
        return (uint32_t)d * 1000UL;
    }
    return _paused ? _pausedMs : (millis() - _stepStartMs);
}

void RecipeRunner::writeRecord() {
    Preferences p;
    if (!p.begin(NVS_NS, false)) return;
    if (_persistSteps) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (const StepInfo& s : _steps) {
            JsonObject o = arr.add<JsonObject>();
            o["n"] = s.name; o["r"] = s.speedRpm; o["d"] = s.durSec; o["i"] = s.rotIntSec;
        }
        String js; serializeJson(doc, js);
        p.putString("name", _name);
        p.putString("steps", js);
    }
    p.putBool("active", true);
    p.putInt("idx", _stepIdx);
    p.putUInt("el", currentElapsedMs());
    p.putUChar("st", _waitConfirm ? 2 : _paused ? 1 : 0);
    p.end();
    _persistActive = true;
    _persistDirty = _persistSteps = false;
    _lastPersistMs = millis();
}

void RecipeRunner::persistNowOrLater() {
    _persistDirty = true;
    if (motorQuiet()) writeRecord();
}

void RecipeRunner::flushPersist() {
    if (!motorQuiet()) return;   // 회전·감속 중엔 플래시 금지
    if (_persistErase) {
        Preferences p;
        if (p.begin(NVS_NS, false)) { p.clear(); p.end(); }
        _persistErase = _persistActive = false;
        _persistDirty = _persistSteps = false;
        return;
    }
    const bool restTick = _running && !_paused && !_waitConfirm &&
                          _motion.state() == MotorState::REST && millis() - _lastPersistMs >= 10000;
    if (_persistDirty || restTick) writeRecord();
}

// ──────────────────────────────────────────────────────────────
void RecipeRunner::clear() {
    // 기록 삭제는 이번 부팅에서 쓴 기록이 있을 때만 — 수동 운전 시작(stopAll) 등으로
    // 이전 부팅의 복구 대기 기록을 조용히 지우지 않게
    if (_running || _paused || _waitConfirm || _persistActive) _persistErase = true;
    _running = _paused = _waitConfirm = false;
    _pendingStep = -1;
    _stepIdx = 0;
    // _name 은 String(힙 버퍼) — Core 0 snapshot()이 동시 복사하므로 뮤텍스 안에서만 변경
    if (_mux && xSemaphoreTake(_mux, pdMS_TO_TICKS(10)) == pdTRUE) {
        _steps.clear();
        _name = "";
        xSemaphoreGive(_mux);
    } else {
        _steps.clear();   // 뮤텍스 미초기화 시(setup 전) 직접 접근
        _name = "";
    }
}

void RecipeRunner::setRecipe(const String& name, const std::vector<StepInfo>& steps) {
    clear();
    _persistErase = false;      // 새 기록이 덮어씀
    _persistSteps = true;
    if (_recov.valid) { _recov.valid = false; Serial.println("[Recover] 새 레시피 시작 — 이전 복구 기록 폐기"); }
    if (_mux && xSemaphoreTake(_mux, pdMS_TO_TICKS(50)) == pdTRUE) {
        _steps = steps;
        _name  = name;
        xSemaphoreGive(_mux);
    } else {
        _steps = steps;
        _name  = name;
    }
}

void RecipeRunner::load(const String& name, const std::vector<StepInfo>& steps) {
    setRecipe(name, steps);
    _running = true;
    startStep(0);
}

void RecipeRunner::startStep(int idx) {
    StepInfo s;
    int total = 0;
    if (xSemaphoreTake(_mux, pdMS_TO_TICKS(50)) == pdTRUE) {
        total = (int)_steps.size();
        if (idx < total) s = _steps[idx];
        xSemaphoreGive(_mux);
    }
    if (idx >= total) {
        // skip 으로 회전 중에도 도달 가능 — 즉시 정지 대신 감속 정지
        _motion.requestSafeStop();
        clear();                // 기록 삭제는 감속 끝난 뒤 flushPersist가 수행
        Serial.println("[Recipe] 모든 단계 완료");
        return;
    }
    _stepIdx     = idx;
    _stepStartMs = millis();
    _pausedMs    = 0;
    _waitConfirm = false;
    _paused      = false;
    persistNowOrLater();        // 모터가 아직 멈춰 있을 때 단계 번호를 먼저 기록
    _motion.setCycle(true);                  // 단계 내 자동 방향전환 on
    _motion.setRotIntSec(s.rotIntSec);
    Serial.printf("[Recipe] Step %d/%d: %s\n", idx + 1, total, s.name.c_str());
    _motion.beginRun(s.speedRpm, true);   // 가감속은 beginRun이 S-커브로 적용
}

void RecipeRunner::update() {
    flushPersist();

    // 단계 종료 감속 완료 이벤트 → 확인 대기 전환
    if (_motion.takeStepStopped()) {
        _waitConfirm = true;
        persistNowOrLater();
        Serial.printf("[Recipe] Step %d 완료 - 확인 대기\n", _stepIdx + 1);
    }

    // 단계 전환 대기 — 감속 정지(IDLE) 완료 후 목표 단계 시작
    if (_pendingStep >= 0) {
        if (_motion.state() == MotorState::IDLE) {
            int idx = _pendingStep;
            _pendingStep = -1;
            startStep(idx);
        }
        return;
    }

    if (!_running || _paused || _waitConfirm) return;
    if (_motion.state() == MotorState::STOP_RECIPE) return;  // 감속 진행 중

    int  durSec = 0;
    bool valid  = false;
    if (xSemaphoreTake(_mux, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (!_steps.empty() && _stepIdx < (int)_steps.size()) {
            durSec = _steps[_stepIdx].durSec;
            valid  = true;
        }
        xSemaphoreGive(_mux);
    }
    if (!valid) return;

    uint32_t el    = millis() - _stepStartMs;
    uint32_t durMs = (uint32_t)durSec * 1000UL;
    if (el >= durMs) {
        _motion.requestStepStop();   // 감속 → STOP_RECIPE → takeStepStopped
    }
}

void RecipeRunner::pauseToggle() {
    if (!_running || _waitConfirm || _pendingStep >= 0) return;
    // 단계 종료 감속 중 정지 요청하면 감속완료 이벤트가 유실되어
    // waitConfirm 으로 못 넘어감 — 곧 확인대기로 전환되므로 무시
    if (!_paused && _motion.state() == MotorState::STOP_RECIPE) return;

    if (!_paused) {
        _pausedMs = millis() - _stepStartMs;
        _paused   = true;
        _persistDirty = true;        // 감속이 끝나 멈추면 flushPersist가 기록
        _motion.requestSafeStop();   // S-커브 감속 정지 후 코일 차단 (기존: 즉시 정지 = 탱크 충격)
        return;
    }
    // 재개 — 현재 단계 속도 스냅샷
    int rpm = 0;
    if (xSemaphoreTake(_mux, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (_stepIdx < (int)_steps.size()) rpm = _steps[_stepIdx].speedRpm;
        xSemaphoreGive(_mux);
    }
    if (rpm <= 0) return;
    _stepStartMs = millis() - _pausedMs;
    _paused      = false;
    persistNowOrLater();           // 멈춘 상태에서 '진행 중'으로 기록 후 기동
    _motion.beginRun(rpm, true);   // 가감속은 beginRun이 S-커브로 적용
}

void RecipeRunner::forcePause() {
    if (!_running || _paused || _waitConfirm) return;
    _pendingStep = -1;
    _pausedMs = millis() - _stepStartMs;
    _paused   = true;
    persistNowOrLater();
}

void RecipeRunner::confirm() {
    if (_running && _waitConfirm) {
        _waitConfirm = false;
        startStep(_stepIdx + 1);
    }
}

// 단계 전환 공통 — 회전 중이면 감속 정지 먼저, IDLE 되면 update()가 idx 시작.
//   (기존: 회전 중 바로 beginRun → 감속 없이 속도/방향 급변)
//   idx가 마지막 단계 초과면 startStep이 레시피 종료 처리.
void RecipeRunner::switchTo(int idx) {
    _waitConfirm = false;
    _paused      = false;
    _pendingStep = idx;
    _motion.requestSafeStop();
}

// 현재 단계를 종료하고 다음 단계로 진행 (running/paused/waitConfirm 무관).
void RecipeRunner::skipStep() {
    if (!_running) return;
    switchTo(_stepIdx + 1);
}

void RecipeRunner::gotoStep(int idx) {
    if (!_running || idx < 0) return;
    int total = 0;
    if (xSemaphoreTake(_mux, pdMS_TO_TICKS(50)) == pdTRUE) {
        total = (int)_steps.size();
        xSemaphoreGive(_mux);
    }
    if (idx >= total) return;
    Serial.printf("[Recipe] 단계 이동 → %d/%d (감속 후)\n", idx + 1, total);
    switchTo(idx);
}

// ──────────────────────────────────────────────────────────────
// 정전·리셋 복구
// ──────────────────────────────────────────────────────────────
RecoveryInfo RecipeRunner::recovery() const {
    RecoveryInfo r;
    if (_mux && xSemaphoreTake(_mux, pdMS_TO_TICKS(5)) == pdTRUE) { r = _recov; xSemaphoreGive(_mux); }
    return r;
}

void RecipeRunner::applyRecovery(RecoverAction a) {
    if (!_recov.valid) return;
    if (a == RecoverAction::DISCARD) {
        _recov.valid = false;
        _persistErase = true;   // 모터 정지 상태 → 다음 update에서 삭제
        Serial.println("[Recover] 복구 취소 — 기록 삭제");
        return;
    }
    if (_motion.state() != MotorState::IDLE) return;   // 모터가 돌고 있으면 거부

    const RecoveryInfo r = _recov;
    const std::vector<StepInfo> steps = _recovSteps;
    setRecipe(r.name, steps);   // _recov.valid = false 처리 포함
    _running = true;

    const uint32_t durMs = (uint32_t)steps[r.stepIdx].durSec * 1000UL;
    switch (a) {
        case RecoverAction::RESUME:
            if (r.state == 2) {                  // 확인대기였음 → 그대로 대기 (모터 정지)
                _stepIdx = r.stepIdx; _waitConfirm = true;
                persistNowOrLater();
            } else if (r.state == 1) {           // 일시정지였음 → 일시정지로 복원 (재개는 사용자)
                _stepIdx = r.stepIdx; _paused = true;
                _pausedMs = r.elapsedMs < durMs ? r.elapsedMs : durMs;
                _stepStartMs = millis() - _pausedMs;
                persistNowOrLater();
            } else {                             // 진행 중이었음 → 남은 시간부터 이어서
                startStep(r.stepIdx);
                const uint32_t el = r.elapsedMs < durMs ? r.elapsedMs : durMs;
                _stepStartMs = millis() - el;
            }
            break;
        case RecoverAction::RESTART_STEP: startStep(r.stepIdx);     break;
        case RecoverAction::NEXT_STEP:    startStep(r.stepIdx + 1); break;
        default: break;
    }
    Serial.printf("[Recover] 복구 실행 (action %u): %s %d/%d\n", (unsigned)a, r.name.c_str(), r.stepIdx + 1, r.total);
}

void RecipeRunner::copySteps(String& name, std::vector<StepInfo>& out) const {
    out.clear();
    if (_mux && xSemaphoreTake(_mux, pdMS_TO_TICKS(20)) == pdTRUE) {
        name = _name;
        out  = _steps;
        xSemaphoreGive(_mux);
    }
}

RecipeStatus RecipeRunner::snapshot() const {
    RecipeStatus st;
    st.running     = _running;
    st.paused      = _paused;
    st.waitConfirm = _waitConfirm;
    st.stepIdx     = _stepIdx;

    if (_mux && xSemaphoreTake(_mux, pdMS_TO_TICKS(5)) == pdTRUE) {
        st.name      = _name;   // String 복사는 뮤텍스 안에서 (clear/load 와 레이스 방지)
        st.stepTotal = (int)_steps.size();
        if (_running && _stepIdx < st.stepTotal) {
            const StepInfo& s = _steps[_stepIdx];
            st.curName    = s.name;
            st.stepDurSec = s.durSec;
            if (_stepIdx + 1 < st.stepTotal) st.nextName = _steps[_stepIdx + 1].name;
            uint32_t el = _paused ? _pausedMs : (millis() - _stepStartMs);
            int64_t  r  = (int64_t)s.durSec * 1000LL - (int64_t)el;
            if (r < 0) r = 0;
            st.stepRemSec = (long)(r / 1000LL);
        }
        xSemaphoreGive(_mux);
    }
    return st;
}
