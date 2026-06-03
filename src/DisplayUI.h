// ================================================================
//  DisplayUI.h — ST7789 TFT + EC11 인코더 + KO 버튼 UI (Core 0 태스크)
//  ----------------------------------------------------------------
//  display_ui.ino 를 클래스 경계로 이관. 렌더 내부는 .cpp 파일스코프
//  static 으로 유지(C 콜백이 tft 접근). 데이터는 주입된 deps에서 읽고,
//  명령은 CommandQueue 로만 enqueue (Core 1이 처리).
//  화면보호기 설정 제공을 위해 ISaver 구현.
// ================================================================
#pragma once
#include "ISaver.h"
#include "MotionController.h"
#include "RecipeRunner.h"
#include "TemperatureSensor.h"
#include "CommandQueue.h"

class DisplayUI : public ISaver {
public:
    struct Deps {
        MotionController*  motion;
        RecipeRunner*      recipe;
        TemperatureSensor* temp;
        CommandQueue*      cmd;
    };

    void begin(const Deps& deps);   // deps 주입 + 화면보호기 prefs 로드
    void start();                   // Core 0 displayTask 기동
    uint32_t freeStack() const;     // 잔여 스택 (진단)

    // ── ISaver ──
    bool saverEnabled() const override;
    int  saverTimeoutSec() const override;
    void setSaverEnabled(bool en) override;
    void setSaverTimeoutSec(int s) override;
};
