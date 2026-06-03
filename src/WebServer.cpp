#include "WebServer.h"
#include "Config.h"
#include "web_assets.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>

void WebServer::begin(const Deps& deps) {
    _d = deps;
    _stagedMux = xSemaphoreCreateMutex();
    _startBuf.reserve(4096);

    // 전역 CORS 헤더 — file:// 또는 외부 오리진(Recipe Editor) API 허용
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin",  "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    setupRoutes();
    _server.begin();
    Serial.println("[HTTP] 서버 시작 완료");
}

bool WebServer::consumeStaged(String& name, std::vector<StepInfo>& steps) {
    if (!_staged.ready) return false;
    bool got = false;
    if (xSemaphoreTake(_stagedMux, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (_staged.ready) {
            _staged.ready = false;
            name  = _staged.name;
            steps = std::move(_staged.steps);
            got   = true;
        }
        xSemaphoreGive(_stagedMux);
    }
    return got;
}

String WebServer::buildStatus() {
    JsonDocument doc;

    const char* dir = "IDLE";
    switch (_d.motion->state()) {
        case MotorState::RUN_FWD: case MotorState::STOP_FWD: dir = "FWD";  break;
        case MotorState::RUN_REV: case MotorState::STOP_REV: dir = "REV";  break;
        case MotorState::REST:                                dir = "REST"; break;
        case MotorState::STOP_RECIPE:                         dir = "FWD";  break;
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

    bool staOK = WifiManager::staConnected();
    doc["staConn"] = staOK;
    doc["staIP"]   = WifiManager::staIP();

    String out; out.reserve(1024);
    serializeJson(doc, out);
    return out;
}

void WebServer::setupRoutes() {
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
        req->send(200, "text/html", INDEX_HTML);
    });
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req){
        req->send(200, "application/json", buildStatus());
    });

    // ── /api/start: 청크 분할 수신 → staging (loop이 consumeStaged) ──
    _server.on("/api/start", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
            if (index == 0) { _startBuf = ""; _startBuf.reserve(total + 1); }
            _startBuf.concat(reinterpret_cast<const char*>(data), len);
            if (index + len < total) return;

            JsonDocument doc;
            if (deserializeJson(doc, _startBuf)) {
                _startBuf = "";
                req->send(400, "application/json", "{\"ok\":false}"); return;
            }
            _startBuf = "";

            if (xSemaphoreTake(_stagedMux, pdMS_TO_TICKS(100)) != pdTRUE) {
                req->send(503, "application/json", "{\"ok\":false}"); return;
            }
            _staged.name = doc["recipeName"] | String("Unknown");
            _staged.steps.clear();
            for (JsonObject s : doc["steps"].as<JsonArray>()) {
                StepInfo si;
                si.name      = s["name"]      | String("Step");
                si.speedRpm  = constrain((int)(s["speedRpm"]    | 50), 1, (int)Cfg::MAX_OUTPUT_RPM);
                si.durSec    = max((int)(s["durationSec"] | 60), 1);
                si.rotIntSec = max((int)(s["rotIntSec"]   | 30), 5);
                _staged.steps.push_back(si);
            }
            bool empty = _staged.steps.empty();
            if (!empty) _staged.ready = true;   // 마지막에 플래그 set
            xSemaphoreGive(_stagedMux);

            if (empty) { req->send(400, "application/json", "{\"ok\":false}"); return; }
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    // ── 모터/레시피 조작은 큐로만 전달 ──
    _server.on("/api/stop", HTTP_POST, [this](AsyncWebServerRequest* req){
        _d.cmd->requestEstop();   // 큐 우회 — 유실 금지
        req->send(200, "application/json", "{\"ok\":true}");
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
    _server.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest* req){
        const WifiSettings& s = _d.wifi->settings();
        JsonDocument doc;
        doc["apSSID"]      = s.apSSID;
        doc["staSSID"]     = s.staSSID;
        doc["staConn"]     = WifiManager::staConnected();
        doc["staIP"]       = WifiManager::staIP();
        doc["staStatic"]   = s.staStatic;
        doc["staStaticIP"] = s.staStaticIP;
        doc["staGW"]       = s.staGW;
        doc["staSN"]       = s.staSN;
        doc["staDNS"]      = s.staDNS;
        String out; serializeJson(doc, out); req->send(200, "application/json", out);
    });
    _server.on("/api/settings", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            JsonDocument doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"ok\":false}"); return;
            }
            WifiSettings& s = _d.wifi->settings();
            s.apSSID  = doc["apSSID"]  | s.apSSID;
            String newApPass  = doc["apPass"]  | String("");
            s.staSSID = doc["staSSID"] | String("");
            String newStaPass = doc["staPass"] | String("");
            s.staStatic   = doc["staStatic"]   | false;
            s.staStaticIP = doc["staStaticIP"] | String("192.168.1.100");
            s.staGW       = doc["staGW"]       | String("192.168.1.1");
            s.staSN       = doc["staSN"]       | String("255.255.255.0");
            s.staDNS      = doc["staDNS"]      | String("8.8.8.8");
            // 비밀번호: 8자 이상일 때만 갱신 (빈 값이면 기존 유지)
            if (newApPass.length()  >= 8) s.apPass  = newApPass;
            if (newStaPass.length() >= 8) s.staPass = newStaPass;
            _d.wifi->save();
            req->send(200, "application/json", "{\"ok\":true}");
            delay(200); ESP.restart();
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
    _server.on("/api/recipes/save", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
            File f = LittleFS.open("/recipes.tmp", index == 0 ? "w" : "a");
            if (f) { f.write(data, len); f.close(); }
            if (index + len >= total) {
                bool ok = LittleFS.rename("/recipes.tmp", "/recipes.json");
                if (!ok) {
                    LittleFS.remove("/recipes.json");
                    ok = LittleFS.rename("/recipes.tmp", "/recipes.json");
                }
                req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
            }
        }
    );

    // ── 화면보호기 설정 ──
    _server.on("/api/saver/settings", HTTP_GET, [this](AsyncWebServerRequest* req){
        bool hasJpg = LittleFS.exists("/saver.jpg");
        bool hasGif = LittleFS.exists("/saver.gif");
        size_t imgSize = 0;
        const char* imgType = "none";
        if (hasGif) {
            File f = LittleFS.open("/saver.gif", "r");
            if (f) { imgSize = f.size(); f.close(); }
            imgType = "gif";
        } else if (hasJpg) {
            File f = LittleFS.open("/saver.jpg", "r");
            if (f) { imgSize = f.size(); f.close(); }
            imgType = "jpg";
        }
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
                if (len >= 4 && data[0]==0x47 && data[1]==0x49 && data[2]==0x46 && data[3]==0x38) {
                    savePath = "/saver.gif";              // "GIF8"
                } else if (len >= 3 && data[0]==0xFF && data[1]==0xD8 && data[2]==0xFF) {
                    savePath = "/saver.jpg";              // JPEG SOI
                } else {
                    String lo = filename; lo.toLowerCase();
                    if (lo.endsWith(".gif"))                              savePath = "/saver.gif";
                    else if (lo.endsWith(".jpg") || lo.endsWith(".jpeg")) savePath = "/saver.jpg";
                }
                if (savePath.length() == 0) {
                    Serial.printf("[Saver] upload rejected (unknown type): %s\n", filename.c_str());
                    return;
                }
                if (savePath == "/saver.gif") { if (LittleFS.exists("/saver.jpg")) LittleFS.remove("/saver.jpg"); }
                else                          { if (LittleFS.exists("/saver.gif")) LittleFS.remove("/saver.gif"); }
                File f = LittleFS.open(savePath, "w");
                if (f) f.close();
                Serial.printf("[Saver] upload start: %s → %s\n", filename.c_str(), savePath.c_str());
            }
            if (savePath.length() == 0) return;
            File f = LittleFS.open(savePath, "a");
            if (f) {
                size_t w = f.write(data, len);
                f.close();
                _upWritten += w;
                if (w != len) Serial.printf("[Saver] write short: %u/%u (저장공간?)\n", (unsigned)w, (unsigned)len);
            }
            if (final) {
                _upOk = (_upWritten == index + len);
                Serial.printf("[Saver] upload done: %u bytes (ok=%d)\n", (unsigned)_upWritten, (int)_upOk);
            }
        }
    );
    _server.on("/api/saver/image", HTTP_DELETE, [](AsyncWebServerRequest* req){
        bool removed = false;
        if (LittleFS.exists("/saver.jpg")) { LittleFS.remove("/saver.jpg"); removed = true; }
        if (LittleFS.exists("/saver.gif")) { LittleFS.remove("/saver.gif"); removed = true; }
        req->send(200, "application/json", removed ? "{\"ok\":true}" : "{\"ok\":true,\"noop\":true}");
    });

    _server.onNotFound([](AsyncWebServerRequest* req){
        if (req->method() == HTTP_OPTIONS) req->send(200);
        else                              req->send(404, "text/plain", "Not Found");
    });
}
