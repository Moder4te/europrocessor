#include "RecipeRunner.h"

void RecipeRunner::begin() {
    _mux = xSemaphoreCreateMutex();
}

void RecipeRunner::clear() {
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

void RecipeRunner::load(const String& name, const std::vector<StepInfo>& steps) {
    clear();
    if (_mux && xSemaphoreTake(_mux, pdMS_TO_TICKS(50)) == pdTRUE) {
        _steps = steps;
        _name  = name;
        xSemaphoreGive(_mux);
    } else {
        _steps = steps;
        _name  = name;
    }
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
    Serial.printf("[Recipe] Step %d/%d: %s\n", idx + 1, total, s.name.c_str());
    _motion.beginRun(s.speedRpm, true);   // 가감속은 beginRun이 S-커브로 적용
}

void RecipeRunner::update() {
    // 단계 종료 감속 완료 이벤트 → 확인 대기 전환
    if (_motion.takeStepStopped()) {
        _waitConfirm = true;
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
    _motion.beginRun(rpm, true);   // 가감속은 beginRun이 S-커브로 적용
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
    Serial.printf("[Recipe] 단계 이동 → %d/%d\n", idx + 1, total);
    switchTo(idx);
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
