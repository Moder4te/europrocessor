// ================================================================
//  WebServer.h — ESPAsyncWebServer 라우트 + 상태 JSON + 레시피 스테이징
//  ----------------------------------------------------------------
//  웹 핸들러(Core 0)는 모터/레시피를 직접 건드리지 않는다:
//    · 모터/레시피 조작 → CommandQueue enqueue (Core 1이 처리)
//    · 비상정지        → CommandQueue::requestEstop (큐 우회)
//    · 레시피 시작     → RecipeStage::stage() → loop()이 consume()
//  의존성은 begin()에서 주입 (포인터). 소유하지 않음.
// ================================================================
#pragma once
#include <ESPAsyncWebServer.h>
#include "Types.h"
#include "MotionController.h"
#include "RecipeRunner.h"
#include "TemperatureSensor.h"
#include "CommandQueue.h"
#include "WifiManager.h"
#include "RecipeStage.h"
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
        RecipeStage*       stage;
    };

    void begin(const Deps& deps);

private:
    void   setupRoutes();
    String buildStatus();

    AsyncWebServer    _server{80};
    Deps              _d{};

    // /api/start 청크 조립 버퍼
    String            _startBuf;

    // 화면보호기 업로드 — 본문 핸들러(set)와 완료 핸들러(read) 공유 상태
    bool              _upOk      = false;
    size_t            _upWritten = 0;
    File              _upFile;        // 업로드 중 열린 채 유지 (청크마다 open/close 회피)
};
