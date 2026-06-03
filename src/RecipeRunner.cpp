#include "RecipeRunner.h"

void RecipeRunner::begin() {
    _mux = xSemaphoreCreateMutex();
}

void RecipeRunner::clear() {
    _running = _paused = _waitConfirm = false;
    _stepIdx = 0;
    if (_mux && xSemaphoreTake(_mux, pdMS_TO_TICKS(10)) == pdTRUE) {
        _steps.clear();
        xSemaphoreGive(_mux);
    } else {
        _steps.clear();   // 뮤텍스 미초기화 시(setup 전) 직접 접근
    }
    _name = "";
}

void RecipeRunner::load(const String& name, const std::vector<StepInfo>& steps) {
    clear();
    if (_mux && xSemaphoreTake(_mux, pdMS_TO_TICKS(50)) == pdTRUE) {
        _steps = steps;
        xSemaphoreGive(_mux);
    } else {
        _steps = steps;
    }
    _name    = name;
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
        _motion.stopImmediate();
        clear();
        Serial.println("[Recipe] 모든 단계 완료");
        return;
    }
    _stepIdx     = idx;
    _stepStartMs = millis();
    _pausedMs    = 0;
    _waitConfirm = false;
    _paused      = false;
    _motion.setCycle(true);                  // 단계 내 자동 방향전환 on
    _motion.setRotIntSec(s.rotIntSec);
    _motion.setAcceleration(Cfg::ACCEL);
    Serial.printf("[Recipe] Step %d/%d: %s\n", idx + 1, total, s.name.c_str());
    _motion.beginRun(s.speedRpm, true);
}

void RecipeRunner::update() {
    // 단계 종료 감속 완료 이벤트 → 확인 대기 전환
    if (_motion.takeStepStopped()) {
        _waitConfirm = true;
        Serial.printf("[Recipe] Step %d 완료 - 확인 대기\n", _stepIdx + 1);
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
    if (!_running || _waitConfirm) return;

    if (!_paused) {
        _pausedMs = millis() - _stepStartMs;
        _paused   = true;
        _motion.freeze();
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
    _motion.setAcceleration(Cfg::ACCEL);
    _motion.beginRun(rpm, true);
}

void RecipeRunner::confirm() {
    if (_running && _waitConfirm) {
        _waitConfirm = false;
        startStep(_stepIdx + 1);
    }
}

RecipeStatus RecipeRunner::snapshot() const {
    RecipeStatus st;
    st.running     = _running;
    st.paused      = _paused;
    st.waitConfirm = _waitConfirm;
    st.name        = _name;
    st.stepIdx     = _stepIdx;

    if (_mux && xSemaphoreTake(_mux, pdMS_TO_TICKS(5)) == pdTRUE) {
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
