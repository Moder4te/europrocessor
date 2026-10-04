// ================================================================
//  main.cpp — App 코디네이터 / setup / loop
//  ----------------------------------------------------------------
//  서브시스템 객체를 소유하고 참조를 주입해 배선한다.
//  Core 1(loop): 명령 dispatch + motion/recipe update (시간 크리티컬)
//  Core 0: WiFi/Web + tempTask + displayTask (비시간 크리티컬)
//
//  manualMode / cycle 같은 시스템 상태 조정은 모두 여기(App)에서:
//    stopAll() = 비상정지(모터+레시피+모드 리셋)
//    safeStop()= 가속도 곡선 감속 후 정지
//    manualStart() = 수동 운전 시작
// ================================================================
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <esp_partition.h>
#include <esp_heap_caps.h>
#include <Preferences.h>
#include <Update.h>

#include "Config.h"
#include "Types.h"
#include "CommandQueue.h"
#include "MotionController.h"
#include "RecipeRunner.h"
#include "TemperatureSensor.h"
#include "WifiManager.h"
#include "WebServer.h"
#include "RecipeStage.h"
#include "NoiseGuard.h"

#if UI_DISPLAY_PRESENT
  #include "DisplayUI.h"
#else
  #include "ISaver.h"
#endif

// ── 서브시스템 객체 ──
static MotionController  motion;
static RecipeRunner      recipe(motion);
static TemperatureSensor temp;
static CommandQueue       cmd;
static RecipeStage       stage;     // 웹/디스플레이 → loop 레시피 전달
static WifiManager       wifi;
static WebServer         web;
static NoiseGuard        guard;

#if UI_DISPLAY_PRESENT
  static DisplayUI display;
#else
  static NullSaver nullSaver;
#endif

// ──────────────────────────────────────────────────────────────
// 코디네이터 — 시스템 상태 전이 (Core 1 전용)
// ──────────────────────────────────────────────────────────────
static void stopAll() {                 // = 기존 emergencyStop
    motion.stopImmediate();
    recipe.clear();
    motion.setManualMode(false);
    motion.setCycle(true);
}
static void safeStop() {                 // 가속도 곡선 감속 후 정지
    if (recipe.active()) recipe.clear();
    motion.setManualMode(false);
    motion.setCycle(true);
    motion.requestSafeStop();
}
static void manualStart(int rpm, bool fwd, bool cycle, int rotSec) {
    stopAll();
    motion.setManualMode(true);
    motion.setCycle(cycle);
    motion.setRotIntSec(rotSec);
    motion.beginRun(rpm, fwd);   // 가감속은 beginRun이 설정RPM 기준 S-커브로 적용
    Serial.printf("[Manual] %dRPM %s cycle=%d rot=%ds\n", rpm, fwd ? "FWD" : "REV", cycle, rotSec);
}
// 노이즈 지속 트립 — 레시피는 일시정지(재개 가능, 필름 보존), 그 외는 감속 정지
static void noiseStop() {
    if (recipe.running() && !recipe.paused() && !recipe.waitConfirm()) recipe.pauseToggle();
    else if (motion.state() != MotorState::IDLE)                        safeStop();
}
static void dispatch(const Cmd& c) {
    switch (c.type) {
        case CmdType::STOP:         stopAll();                                  break;
        case CmdType::PAUSE_TOGGLE: recipe.pauseToggle();                       break;
        case CmdType::CONFIRM:      recipe.confirm();                           break;
        case CmdType::MANUAL:       manualStart(c.rpm, c.fwd, c.cycle, c.rotSec); break;
        case CmdType::SAFE_STOP:    safeStop();                                 break;
        case CmdType::SKIP_STEP:    recipe.skipStep();                          break;
        case CmdType::GOTO_STEP:    recipe.gotoStep(c.step);                    break;
    }
}

// ──────────────────────────────────────────────────────────────
// 부팅 핀 안전 고정 (★ 물리 손상 방지 — 무조건 최우선)
// ──────────────────────────────────────────────────────────────
static void safePinInit() {
    // 레벨 먼저 → 방향 나중. pinMode(OUTPUT) 먼저 하면 출력 레지스터 기본값(LOW)이
    // 잠깐 나가 EN 활성(코일 통전) 글리치 발생. (core 2.x digitalWrite = gpio_set_level 직행)
    auto out = [](uint8_t pin, uint8_t lvl){ digitalWrite(pin, lvl); pinMode(pin, OUTPUT); };
    // TMC2209 EN active-low: HIGH = 코일 차단
    out(Pin::EN, HIGH);
    // panel 신호 핀 idle 고정 (floating noise → 컨트롤러 손상 예방)
    out(Pin::TFT_CS,   HIGH);
    out(Pin::TFT_DC,   HIGH);
    out(Pin::TFT_RST,  HIGH);
    out(Pin::TFT_BL,   LOW);
    out(Pin::TFT_SCK,  LOW);
    out(Pin::TFT_MOSI, LOW);
    // 입력 핀 즉시 풀업 (floating noise 방지)
    pinMode(Pin::ENC_A,    INPUT_PULLUP);
    pinMode(Pin::ENC_B,    INPUT_PULLUP);
    pinMode(Pin::ENC_PUSH, INPUT_PULLUP);
    pinMode(Pin::KEY_OK,   INPUT_PULLUP);
}

// ──────────────────────────────────────────────────────────────
// OTA 안전망 — OTA 직후 펌웨어가 크래시 루프(NoiseGuard 잠금)면 이전 파티션으로 자동 롤백.
//   USB 없이 웹으로만 업데이트하므로, 깨진 펌웨어에 갇히지 않게 하는 마지막 방어선.
//   pending은 OTA 성공 시 set, 롤백 또는 GUARD_STABLE_MS 정상 가동 시 clear (핑퐁 방지).
// ──────────────────────────────────────────────────────────────
static bool otaPending() {
    Preferences p; bool v = false;
    if (p.begin("ota", true)) { v = p.getBool("pending", false); p.end(); }
    return v;
}
static void otaClearPending() {
    Preferences p;
    if (p.begin("ota", false)) { p.putBool("pending", false); p.end(); }
}
static void otaRollbackIfCrashLoop() {
    if (!guard.motorLocked() || !otaPending()) return;
    otaClearPending();
    if (Update.rollBack()) {
        guard.clearResets();
        Serial.println("[OTA] ★새 펌웨어 크래시 루프 → 이전 펌웨어로 롤백, 재부팅★");
        Serial.flush();
        ESP.restart();
    }
    Serial.println("[OTA] 롤백 불가 (이전 파티션 없음) — USB로 재플래시 필요");
}

static void mountFs() {
    // 1차 실패 즉시 포맷 금지 — 부팅 전원 불안정으로 인한 일시적 읽기 오류에 레시피 전체가 지워짐. 1회 재시도.
    bool ok = LittleFS.begin(false);
    if (!ok) { delay(200); ok = LittleFS.begin(false); }
    if (!ok) {
        Serial.println("[LittleFS] 1차 마운트 실패 — partition 진단:");
        const esp_partition_t* sp = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
        if (sp) Serial.printf("[LittleFS] spiffs 파티션: label='%s' size=%u KB offset=0x%X\n",
                              sp->label, sp->size / 1024, sp->address);
        else    Serial.println("[LittleFS] spiffs subtype 없음 — partitions.csv 미적용!");
        Serial.println("[LittleFS] format() 시도...");
        Serial.flush();
        if (LittleFS.format() && LittleFS.begin(false))
            Serial.printf("[LittleFS] 포맷 후 재마운트 성공 (%u KB)\n", LittleFS.totalBytes() / 1024);
        else
            Serial.println("[LittleFS] 포맷/재마운트 실패 — 저장 기능 비활성");
    } else {
        Serial.printf("[LittleFS] 마운트 성공 (%u KB)\n", LittleFS.totalBytes() / 1024);
    }
}

static void configWdt() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_task_wdt_config_t wdtCfg = { .timeout_ms = 6000, .idle_core_mask = 0, .trigger_panic = true };
    if (esp_task_wdt_reconfigure(&wdtCfg) == ESP_ERR_INVALID_STATE) esp_task_wdt_init(&wdtCfg);
#else
    esp_task_wdt_init(6, true);
#endif
    esp_task_wdt_add(nullptr);   // setup/loop 태스크 등록
}

// ──────────────────────────────────────────────────────────────
void setup() {
    safePinInit();

    Serial.begin(115200);
    // USB-CDC(S3 네이티브 USB): 호스트가 포트를 열 때까지 최대 2초 대기 →
    // 부팅 초반 로그 유실 방지. UART 빌드에선 Serial이 곧 true라 즉시 통과.
    { uint32_t t0 = millis(); while (!Serial && millis() - t0 < 2000) delay(10); }
    delay(300);
    Serial.println("\n================================");
    Serial.printf("[BOOT] Film Processor %s 시작\n", Cfg::FW_VERSION);
    Serial.printf("[BOOT] reset reason: %d, free heap: %u\n",
                  (int)esp_reset_reason(), (unsigned)ESP.getFreeHeap());
    Serial.flush();

    guard.begin();       // 리셋 원인 집계 → 연속 비정상 리셋 시 모터 잠금
    otaRollbackIfCrashLoop();
    motion.begin();      // 스테퍼 엔진 (EN은 safePinInit에서 이미 차단)
    motion.setLocked(guard.motorLocked());
    Serial.printf("[Stepper] MAX_SPEED=%.0f steps/s (출력축 최대 %.0f RPM)\n",
                  (float)Cfg::MAX_SPEED, (float)Cfg::MAX_OUTPUT_RPM);
    recipe.begin();
    cmd.begin(8);
    stage.begin();
    temp.begin();        // MAX31865 + Core 0 tempTask

    wifi.load();
    wifi.begin();        // AP+STA + DNS + mDNS
    mountFs();

    // 웹 서버 — 의존성 주입
    WebServer::Deps wd{};
    wd.motion = &motion; wd.recipe = &recipe; wd.temp = &temp;
    wd.cmd = &cmd; wd.wifi = &wifi; wd.stage = &stage; wd.guard = &guard;
#if UI_DISPLAY_PRESENT
    wd.saver = &display;
#else
    wd.saver = &nullSaver;
#endif
    web.begin(wd);

    configWdt();

#if UI_DISPLAY_PRESENT
    DisplayUI::Deps dd{ &motion, &recipe, &temp, &cmd, &stage, &guard };
    display.begin(dd);
    display.start();
    Serial.println("[Core] DisplayTask → Core 0 시작\n");
#else
    Serial.println("[Core] DisplayTask 비활성 (UI_DISPLAY_PRESENT=0)\n");
#endif
}

void loop() {
    esp_task_wdt_reset();

    // 0) 비상정지 최우선 (큐 우회) — 정지 전에 쌓인 기동 명령/레시피가
    //    같은 사이클에서 바로 재기동시키지 않도록 큐·스테이징까지 폐기
    Cmd c;
    if (cmd.consumeEstop()) {
        stopAll();
        while (cmd.dequeue(c)) {}
        String n; std::vector<StepInfo> s; stage.consume(n, s);
    }
    guard.update();
    if (guard.takeTrip()) noiseStop();
    static bool otaConfirmed = false;   // 정상 가동 확인 → OTA 펌웨어 확정 (롤백 대상 해제)
    // NVS 쓰기라 모터 정지·레시피 비활성일 때만 (운전 중 플래시 쓰기 금지 원칙)
    if (!otaConfirmed && millis() >= Cfg::GUARD_STABLE_MS &&
        motion.state() == MotorState::IDLE && !recipe.active()) {
        otaConfirmed = true;
        if (otaPending()) { otaClearPending(); Serial.println("[OTA] 새 펌웨어 정상 가동 확인 — 확정"); }
    }

    // 1) 명령 큐 dispatch (모든 모터/레시피 변경은 Core 1에서만)
    while (cmd.dequeue(c)) dispatch(c);

    // 2) 스테이징 레시피 적용 (웹/디스플레이 공용 진입점)
    {
        String                name;
        std::vector<StepInfo> steps;
        if (stage.consume(name, steps)) {
            stopAll();                 // 현재 레시피/모터 정지 후
            if (guard.motorLocked()) Serial.println("[Guard] 모터 잠금 — 레시피 시작 거부");
            else                     recipe.load(name, steps);  // 0단계부터 실행
        }
    }

    // 3) 비차단 업데이트
    wifi.update();

    // 장시간 운전 메모리 추적 — 내부 RAM(lwIP/AsyncTCP 사용 영역). free·largest가 계속 줄면 누수/단편화.
    static uint32_t lastHeapMs = 0;
    if (millis() - lastHeapMs >= 10000) {
        lastHeapMs = millis();
        Serial.printf("[Heap] up %lus  free %u  min %u  largest %u  (STA %s, AP 클라 %u)\n",
                      (unsigned long)(lastHeapMs / 1000),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                      WifiManager::staConnected() ? "on" : "off",
                      (unsigned)WiFi.softAPgetStationNum());
    }
    motion.update();
    recipe.update();
}
