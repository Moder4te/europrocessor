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
    // 보드 번호 1~9 — 다중 보드 운용 시 이름/AP 대역 충돌 방지
    //   이름: 1=europrocessor, N=europrocessorN (.local)
    //   AP:   192.168.(3+N).1 → 1번=4.1(기존 호환), 2번=5.1, 3번=6.1
    //   (슬레이브가 1번 AP에 STA로 붙을 때 자기 AP와 같은 4.x 대역이면 라우팅이 깨짐)
    int    boardId     = 1;
};

class WifiManager {
public:
    void load();                 // Preferences → _s
    void begin();                // AP+STA 기동 + DNS + mDNS (load 이후 호출)
    void update();               // loop()에서 매 사이클 (비차단) — DNS + STA 재연결 백오프
    void save();                 // _s → Preferences (호출 후 ESP.restart() 권장)

    WifiSettings&       settings()       { return _s; }
    const WifiSettings& settings() const { return _s; }

    static bool   staConnected() { return WiFi.status() == WL_CONNECTED; }
    static String staIP()        { return staConnected() ? WiFi.localIP().toString() : String(""); }

    IPAddress apIP()     const { return IPAddress(192, 168, 3 + _s.boardId, 1); }
    String    hostName() const { return _s.boardId <= 1 ? String("europrocessor")
                                                        : String("europrocessor") + _s.boardId; }

private:
    static void onEvent(arduino_event_id_t event, arduino_event_info_t info);

    WifiSettings _s;
    DNSServer    _dns;
    Preferences  _prefs;
    uint32_t     _lastTryMs = 0;
    uint8_t      _fails     = 0;
};
