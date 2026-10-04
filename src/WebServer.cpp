#include "WebServer.h"
#include "Config.h"
#include "web_assets.h"
#include "web_multi.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_netif_sta_list.h>

// send()는 응답을 저장만 하고 전송은 핸들러 반환 후 — 핸들러 안에서 restart 하면 응답 유실.
// 1초 뒤 별도 태스크로 재시작.
static uint8_t flashSizeCode(uint32_t sz) {
    return sz >= (16u << 20) ? 4 : sz >= (8u << 20) ? 3 : sz >= (4u << 20) ? 2 : sz >= (2u << 20) ? 1 : 0;
}

static void scheduleReboot() {
    xTaskCreate([](void*){ vTaskDelay(pdMS_TO_TICKS(1000)); ESP.restart(); },
                "reboot", 2048, nullptr, 1, nullptr);
}

void WebServer::begin(const Deps& deps) {
    _d = deps;

    // 전역 CORS 헤더 — file:// 또는 외부 오리진(Recipe Editor) API 허용
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin",  "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    setupRoutes();
    _server.begin();
    Serial.println("[HTTP] 서버 시작 완료");
}

String WebServer::buildStatus() {
    JsonDocument doc;

    const char* dir = "IDLE";
    switch (_d.motion->state()) {
        case MotorState::RUN_FWD: case MotorState::STOP_FWD: dir = "FWD";  break;
        case MotorState::RUN_REV: case MotorState::STOP_REV: dir = "REV";  break;
        case MotorState::REST:                                dir = "REST"; break;
        case MotorState::STOP_RECIPE:  // 감속 중 — 실제 회전 방향 표시
            dir = _d.motion->isFwd() ? "FWD" : "REV";                       break;
        case MotorState::STOP_SAFE:                           dir = "STOP"; break;
        default: break;
    }
    doc["motorRpm"]    = (int)round(_d.motion->curRpm());
    doc["motorDir"]    = dir;
    doc["manualMode"]  = _d.motion->manualMode();

    RecipeStatus rs = _d.recipe->snapshot();
    doc["recipeRun"]   = rs.running;
    doc["recipePause"] = rs.paused;
    doc["waitConfirm"] = rs.waitConfirm;
    doc["recipeName"]  = rs.name;
    doc["stepIdx"]     = rs.stepIdx;
    doc["totalSteps"]  = rs.stepTotal;
    doc["stepName"]    = rs.curName;
    doc["stepNext"]    = rs.nextName;
    doc["stepDurSec"]  = rs.stepDurSec;
    doc["stepRemSec"]  = (int)rs.stepRemSec;

    float   t = _d.temp->temperature();
    uint8_t f = _d.temp->fault();
    doc["temperature"] = (f == 0) ? t : Cfg::TEMP_UNREAD;
    doc["tempFault"]   = (f != 0);
    doc["tempFaultCode"] = f;   // MAX31865 fault 비트 (0x80 상한,0x40 하한,0x20 REFIN-과전압,0x10 REFIN-개방,0x08 RTDIN-개방,0x04 과·저전압,0x02 범위밖,0x01 SPI미통신)
    doc["rtdWires"]    = boardRtdWires();
    doc["rtdOhm"]      = _d.temp->rtdOhm();      // 보정 전 측정 저항 — 웹 보정 계산용
    doc["calGain"]     = _d.temp->calGain();
    doc["calOffset"]   = _d.temp->calOffset();

    bool staOK = WifiManager::staConnected();
    doc["staConn"] = staOK;
    doc["staIP"]   = WifiManager::staIP();

    doc["guard"]       = _d.guard->status();      // ok / noise / locked
    doc["hwFault"]     = HwSafety::text(_d.hw->fault());   // "" / driver / vm_low / vm_high
    if (_d.hw->hasVm()) doc["vm"] = _d.hw->vm();
    {   // 정전·리셋 복구 대기
        RecoveryInfo r = _d.recipe->recovery();
        if (r.valid) {
            JsonObject o = doc["recovery"].to<JsonObject>();
            o["name"] = r.name; o["step"] = r.stepIdx; o["total"] = r.total; o["stepName"] = r.stepName;
            o["stepDurSec"] = r.stepDurSec; o["elapsedSec"] = r.elapsedMs / 1000; o["state"] = r.state;
        }
    }
    doc["resetReason"] = _d.guard->resetReason();

    // 장시간 운전 진단 — 내부 RAM(lwIP/AsyncTCP 영역). free·largest가 계속 줄면 누수/단편화
    doc["fw"]       = Cfg::FW_VERSION;
    doc["name"]     = _d.wifi->hostName();
    doc["boardId"]  = _d.wifi->settings().boardId;
    doc["mac"]      = boardMac();
    doc["pinProfile"] = pinProfileLabel();
    doc["variant"]  = FW_VARIANT;
    doc["rssi"]     = staOK ? WiFi.RSSI() : 0;          // 집 WiFi 수신 세기 (dBm) — 링크 품질 진단
    doc["apClients"] = WiFi.softAPgetStationNum();
    doc["upSec"]    = (uint32_t)(millis() / 1000);
    doc["heapFree"] = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    doc["heapMin"]  = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    doc["heapMax"]  = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    String out; out.reserve(1024);
    serializeJson(doc, out);
    return out;
}

void WebServer::setupRoutes() {
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
        // 길이 지정 오버로드 → 플래시에서 직접 전송 (String 복사 ~40KB 힙 스파이크 회피)
        req->send(200, "text/html", (const uint8_t*)INDEX_HTML, sizeof(INDEX_HTML) - 1);
    });
    _server.on("/multi", HTTP_GET, [](AsyncWebServerRequest* req){
        req->send(200, "text/html", (const uint8_t*)MULTI_HTML, sizeof(MULTI_HTML) - 1);
    });
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req){
        req->send(200, "application/json", buildStatus());
    });

    // ── /api/start: 청크 분할 수신 → RecipeStage::stage (loop이 consume) ──
    _server.on("/api/start", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
            if (total > Cfg::MAX_JSON_BODY) {   // 비정상 Content-Length → reserve 힙 고갈 방지
                if (index + len >= total) req->send(413, "application/json", "{\"ok\":false}");
                return;
            }
            // 요청별 버퍼(_tempObject) — 여러 폰/보드가 동시에 시작해도 본문이 섞이지 않음.
            //   요청 소멸 시 라이브러리가 free() 해줌.
            if (index == 0) req->_tempObject = malloc(total + 1);
            char* buf = static_cast<char*>(req->_tempObject);
            if (!buf) {
                if (index + len >= total) req->send(500, "application/json", "{\"ok\":false}");
                return;
            }
            memcpy(buf + index, data, len);
            if (index + len < total) return;
            buf[total] = 0;

            JsonDocument doc;
            if (deserializeJson(doc, buf, total)) {
                req->send(400, "application/json", "{\"ok\":false}"); return;
            }

            String rname = doc["recipeName"] | String("Unknown");
            std::vector<StepInfo> steps;
            for (JsonObject s : doc["steps"].as<JsonArray>()) {
                StepInfo si;
                si.name      = s["name"]      | String("Step");
                si.speedRpm  = constrain((int)(s["speedRpm"]    | 50), 1, (int)Cfg::MAX_OUTPUT_RPM);
                si.durSec    = max((int)(s["durationSec"] | 60), 1);
                si.rotIntSec = max((int)(s["rotIntSec"]   | 30), 5);
                steps.push_back(si);
            }
            bool ok = _d.stage->stage(rname, std::move(steps));
            req->send(ok ? 200 : 400, "application/json",
                      ok ? "{\"ok\":true}" : "{\"ok\":false}");
        }
    );

    // ── 모터/레시피 조작은 큐로만 전달 ──
    _server.on("/api/stop", HTTP_POST, [this](AsyncWebServerRequest* req){
        _d.cmd->requestEstop();   // 큐 우회 — 유실 금지
        req->send(200, "application/json", "{\"ok\":true}");
    });
    // ── 단계 이동: {"step": N(0부터)} ──
    _server.on("/api/goto", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            JsonDocument doc;
            if (deserializeJson(doc, data, len) || !doc["step"].is<int>()) {
                req->send(400, "application/json", "{\"ok\":false}"); return;
            }
            if (!_d.recipe->active()) {
                req->send(409, "application/json", "{\"ok\":false,\"error\":\"no recipe\"}"); return;
            }
            Cmd c{}; c.type = CmdType::GOTO_STEP; c.step = doc["step"].as<int>();
            bool ok = _d.cmd->enqueue(c);
            req->send(ok ? 200 : 503, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
        }
    );
    // ── 현재 실행 중 레시피 단계 목록 (모든 클라이언트가 같은 진행도 바를 그리도록) ──
    _server.on("/api/recipe", HTTP_GET, [this](AsyncWebServerRequest* req){
        String name; std::vector<StepInfo> steps;
        _d.recipe->copySteps(name, steps);
        JsonDocument doc;
        doc["name"] = name;
        JsonArray arr = doc["steps"].to<JsonArray>();
        for (const StepInfo& s : steps) {
            JsonObject o = arr.add<JsonObject>();
            o["name"] = s.name; o["durSec"] = s.durSec; o["speedRpm"] = s.speedRpm;
        }
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });
    _server.on("/api/pause", HTTP_POST, [this](AsyncWebServerRequest* req){
        Cmd c{}; c.type = CmdType::PAUSE_TOGGLE;
        _d.cmd->enqueue(c);
        req->send(200, "application/json", "{\"ok\":true}");
    });
    _server.on("/api/confirm", HTTP_POST, [this](AsyncWebServerRequest* req){
        Cmd c{}; c.type = CmdType::CONFIRM;
        _d.cmd->enqueue(c);
        req->send(200, "application/json", "{\"ok\":true}");
    });
    _server.on("/api/skip", HTTP_POST, [this](AsyncWebServerRequest* req){
        Cmd c{}; c.type = CmdType::SKIP_STEP;   // 레시피 현재 단계 건너뛰기
        _d.cmd->enqueue(c);
        req->send(200, "application/json", "{\"ok\":true}");
    });
    _server.on("/api/safestop", HTTP_POST, [this](AsyncWebServerRequest* req){
        Cmd c{}; c.type = CmdType::SAFE_STOP;   // 수동 운전 안전 정지 (감속)
        // 큐 full이면 503 → 클라이언트가 재시도 (기존: 실패해도 200이라 정지 유실이 안 보임)
        bool ok = _d.cmd->enqueue(c);
        req->send(ok ? 200 : 503, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });
    _server.on("/api/manual", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"ok\":false}"); return;
            }
            Cmd c{};
            c.type   = CmdType::MANUAL;
            c.rpm    = constrain((int)(doc["speedRpm"] | 50), 1, (int)Cfg::MAX_OUTPUT_RPM);
            c.fwd    = doc["fwd"]   | true;
            c.cycle  = doc["cycle"] | true;
            c.rotSec = max((int)(doc["rotIntSec"] | 30), 5);
            _d.cmd->enqueue(c);
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    // ── WiFi 설정 ──
    // ── 동시 제어용 피어 탐색: 이 보드 AP에 붙은 기기들의 IP ──
    //   폰/PC도 섞여 있음 → 브라우저가 각 IP의 /api/status로 프로세서인지 확인.
    //   슬레이브는 DHCP로 붙어도 됨 (고정 IP 불필요).
    _server.on("/api/peers", HTTP_GET, [](AsyncWebServerRequest* req){
        JsonDocument doc;
        JsonArray arr = doc["ap"].to<JsonArray>();
        wifi_sta_list_t wl{};
        esp_netif_sta_list_t nl{};
        int noIp = 0;   // IP 모르는 기기 — DHCP 전이거나 고정 IP(마스터가 IP를 할당하지 않음)
        if (esp_wifi_ap_get_sta_list(&wl) == ESP_OK && esp_netif_get_sta_list(&wl, &nl) == ESP_OK) {
            for (int i = 0; i < nl.num; ++i) {
                if (nl.sta[i].ip.addr == 0) { noIp++; continue; }
                arr.add(IPAddress(nl.sta[i].ip.addr).toString());
            }
        }
        doc["noIp"] = noIp;
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // ── 정전 복구 선택: {"action":"resume"|"restart"|"next"|"discard"} ──
    _server.on("/api/recovery", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) { req->send(400, "application/json", "{\"ok\":false}"); return; }
            const String a = doc["action"] | String("");
            RecoverAction act;
            if      (a == "resume")  act = RecoverAction::RESUME;
            else if (a == "restart") act = RecoverAction::RESTART_STEP;
            else if (a == "next")    act = RecoverAction::NEXT_STEP;
            else if (a == "discard") act = RecoverAction::DISCARD;
            else { req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad action\"}"); return; }
            if (!_d.recipe->recoveryPending()) { req->send(409, "application/json", "{\"ok\":false,\"error\":\"no recovery\"}"); return; }
            if (act != RecoverAction::DISCARD && motorBusy()) { req->send(409, "application/json", "{\"ok\":false,\"error\":\"motor running\"}"); return; }
            if (act != RecoverAction::DISCARD && (_d.hw->fault() || _d.guard->motorLocked())) {
                req->send(409, "application/json", "{\"ok\":false,\"error\":\"motor locked\"}"); return;
            }
            Cmd c{}; c.type = CmdType::RECOVER; c.step = (int)act;
            const bool ok = _d.cmd->enqueue(c);
            req->send(ok ? 200 : 503, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
        }
    );
    // ── 하드웨어 고장 해제 (원인이 사라졌을 때만 실제 해제 — Core 1이 재확인) ──
    _server.on("/api/hwfault/clear", HTTP_POST, [this](AsyncWebServerRequest* req){
        Cmd c{}; c.type = CmdType::HW_CLEAR;
        const bool ok = _d.cmd->enqueue(c);
        req->send(ok ? 200 : 503, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    // ── 온도 보정 저장: {"gain":g,"offset":Ω} 또는 {"reset":true} ──
    //   계산은 웹이 함(기준온도 → PT100 저항, 측정저항 rtdOhm과 선형 맞춤). NVS 쓰기라 운전 중 거부.
    _server.on("/api/temp/cal", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            if (motorBusy()) { req->send(409, "application/json", "{\"ok\":false,\"error\":\"motor running\"}"); return; }
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) { req->send(400, "application/json", "{\"ok\":false}"); return; }
            const bool reset = doc["reset"] | false;
            const float g = reset ? 1.0f : (doc["gain"]   | 1.0f);
            const float o = reset ? 0.0f : (doc["offset"] | 0.0f);
            if (!_d.temp->setCalibration(g, o)) {
                req->send(400, "application/json", "{\"ok\":false,\"error\":\"out of range\"}"); return;
            }
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    // ── 주변 WiFi 검색 (비동기) ──
    //   GET → 결과 있으면 {"scanning":false,"nets":[…]}, 진행 중이면 {"scanning":true} (클라가 1초마다 재요청)
    //   15초 내 결과는 캐시 재사용(?refresh=1로 강제). 검색 중 2~4초 AP 클라 통신 지연 가능(채널 순회).
    //   AP 단독 모드면 scanNetworks가 STA를 자동으로 켬 — STA SSID 없으면 재연결 시도 안 하므로 무해.
    _server.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* req){
        int16_t n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) { req->send(200, "application/json", "{\"scanning\":true}"); return; }
        // 내가 시작한 검색 결과만 사용 — STA 접속 시 내부 검색도 완료 비트를 세워 n=0을 돌려줌(빈 목록 오인)
        if (n >= 0 && _scanPending) {   // 완료 → 정리해서 캐시
            _scanPending = false;
            JsonDocument doc;
            JsonArray arr = doc["nets"].to<JsonArray>();
            for (int i = 0; i < n; ++i) {
                String ssid = WiFi.SSID(i);
                if (!ssid.length()) continue;                       // 숨김 SSID 제외
                bool dup = false;                                   // 같은 SSID(메시/다중 AP)는 가장 센 것만
                for (JsonObject o : arr) if (o["ssid"] == ssid) {
                    if (WiFi.RSSI(i) > (int)o["rssi"]) { o["rssi"] = WiFi.RSSI(i); o["ch"] = WiFi.channel(i); }
                    dup = true; break;
                }
                if (dup) continue;
                JsonObject o = arr.add<JsonObject>();
                o["ssid"] = ssid; o["rssi"] = WiFi.RSSI(i); o["ch"] = WiFi.channel(i);
                o["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
            }
            WiFi.scanDelete();
            _scanJson = ""; serializeJson(doc, _scanJson);
            _scanMs = millis();
        }
        const bool fresh = _scanJson.length() && millis() - _scanMs < 15000 && !req->hasParam("refresh");
        if (fresh) { req->send(200, "application/json", String("{\"scanning\":false,") + _scanJson.substring(1)); return; }
        WiFi.scanDelete();         // 이전(내부) 결과 정리
        _scanPending = WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING;   // 비동기 시작
        req->send(200, "application/json", "{\"scanning\":true}");
    });

    _server.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest* req){
        const WifiSettings& s = _d.wifi->settings();
        JsonDocument doc;
        doc["apSSID"]      = s.apSSID;
        doc["staSSID"]     = s.staSSID;
        doc["staConn"]     = WifiManager::staConnected();
        doc["staIP"]       = WifiManager::staIP();
        doc["boardId"]     = s.boardId;
        doc["apNameValid"] = WifiManager::isValidApName(s.apSSID);   // false면 UI가 SSID 변경 권장
        doc["apIP"]        = _d.wifi->apIP().toString();
        doc["hostName"]    = _d.wifi->hostName();
        String out; serializeJson(doc, out); req->send(200, "application/json", out);
    });
    _server.on("/api/settings", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            // 저장 = NVS 쓰기 + 재부팅. 운전 중이면 레시피 소실·모터 급정지(리셋 중 EN 플로팅) → 거부
            if (motorBusy()) {
                req->send(409, "application/json", "{\"ok\":false,\"error\":\"motor running\"}"); return;
            }
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"ok\":false}"); return;
            }
            WifiSettings& s = _d.wifi->settings();
            // AP SSID = 접속 이름(이름.local) → mDNS 호스트 규칙 강제. 기존 값 그대로 보내면(규칙 밖이어도) 허용 —
            // 이전 SSID를 쓰는 슬레이브·폰 연결을 끊지 않게. 바꿀 때만 규칙 검사.
            String newApSSID = doc["apSSID"] | s.apSSID;
            if (newApSSID != s.apSSID && !WifiManager::isValidApName(newApSSID)) {
                req->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid ssid\"}"); return;
            }
            s.apSSID = newApSSID;
            String newApPass  = doc["apPass"]  | String("");
            s.staSSID = doc["staSSID"] | String("");
            String newStaPass = doc["staPass"] | String("");
            if (doc["boardId"].is<int>()) s.boardId = constrain(doc["boardId"].as<int>(), 1, 9);
            // 비밀번호: 8자 이상일 때만 갱신 (빈 값이면 기존 유지)
            if (newApPass.length()  >= 8 && newApPass.length()  <= 63) s.apPass  = newApPass;
            if (newStaPass.length() >= 8 && newStaPass.length() <= 63) s.staPass = newStaPass;
            if (s.staSSID.length() == 0) s.staPass = "";   // 홈 WiFi 삭제 시 비번도 삭제 → 재부팅 후 AP 단독 모드
            _d.wifi->save();
            req->send(200, "application/json", "{\"ok\":true}");
            scheduleReboot();
        }
    );

    // ── 레시피 파일 (LittleFS) ──
    _server.on("/api/recipes/load", HTTP_GET, [](AsyncWebServerRequest* req){
        if (!LittleFS.exists("/recipes.json")) {
            req->send(200, "application/json",
                      "{\"B&W\":[],\"C-41\":[],\"ECN-2\":[],\"E-6\":[]}");
            return;
        }
        req->send(LittleFS, "/recipes.json", "application/json");
    });
    // ── 레시피 저장 — 방어:
    //   · 모터/레시피 동작 중 거부 (플래시 소거 수십 ms 동안 모터 펄스 큐 보충이 멈춰 덜컹일 수 있음)
    //   · 동시 저장 거부 (두 기기가 같은 임시파일에 이어쓰면 파일 손상)
    //   · 크기 상한 / 쓰기 실패 감지 / 커밋 전 JSON 검증 (깨진 파일이 기존 레시피를 덮어쓰지 않게)
    //   요청별 상태는 _tempObject(1바이트 코드)에 — 거부된 요청끼리도 각자 올바른 응답.
    _server.on("/api/recipes/save", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
            enum : uint8_t { REC_OK, REC_TOO_LARGE, REC_MOTOR, REC_BUSY, REC_IO, REC_INVALID };
            if (index == 0) {
                uint8_t* st = static_cast<uint8_t*>(malloc(1));
                req->_tempObject = st;
                if (!st) return;
                *st = REC_OK;
                if (total > Cfg::MAX_RECIPES_BODY)                            *st = REC_TOO_LARGE;
                else if (motorBusy())                                         *st = REC_MOTOR;
                else if (_recReq && millis() - _recLastMs < 5000)             *st = REC_BUSY;
                else {
                    if (_recFile) _recFile.close();                           // 끊긴 이전 업로드 정리
                    _recFile = LittleFS.open("/recipes.tmp", "w");
                    if (!_recFile) *st = REC_IO;
                    else { _recReq = req; _recLastMs = millis(); }
                }
            }
            uint8_t* st = static_cast<uint8_t*>(req->_tempObject);
            if (!st) { if (index + len >= total) req->send(500, "application/json", "{\"ok\":false,\"error\":\"oom\"}"); return; }

            if (*st == REC_OK && req == _recReq) {
                _recLastMs = millis();
                if (motorBusy())                              *st = REC_MOTOR;   // 업로드 중 모터 기동
                else if (_recFile.write(data, len) != len)    *st = REC_IO;      // 저장공간 부족 등
                if (*st != REC_OK) { _recFile.close(); LittleFS.remove("/recipes.tmp"); _recReq = nullptr; }
            }
            if (index + len < total) return;

            if (*st == REC_OK && req == _recReq) {
                _recFile.close();
                _recReq = nullptr;
                File f = LittleFS.open("/recipes.tmp", "r");
                JsonDocument doc;
                bool valid = f && !deserializeJson(doc, f) && doc.is<JsonObject>();
                if (f) f.close();
                if (!valid) {
                    *st = REC_INVALID;
                    LittleFS.remove("/recipes.tmp");
                } else {
                    bool ok = LittleFS.rename("/recipes.tmp", "/recipes.json");
                    if (!ok) { LittleFS.remove("/recipes.json"); ok = LittleFS.rename("/recipes.tmp", "/recipes.json"); }
                    if (!ok) *st = REC_IO;
                }
            }
            static const char* const ERR[] = { "", "too large", "motor running", "busy", "write failed", "invalid json" };
            static const int CODE[] = { 200, 413, 409, 409, 507, 400 };
            if (*st == REC_OK) req->send(200, "application/json", "{\"ok\":true}");
            else req->send(CODE[*st], "application/json",
                           String("{\"ok\":false,\"error\":\"") + ERR[*st] + "\"}");
        }
    );

    // ── 화면보호기 설정 ──
    _server.on("/api/saver/settings", HTTP_GET, [this](AsyncWebServerRequest* req){
        size_t imgSize = 0;
        const char* imgType = "none";
        const char* path = nullptr;
        if      (LittleFS.exists("/saver.anim")) { imgType = "anim"; path = "/saver.anim"; }
        else if (LittleFS.exists("/saver.gif"))  { imgType = "gif";  path = "/saver.gif";  }
        else if (LittleFS.exists("/saver.jpg"))  { imgType = "jpg";  path = "/saver.jpg";  }
        if (path) { File f = LittleFS.open(path, "r"); if (f) { imgSize = f.size(); f.close(); } }
        JsonDocument doc;
        doc["enabled"]    = _d.saver->saverEnabled();
        doc["timeoutSec"] = _d.saver->saverTimeoutSec();
        doc["imageType"]  = imgType;
        doc["imageSize"]  = (uint32_t)imgSize;
        String out; serializeJson(doc, out);
        req->send(200, "application/json", out);
    });
    _server.on("/api/saver/settings", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
            static String buf;
            if (index == 0) buf = "";
            buf.concat((const char*)data, len);
            if (index + len < total) return;
            JsonDocument doc;
            if (deserializeJson(doc, buf) != DeserializationError::Ok) {
                req->send(400, "application/json", "{\"ok\":false}"); return;
            }
            if (doc["enabled"].is<bool>())   _d.saver->setSaverEnabled(doc["enabled"].as<bool>());
            if (doc["timeoutSec"].is<int>()) _d.saver->setSaverTimeoutSec(doc["timeoutSec"].as<int>());
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    // ── 화면보호기 이미지 업로드 (매직바이트 타입 판정) ──
    //   본문 핸들러가 _upOk/_upWritten 설정 → 완료 핸들러가 실제 저장결과로 응답.
    _server.on("/api/saver/image", HTTP_POST,
        [this](AsyncWebServerRequest* req){
            if (_upOk && _upWritten > 0) {
                req->send(200, "application/json", "{\"ok\":true}");
            } else if (motorBusy()) {   // 운전 중 거부/중단 — 원인을 정확히 알림
                req->send(409, "application/json", "{\"ok\":false,\"error\":\"motor running\"}");
            } else {
                req->send(415, "application/json",
                          "{\"ok\":false,\"error\":\"unsupported or empty image\"}");
            }
            _upOk = false; _upWritten = 0;   // 다음 요청 대비 초기화
        },
        [this](AsyncWebServerRequest* req, String filename, size_t index,
               uint8_t* data, size_t len, bool final){
            static String savePath;
            if (index == 0) {
                _upOk = false; _upWritten = 0; savePath = "";
                if (_upFile) _upFile.close();   // 이전 업로드 잔여 핸들 정리
                // 모터 운전 중 플래시 쓰기 금지 — 펄스 큐 보충 정지(덜컹) 방지
                if (motorBusy()) {
                    Serial.println("[Saver] upload 거부: 모터 운전 중 (정지 후 재시도)");
                    return;   // savePath 빈 채 → 파일 안 열고 write 스킵
                }
                if (len >= 4 && data[0]=='A' && data[1]=='N' && data[2]=='M' && data[3]=='1') {
                    savePath = "/saver.anim";             // RGB565 프레임 (브라우저 인코딩)
                } else if (len >= 4 && data[0]==0x47 && data[1]==0x49 && data[2]==0x46 && data[3]==0x38) {
                    savePath = "/saver.gif";              // "GIF8"
                } else if (len >= 3 && data[0]==0xFF && data[1]==0xD8 && data[2]==0xFF) {
                    savePath = "/saver.jpg";              // JPEG SOI
                } else {
                    String lo = filename; lo.toLowerCase();
                    if (lo.endsWith(".anim"))                             savePath = "/saver.anim";
                    else if (lo.endsWith(".gif"))                         savePath = "/saver.gif";
                    else if (lo.endsWith(".jpg") || lo.endsWith(".jpeg")) savePath = "/saver.jpg";
                }
                if (savePath.length() == 0) {
                    Serial.printf("[Saver] upload rejected (unknown type): %s\n", filename.c_str());
                    return;
                }
                // 한 번에 하나만 — 다른 종류 화면보호기 파일 제거
                const char* others[] = { "/saver.anim", "/saver.gif", "/saver.jpg" };
                for (const char* o : others) if (savePath != o && LittleFS.exists(o)) LittleFS.remove(o);
                _upFile = LittleFS.open(savePath, "w");   // 한 번 열고 업로드 내내 유지
                Serial.printf("[Saver] upload start: %s → %s\n", filename.c_str(), savePath.c_str());
            }
            // 업로드 도중 모터가 기동하면 즉시 중단 — 플래시 write ↔ 인코더 인터럽트 캐시 크래시 회피
            if (_upFile && motorBusy()) {
                _upFile.close();
                if (savePath.length()) LittleFS.remove(savePath);
                savePath = ""; _upOk = false; _upWritten = 0;
                Serial.println("[Saver] upload 중단: 업로드 중 모터 기동 (부분파일 삭제)");
            }
            if (_upFile) {                                // 청크마다 write만 (open/close 없음)
                size_t w = _upFile.write(data, len);
                _upWritten += w;
                if (w != len) Serial.printf("[Saver] write short: %u/%u (저장공간?)\n", (unsigned)w, (unsigned)len);
            }
            if (final) {
                if (_upFile) _upFile.close();
                _upOk = (_upWritten == index + len);
                Serial.printf("[Saver] upload done: %u bytes (ok=%d)\n", (unsigned)_upWritten, (int)_upOk);
            }
        }
    );
    _server.on("/api/saver/image", HTTP_DELETE, [this](AsyncWebServerRequest* req){
        if (motorBusy()) {   // 모터 운전 중 플래시 삭제 금지
            req->send(409, "application/json", "{\"ok\":false,\"error\":\"motor running\"}");
            return;
        }
        bool removed = false;
        const char* files[] = { "/saver.jpg", "/saver.gif", "/saver.anim" };
        for (const char* fp : files) if (LittleFS.exists(fp)) { LittleFS.remove(fp); removed = true; }
        req->send(200, "application/json", removed ? "{\"ok\":true}" : "{\"ok\":true,\"noop\":true}");
    });

    // ── OTA 펌웨어 업데이트 (웹에서 firmware.bin 업로드 → 비활성 app 파티션에 기록 → 재부팅) ──
    //   · X-OTA 헤더 필수: 커스텀 헤더는 CORS 프리플라이트 대상이고 Allow-Headers엔 Content-Type만
    //     있으므로, 외부 웹페이지가 브라우저를 통해 몰래 펌웨어를 올리는 것(CSRF)을 차단.
    //   · 모터/레시피 동작 중 거부 (플래시 쓰기 ↔ 모터 타이밍 간섭 회피).
    //   · 성공 시 NVS "ota/pending" 표시 → 새 펌웨어가 크래시 루프면 main이 이전 파티션으로 자동 롤백.
    _server.on("/api/ota", HTTP_POST,
        [this](AsyncWebServerRequest* req){
            if (req != _otaReq) {   // 다른 기기가 업로드 중이라 거부된 요청
                req->send(409, "application/json", "{\"ok\":false,\"error\":\"busy\"}");
                return;
            }
            _otaReq = nullptr;
            if (_otaOk) {
                req->send(200, "application/json", "{\"ok\":true}");
                Serial.println("[OTA] 완료 — 1초 후 재부팅");
                scheduleReboot();
            } else {
                req->send(500, "application/json",
                          String("{\"ok\":false,\"error\":\"") + (_otaErr.length() ? _otaErr : String("empty")) + "\"}");
            }
            _otaOk = false;
        },
        [this](AsyncWebServerRequest* req, String filename, size_t index,
               uint8_t* data, size_t len, bool final){
            if (index == 0) {
                // 다른 기기가 업로드 중이면 거부. 단 10초간 청크가 없으면 끊긴 업로드로 보고 정리.
                if (Update.isRunning()) {
                    if (millis() - _otaLastMs < 10000) return;   // 완료 핸들러가 409 busy 응답
                    Update.abort();
                }
                _otaReq = req; _otaLastMs = millis();
                _otaOk = false; _otaErr = "";
                if (!req->hasHeader("X-OTA"))             { _otaErr = "forbidden";     return; }
                if (motorBusy())
                                                          { _otaErr = "motor running"; return; }
                // 칩 사양 불일치 이미지 거부 (N16R8용 .bin을 N8R2 바디에 올리는 사고 등)
                //   이미지 헤더 byte3 상위 4비트 = 플래시 용량 코드 (2=4MB, 3=8MB, 4=16MB)
                if (len < 4 || data[0] != 0xE9 || (data[3] >> 4) != flashSizeCode(ESP.getFlashChipSize()))
                                                          { _otaErr = "wrong board variant"; return; }
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) { _otaErr = Update.errorString(); return; }
                Serial.printf("[OTA] 시작: %s\n", filename.c_str());
            }
            if (req != _otaReq || !Update.isRunning()) return;   // 거부/실패/남의 업로드 청크 무시
            _otaLastMs = millis();
            if (motorBusy()) {        // 업로드 도중 모터 기동 → 중단
                Update.abort(); _otaErr = "motor started"; return;
            }
            if (Update.write(data, len) != len) {                // 첫 바이트 0xE9 아니면 여기서 거부됨
                _otaErr = Update.errorString(); Update.abort(); return;
            }
            if (final) {
                if (Update.end(true)) {                          // 이미지 검증 + 부트 파티션 전환
                    Preferences p;
                    if (p.begin("ota", false)) { p.putBool("pending", true); p.end(); }
                    _otaOk = true;
                    Serial.printf("[OTA] 기록 완료: %u bytes\n", (unsigned)(index + len));
                } else {
                    _otaErr = Update.errorString();
                }
            }
        }
    );

    _server.onNotFound([](AsyncWebServerRequest* req){
        if (req->method() == HTTP_OPTIONS) req->send(200);
        else                              req->send(404, "text/plain", "Not Found");
    });
}
