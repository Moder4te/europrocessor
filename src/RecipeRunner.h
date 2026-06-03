// ================================================================
//  RecipeRunner.h — 레시피 실행 (단계 시퀀스 + 확인대기)
//  ----------------------------------------------------------------
//  steps 벡터는 웹 buildStatus(Core 0)이 동시에 읽으므로 뮤텍스 보호.
//  모터는 MotionController 경유로만 구동. 단계 종료 감속 완료는
//  motion.takeStepStopped() 이벤트로 받아 waitConfirm 전환.
// ================================================================
#pragma once
#include "Types.h"
#include "MotionController.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// 상태 스냅샷 — 웹/디스플레이 표시용 (뮤텍스 밖에서 안전하게 사용)
struct RecipeStatus {
    bool   running     = false;
    bool   paused      = false;
    bool   waitConfirm = false;
    String name;
    int    stepIdx     = 0;
    int    stepTotal   = 0;
    String curName;
    String nextName;
    int    stepDurSec  = 0;
    long   stepRemSec  = 0;
};

class RecipeRunner {
public:
    explicit RecipeRunner(MotionController& motion) : _motion(motion) {}

    void begin();
    void update();    // loop()에서 매 사이클 호출

    // 레시피 적용 — steps 교체 후 0단계부터 실행
    void load(const String& name, const std::vector<StepInfo>& steps);
    void clear();     // 레시피 데이터 초기화 (모터는 건드리지 않음)

    void pauseToggle();
    void confirm();

    bool running()     const { return _running; }
    bool paused()      const { return _paused; }
    bool waitConfirm() const { return _waitConfirm; }
    bool active()      const { return _running || _paused || _waitConfirm; }

    RecipeStatus snapshot() const;

private:
    void startStep(int idx);

    MotionController& _motion;
    SemaphoreHandle_t _mux = nullptr;

    bool   _running = false, _paused = false, _waitConfirm = false;
    String _name;
    int    _stepIdx = 0;
    uint32_t _stepStartMs = 0, _pausedMs = 0;
    std::vector<StepInfo> _steps;
};
