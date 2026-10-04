#include "WifiManager.h"
#include <ESPmDNS.h>

static const IPAddress AP_SUB(255, 255, 255, 0);

void WifiManager::load() {
    _prefs.begin("wifi", true);
    _s.apSSID      = _prefs.getString("ap_ssid",   "FilmProcessor");
    _s.apPass      = _prefs.getString("ap_pass",   "12345678");
    _s.staSSID     = _prefs.getString("sta_ssid",  "");
    _s.staPass     = _prefs.getString("sta_pass",  "");
    _s.staStatic   = _prefs.getBool  ("sta_static", false);
    _s.staStaticIP = _prefs.getString("sta_ip",    "192.168.1.100");
    _s.staGW       = _prefs.getString("sta_gw",    "192.168.1.1");
    _s.staSN       = _prefs.getString("sta_sn",    "255.255.255.0");
    _s.staDNS      = _prefs.getString("sta_dns",   "8.8.8.8");
    _s.boardId     = constrain(_prefs.getInt("board_id", 1), 1, 9);
    _prefs.end();
}

void WifiManager::save() {
    _prefs.begin("wifi", false);
    _prefs.putString("ap_ssid",   _s.apSSID);
    _prefs.putString("ap_pass",   _s.apPass);
    _prefs.putString("sta_ssid",  _s.staSSID);
    _prefs.putString("sta_pass",  _s.staPass);
    _prefs.putBool  ("sta_static", _s.staStatic);
    _prefs.putString("sta_ip",    _s.staStaticIP);
    _prefs.putString("sta_gw",    _s.staGW);
    _prefs.putString("sta_sn",    _s.staSN);
    _prefs.putString("sta_dns",   _s.staDNS);
    _prefs.putInt   ("board_id",  _s.boardId);
    _prefs.end();
}

void WifiManager::onEvent(arduino_event_id_t event, arduino_event_info_t) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            // 여기서 즉시 reconnect 금지 — 홈 AP 부재 시 재시도마다 채널 스캔 →
            // AP 클라이언트 통신 정지가 반복됨. 재시도는 update()가 백오프로 담당.
            Serial.println("[STA] 연결 끊김");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("[STA] 연결 복구 IP: %s\n", WiFi.localIP().toString().c_str());
            break;
        default: break;
    }
}

void WifiManager::begin() {
    // 홈 WiFi 미설정 → AP 단독 (STA 인터페이스 자체를 안 띄움)
    const bool useSta = _s.staSSID.length() > 0;
    const String host = hostName();
    WiFi.setHostname(host.c_str());    // mode() 전에 설정해야 적용됨 (core 2.x)
    WiFi.mode(useSta ? WIFI_AP_STA : WIFI_AP);
    WiFi.setSleep(false);              // modem sleep OFF — AP 응답 지연 누적 방지
    WiFi.setAutoReconnect(false);      // core 자동재연결은 NO_AP_FOUND에도 즉시 재시도 → 스캔 폭주
    WiFi.persistent(false);
    WiFi.onEvent(onEvent);
    const IPAddress ap = apIP();
    WiFi.softAPConfig(ap, ap, AP_SUB);
    // 최대 접속 10 (기본 4) — 폰 여러 대 + 슬레이브 보드가 같은 슬롯을 공유
    WiFi.softAP(_s.apSSID.c_str(), _s.apPass.c_str(), 1, 0, 10);
    Serial.printf("[AP] SSID: %s  http://%s  (보드 #%d)\n", _s.apSSID.c_str(),
                  ap.toString().c_str(), _s.boardId);

    _dns.start(53, host + ".local", ap);
    Serial.printf("[DNS] AP DNS 서버 시작 (%s.local → %s)\n", host.c_str(), ap.toString().c_str());

    if (useSta) {
        if (_s.staStatic) {
            IPAddress ip, gw, sn, dns;
            if (ip.fromString(_s.staStaticIP) && gw.fromString(_s.staGW) && sn.fromString(_s.staSN)) {
                if (!dns.fromString(_s.staDNS)) dns = gw;
                WiFi.config(ip, gw, sn, dns);
                Serial.printf("[STA] 고정 IP: %s / GW: %s\n", _s.staStaticIP.c_str(), _s.staGW.c_str());
            } else {
                Serial.println("[STA] 고정 IP 주소 오류 — DHCP로 대체");
            }
        }
        WiFi.begin(_s.staSSID.c_str(), _s.staPass.c_str());
        Serial.printf("[STA] 연결 시도: %s (%s)\n", _s.staSSID.c_str(),
                      _s.staStatic ? "고정 IP" : "DHCP");
        _lastTryMs = millis();
    } else {
        Serial.println("[STA] 미설정 (설정 페이지에서 홈 WiFi 입력 가능)");
    }

    if (MDNS.begin(host.c_str())) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[mDNS] http://%s.local 등록 완료\n", host.c_str());
    } else {
        Serial.println("[mDNS] 시작 실패");
    }
}

void WifiManager::update() {
    _dns.processNextRequest();

    // STA 재연결 백오프: 30s → 60s → … 최대 5분. 스캔 1회 동안 AP 통신이 잠깐 멈추므로 드물게.
    if (_s.staSSID.length() == 0) return;
    if (staConnected()) { _fails = 0; _lastTryMs = millis(); return; }
    uint32_t wait = 30000UL << (_fails < 4 ? _fails : 4);
    if (wait > 300000UL) wait = 300000UL;
    if (millis() - _lastTryMs < wait) return;
    _lastTryMs = millis();
    if (_fails < 255) _fails++;
    Serial.printf("[STA] 재연결 시도 #%u: %s\n", (unsigned)_fails, _s.staSSID.c_str());
    WiFi.reconnect();
}
