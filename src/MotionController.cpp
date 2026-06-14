#include "MotionController.h"
#include <cstdlib>   // std::abs(int64_t)

void MotionController::begin() {
    // EN 핀은 setup()에서 이미 OUTPUT+HIGH(차단)로 고정됨. 여기선 재확인.
    pinMode(Pin::EN, OUTPUT);
    disableCoils();

    _engine.init();
    _stepper = _engine.stepperConnectToPin(Pin::STEP);
    if (_stepper) {
        _stepper->setDirectionPin(Pin::DIR);
        _stepper->setSpeedInHz((uint32_t)Cfg::MAX_SPEED);   // 기본 최고속
        applyRamp(Cfg::MAX_SPEED);                          // 기본 S-커브 가감속
        // EN은 라이브러리 자동제어 미사용 — REST 구간 수동 차단 로직 유지
    }
}

// 목표속도(step/s)에 맞춰 S-커브 소프트스타트 가감속 파라미터 계산·적용.
//   a   = v_t·(1+f)/T          (정가속 구간 가속도)
//   s_h = f²·v_t·T / (1.5·(1+f))  (저크제한 선형가속 구간 스텝수)
// → 0→목표속도 도달이 RAMP_SEC초, 초반은 가속도가 0에서 선형 증가(부드러운 출발).
// setLinearAcceleration은 이후 모든 가속·감속(stopMove 포함)에 적용되어 정지도 대칭으로 부드럽다.
void MotionController::applyRamp(float targetSteps) {
    if (!_stepper || targetSteps < 1.0f) return;
    const float f = Cfg::SCURVE_HANDOVER;
    const float T = Cfg::RAMP_SEC;
    float a  = targetSteps * (1.0f + f) / T;
    float sh = f * f * targetSteps * T / (1.5f * (1.0f + f));
    _stepper->setAcceleration((uint32_t)(a < 1.0f ? 1.0f : a));
    _stepper->setLinearAcceleration((uint32_t)(sh < 0.0f ? 0.0f : sh));
}

void MotionController::beginRun(int rpm, bool fwd) {
    if (!_stepper) return;   // stepperConnectToPin 실패 시 null deref 방지
    if (rpm <= 0) { disableCoils(); _state = MotorState::IDLE; return; }

    int   safeRpm = constrain(rpm, (int)Cfg::MIN_OUTPUT_RPM, (int)Cfg::MAX_OUTPUT_RPM);
    float spd     = rpmToSteps((float)safeRpm);

    enableCoils();
    _stepper->setSpeedInHz((uint32_t)spd);
    applyRamp(spd);                      // 설정 RPM 기준 S-커브 가감속(도달 RAMP_SEC초)
    _stepper->setCurrentPosition(0);     // 위치 카운터 오버플로 방지
    if (fwd) _stepper->runForward();
    else     _stepper->runBackward();

    _targetRpm = safeRpm;
    _isFwd     = fwd;
    _stateMs   = millis();
    _state     = fwd ? MotorState::RUN_FWD : MotorState::RUN_REV;
}

void MotionController::stopImmediate() {
    haltStepper();
    disableCoils();
    _state     = MotorState::IDLE;
    _curRpm    = 0.0f;
    _targetRpm = 0;
}

void MotionController::freeze() {
    // 일시정지용 — 즉시 멈추되 targetRpm 보존(재개 시 사용)
    haltStepper();
    disableCoils();
    _state  = MotorState::IDLE;
    _curRpm = 0.0f;
}

void MotionController::requestSafeStop() {
    if (isRunning()) {
        _stepper->stopMove();              // 가속도 곡선 감속
        _state = MotorState::STOP_SAFE;
        Serial.println("[Motor] Safe stop requested (decelerating)");
    } else {
        disableCoils();
        _state     = MotorState::IDLE;
        _curRpm    = 0.0f;
        _targetRpm = 0;
    }
}

void MotionController::requestStepStop() {
    if (_stepper) _stepper->stopMove();    // 부드러운 감속 시작
    _state = MotorState::STOP_RECIPE;
}

bool MotionController::takeStepStopped() {
    if (!_stepStoppedEvt) return false;
    _stepStoppedEvt = false;
    return true;
}

void MotionController::update() {
    uint32_t now = millis();
    uint32_t el  = now - _stateMs;

    // 실측 출력축 RPM (웹/디스플레이 표시용)
    if (_state != MotorState::IDLE && _state != MotorState::REST) {
        int64_t mHz = (int64_t)_stepper->getCurrentSpeedInMilliHz();
        _curRpm = stepsToRpm((float)std::abs(mHz) / 1000.0f);
    } else {
        _curRpm = 0.0f;
    }

    switch (_state) {
        case MotorState::IDLE: break;

        case MotorState::RUN_FWD:
            if (_cycle && el >= (uint32_t)_rotIntSec * 1000UL) {
                _stepper->stopMove();
                _state = MotorState::STOP_FWD;
            }
            break;

        case MotorState::STOP_FWD:
            if (!_stepper->isRunning()) {
                disableCoils();
                _stateMs = millis();
                _state   = MotorState::REST;
            }
            break;

        case MotorState::REST:
            if (el >= Cfg::REST_MS) {
                beginRun(_targetRpm, !_isFwd);
            }
            break;

        case MotorState::RUN_REV:
            if (_cycle && el >= (uint32_t)_rotIntSec * 1000UL) {
                _stepper->stopMove();
                _state = MotorState::STOP_REV;
            }
            break;

        case MotorState::STOP_REV:
            if (!_stepper->isRunning()) {
                disableCoils();
                _stateMs = millis();
                _state   = MotorState::REST;
            }
            break;

        case MotorState::STOP_RECIPE:
            if (!_stepper->isRunning()) {
                disableCoils();
                _state          = MotorState::IDLE;
                _curRpm         = 0.0f;
                _stepStoppedEvt = true;     // RecipeRunner가 소비 → waitConfirm
            }
            break;

        case MotorState::STOP_SAFE:
            if (!_stepper->isRunning()) {
                disableCoils();
                _state     = MotorState::IDLE;
                _curRpm    = 0.0f;
                _targetRpm = 0;   // 정지 후 "target N" 잔존 표시 방지
                Serial.println("[Motor] Safe stop complete");
            }
            break;
    }
}
