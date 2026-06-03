// ================================================================
//  WebServer.h — ESPAsyncWebServer 라우트 + 상태 JSON + 레시피 스테이징
//  ----------------------------------------------------------------
//  웹 핸들러(Core 0)는 모터/레시피를 직접 건드리지 않는다:
//    · 모터/레시피 조작 → CommandQueue enqueue (Core 1이 처리)
//    · 비상정지        → CommandQueue::requestEstop (큐 우회)
//    · 레시피 시작     → 내부 staging 버퍼에 적재 → loop()이 consumeStaged()
//  의존성은 begin()에서 주입 (포인터). 소유하지 않음.
// ================================================================
#pragma once
#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "Types.h"
#include "MotionController.h"
#include "RecipeRunner.h"
#include "TemperatureSensor.h"
#include "CommandQueue.h"
#include "WifiManager.h"
#include "ISaver.h"

class WebServer {
public:
    struct Deps {
        MotionController*  motion;
        RecipeRunner*      recipe;
        TemperatureSensor* temp;
        CommandQueue*      cmd;
        WifiManager*       wifi;
        ISaver*            saver;
    };

    void begin(const Deps& deps);

    // loop(Core 1)에서 호출 — 스테이징된 레시피가 있으면 꺼내 true
    bool consumeStaged(String& name, std::vector<StepInfo>& steps);

private:
    void   setupRoutes();
    String buildStatus();

    AsyncWebServer    _server{80};
    Deps              _d{};

    // /api/start 청크 조립 + 스테이징 (g_stagedMux 역할)
    SemaphoreHandle_t _stagedMux = nullptr;
    String            _startBuf;

    // 화면보호기 업로드 — 본문 핸들러(set)와 완료 핸들러(read) 공유 상태
    bool              _upOk      = false;
    size_t            _upWritten = 0;
    struct {
        String                name;
        std::vector<StepInfo> steps;
        volatile bool         ready = false;
    } _staged;
};
