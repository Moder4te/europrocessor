#include "WifiManager.h"
#include <ESPmDNS.h>

const IPAddress WifiManager::AP_IP(192, 168, 4, 1);
static const IPAddress AP_GW (192, 168, 4, 1);
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
    _prefs.end();
}

void WifiManager::onEvent(arduino_event_id_t event, arduino_event_info_t) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("[STA] 연결 끊김 — 재연결 시도");
            WiFi.reconnect();
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("[STA] 연결 복구 IP: %s\n", WiFi.localIP().toString().c_str());
            break;
        default: break;
    }
}

void WifiManager::begin() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.onEvent(onEvent);
    WiFi.softAPConfig(AP_IP, AP_GW, AP_SUB);
    WiFi.softAP(_s.apSSID.c_str(), _s.apPass.c_str());
    Serial.printf("[AP] SSID: %s  http://192.168.4.1\n", _s.apSSID.c_str());

    _dns.start(53, "europrocessor.local", AP_IP);
    Serial.println("[DNS] AP DNS 서버 시작 (europrocessor.local → 192.168.4.1)");

    if (_s.staSSID.length() > 0) {
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
    } else {
        Serial.println("[STA] 미설정 (설정 페이지에서 홈 WiFi 입력 가능)");
    }

    if (MDNS.begin("europrocessor")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("[mDNS] http://europrocessor.local 등록 완료");
    } else {
        Serial.println("[mDNS] 시작 실패");
    }
}

void WifiManager::processDns() {
    _dns.processNextRequest();
}
