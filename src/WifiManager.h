// ================================================================
//  WifiManager.h — AP+STA 동시 모드 / DNS / mDNS / 설정 영구저장
//  ----------------------------------------------------------------
//  Preferences "wifi" 네임스페이스에서 설정 로드/저장.
//  STA 자동 재연결 이벤트 처리. AP DNS(captive) + mDNS 등록.
// ================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>

struct WifiSettings {
    String apSSID  = "FilmProcessor";
    String apPass  = "12345678";
    String staSSID = "";
    String staPass = "";
    bool   staStatic   = false;
    String staStaticIP = "192.168.1.100";
    String staGW       = "192.168.1.1";
    String staSN       = "255.255.255.0";
    String staDNS      = "8.8.8.8";
};

class WifiManager {
public:
    void load();                 // Preferences → _s
    void begin();                // AP+STA 기동 + DNS + mDNS (load 이후 호출)
    void processDns();           // loop()에서 매 사이클 (비차단)
    void save();                 // _s → Preferences (호출 후 ESP.restart() 권장)

    WifiSettings&       settings()       { return _s; }
    const WifiSettings& settings() const { return _s; }

    static bool   staConnected() { return WiFi.status() == WL_CONNECTED; }
    static String staIP()        { return staConnected() ? WiFi.localIP().toString() : String(""); }

    static const IPAddress AP_IP;

private:
    static void onEvent(arduino_event_id_t event, arduino_event_info_t info);

    WifiSettings _s;
    DNSServer    _dns;
    Preferences  _prefs;
};
