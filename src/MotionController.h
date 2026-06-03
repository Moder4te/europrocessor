// ================================================================
//  MotionController.h — TMC2209 스테퍼 상태 머신 (모터 전용)
//  ----------------------------------------------------------------
//  FastAccelStepper 하드웨어 ISR로 펄스 생성. EN 핀은 수동 제어
//  (REST 구간 코일 차단 = 발열 억제). 레시피를 모름 — 단계 종료는
//  requestStepStop() 으로 받고, 감속 완료는 takeStepStopped() 이벤트로 알림.
//
//  ※ 모든 상태 변경은 Core 1(loop) 단일 코어에서만. 웹/디스플레이는
//    CommandQueue 경유로만 간접 요청.
// ================================================================
#pragma once
#include <FastAccelStepper.h>
#include "Config.h"
#include "Types.h"

class MotionController {
public:
    void begin();
    void update();   // loop()에서 매 사이클 호출 — 상태 머신 + curRpm 갱신

    // ── 구동 명령 (Core 1 전용) ──
    void beginRun(int rpm, bool fwd);   // 출력축 RPM, 방향
    void stopImmediate();               // 비상: forceStop + EN 차단 + IDLE (targetRpm=0)
    void freeze();                      // 일시정지: halt + EN 차단 + IDLE (targetRpm 유지)
    void requestSafeStop();             // 가속도 곡선 감속 후 IDLE
    void requestStepStop();             // 레시피 단계 종료 감속 → STOP_RECIPE

    // ── 이벤트 소비 (RecipeRunner가 폴링) ──
    bool takeStepStopped();             // STOP_RECIPE 감속 완료 1회성 true

    // ── 설정 ──
    void setCycle(bool on)       { _cycle = on; }      // 자동 방향전환 on/off
    void setManualMode(bool on)  { _manualMode = on; } // 상태 태그(로직엔 미영향)
    void setRotIntSec(int s)     { _rotIntSec = s; }
    void setAcceleration(float a){ if (_stepper) _stepper->setAcceleration((uint32_t)a); }

    // ── 조회 ──
    MotorState state()     const { return _state; }
    float      curRpm()    const { return _curRpm; }
    int        targetRpm() const { return _targetRpm; }
    bool       isFwd()     const { return _isFwd; }
    bool       cycle()     const { return _cycle; }
    bool       manualMode()const { return _manualMode; }
    int        rotIntSec() const { return _rotIntSec; }
    bool       isRunning() const { return _stepper && _stepper->isRunning(); }

private:
    void enableCoils()  { digitalWrite(Pin::EN, LOW);  }
    void disableCoils() { digitalWrite(Pin::EN, HIGH); }
    void haltStepper()  { if (_stepper) _stepper->forceStopAndNewPosition(_stepper->getCurrentPosition()); }

    static float rpmToSteps(float rpm) { return rpm * Cfg::GEAR_RATIO / 60.0f * Cfg::STEPS_PER_REV; }
    static float stepsToRpm(float sps) { return sps / Cfg::STEPS_PER_REV * 60.0f / Cfg::GEAR_RATIO; }

    FastAccelStepperEngine _engine;
    FastAccelStepper*      _stepper = nullptr;

    MotorState _state     = MotorState::IDLE;
    int        _targetRpm = 0;
    float      _curRpm    = 0.0f;
    bool       _isFwd     = true;
    uint32_t   _stateMs   = 0;
    int        _rotIntSec = 30;
    bool       _cycle     = true;     // = !noCycle
    bool       _manualMode = false;   // 상태 표시용 태그
    bool       _stepStoppedEvt = false;
};
