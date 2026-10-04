// ================================================================
//  WifiManager.h — AP(+STA) / 이름.local / 자체 DNS / 설정 영구저장
//  ----------------------------------------------------------------
//  · 이름: AP SSID를 mDNS 호스트 규칙(소문자·숫자·'-')으로 변환 → http://이름.local
//    새로 저장하는 SSID는 규칙을 강제(isValidApName). 기존 SSID는 변환만 하고 바꾸지 않음
//    (바꾸면 슬레이브·폰의 저장된 연결이 끊김).
//  · 자체 DNS(Core 0 태스크): AP 클라이언트의 질의에 자기 이름 → AP IP,
//    연결된 다른 보드 이름 → mDNS로 찾아 대신 응답 (안드로이드처럼 .local을 못 푸는 폰 대응).
//    그 외 도메인은 NXDOMAIN.
//  · STA는 항상 DHCP (고정 IP 옵션 제거 — 마스터가 접속 목록으로 자동 탐색).
//  · 보드 번호 1~9: AP 대역 192.168.(3+N).1 (슬레이브 STA 대역과 충돌 방지).
// ================================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

struct WifiSettings {
    String apSSID  = "europrocessor";
    String apPass  = "12345678";
    String staSSID = "";
    String staPass = "";
    int    boardId = 1;
};

class WifiManager {
public:
    void load();                 // Preferences → _s
    void begin();                // AP(+STA) 기동 + mDNS + DNS 태스크 (load 이후)
    void update();               // loop() — STA 재연결 백오프 (비차단)
    void save();                 // _s → Preferences (호출 후 재부팅)

    WifiSettings&       settings()       { return _s; }
    const WifiSettings& settings() const { return _s; }

    static bool   staConnected() { return WiFi.status() == WL_CONNECTED; }
    static String staIP()        { return staConnected() ? WiFi.localIP().toString() : String(""); }

    IPAddress apIP()     const { return IPAddress(192, 168, 3 + _s.boardId, 1); }
    String    hostName() const;  // AP SSID → mDNS 이름 (빈 결과면 europrocessor[N])

    static String toHostName(const String& s);      // 규칙에 맞게 변환 (소문자, 그 외 문자 → '-')
    static bool   isValidApName(const String& s);   // ^[a-z0-9]([a-z0-9-]{0,30}[a-z0-9])?$

private:
    static void onEvent(arduino_event_id_t event, arduino_event_info_t info);
    static void dnsTask(void* self);
    void        dnsLoop();

    WifiSettings _s;
    Preferences  _prefs;
    uint32_t     _lastTryMs = 0;
    uint8_t      _fails     = 0;
};
