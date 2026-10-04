#include "WifiManager.h"
#include <ESPmDNS.h>
#include <WiFiUdp.h>

static const IPAddress AP_SUB(255, 255, 255, 0);

// ──────────────────────────────────────────────────────────────
// 이름 규칙 (mDNS 호스트 라벨, SSID 최대 32자)
// ──────────────────────────────────────────────────────────────
String WifiManager::toHostName(const String& s) {
    String h;
    for (size_t i = 0; i < s.length() && h.length() < 32; ++i) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) h += c;
        else if (h.length() && h[h.length() - 1] != '-') h += '-';   // 공백·_·.·한글 등 → '-'
    }
    while (h.length() && h[h.length() - 1] == '-') h.remove(h.length() - 1);
    return h;
}

bool WifiManager::isValidApName(const String& s) {
    if (s.length() < 1 || s.length() > 32) return false;
    if (s[0] == '-' || s[s.length() - 1] == '-') return false;
    for (size_t i = 0; i < s.length(); ++i) {
        const char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}

String WifiManager::hostName() const {
    String h = toHostName(_s.apSSID);
    if (!h.length()) h = _s.boardId <= 1 ? String("europrocessor") : String("europrocessor") + _s.boardId;
    return h;
}

// ──────────────────────────────────────────────────────────────
void WifiManager::load() {
    _prefs.begin("wifi", true);
    _s.apSSID  = _prefs.getString("ap_ssid",  "europrocessor");
    _s.apPass  = _prefs.getString("ap_pass",  "12345678");
    _s.staSSID = _prefs.getString("sta_ssid", "");
    _s.staPass = _prefs.getString("sta_pass", "");
    _s.boardId = constrain(_prefs.getInt("board_id", 1), 1, 9);
    _prefs.end();
}

void WifiManager::save() {
    _prefs.begin("wifi", false);
    _prefs.putString("ap_ssid",  _s.apSSID);
    _prefs.putString("ap_pass",  _s.apPass);
    _prefs.putString("sta_ssid", _s.staSSID);
    _prefs.putString("sta_pass", _s.staPass);
    _prefs.putInt   ("board_id", _s.boardId);
    _prefs.remove("sta_static");   // 고정 IP 옵션 제거 — 이전 기록 정리
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
    Serial.printf("[AP] SSID: %s  http://%s  http://%s.local  (보드 #%d)\n", _s.apSSID.c_str(),
                  ap.toString().c_str(), host.c_str(), _s.boardId);
    if (!isValidApName(_s.apSSID))
        Serial.printf("[AP] ⚠ SSID '%s'는 이름 규칙 밖 → 주소는 %s.local (설정에서 SSID 변경 권장)\n",
                      _s.apSSID.c_str(), host.c_str());

    if (useSta) {
        WiFi.begin(_s.staSSID.c_str(), _s.staPass.c_str());   // 항상 DHCP
        Serial.printf("[STA] 연결 시도: %s (DHCP)\n", _s.staSSID.c_str());
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

    // 자체 DNS — Core 0, 낮은 우선순위. mDNS 조회(수백 ms 대기)가 모터 루프(Core 1)를 막지 않게.
    xTaskCreatePinnedToCore(dnsTask, "dnsTask", 4096, this, 1, nullptr, 0);
}

void WifiManager::update() {
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

// ──────────────────────────────────────────────────────────────
// 자체 DNS 응답기 (UDP 53, AP 클라이언트용)
//   이름.local / 이름 → 자기 AP IP 또는 mDNS로 찾은 다른 보드 IP (60초 캐시, 실패 10초 캐시)
//   그 외 도메인 → NXDOMAIN (인터넷 없는 AP — 폰이 빨리 포기하게)
// ──────────────────────────────────────────────────────────────
void WifiManager::dnsTask(void* self) { static_cast<WifiManager*>(self)->dnsLoop(); }

void WifiManager::dnsLoop() {
    struct Entry { char name[33]; uint32_t ip; uint32_t ts; };
    Entry cache[8] = {};
    WiFiUDP udp;
    udp.begin(53);
    uint8_t buf[512];
    for (;;) {
        const int n = udp.parsePacket();
        if (n <= 0) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        const int len = udp.read(buf, sizeof(buf));
        if (len < 17 || (buf[2] & 0x80) || ((buf[4] << 8) | buf[5]) != 1) continue;   // 질의 1건만

        // QNAME → 소문자 "a.b.c"
        String qname; int pos = 12;
        while (pos < len && buf[pos]) {
            const int l = buf[pos++];
            if ((l & 0xC0) || pos + l > len) { pos = len; break; }
            if (qname.length()) qname += '.';
            for (int i = 0; i < l; ++i) { char c = (char)buf[pos + i]; if (c >= 'A' && c <= 'Z') c += 32; qname += c; }
            pos += l;
        }
        if (pos + 5 > len) continue;
        pos++;                                             // 0 terminator
        const uint16_t qtype = (buf[pos] << 8) | buf[pos + 1];
        const int qend = pos + 4;                          // 질문 영역 끝

        // 이름 결정 — "x.local" 또는 점 없는 "x"만 우리 담당
        String base = qname;
        const bool isLocal = qname.endsWith(".local");
        if (isLocal) base = qname.substring(0, qname.length() - 6);
        uint32_t ip = 0;
        bool ours = (isLocal || base.indexOf('.') < 0) && base.length() > 0 && base.length() <= 32;
        if (ours) {
            if (base == hostName()) ip = (uint32_t)apIP();
            else {
                Entry* hit = nullptr; Entry* slot = &cache[0];
                for (Entry& e : cache) {
                    if (e.name[0] && base == e.name) { hit = &e; break; }
                    if (e.ts < slot->ts) slot = &e;        // 가장 오래된 칸 재사용
                }
                const uint32_t now = millis() | 1;
                if (hit && now - hit->ts < (hit->ip ? 60000UL : 10000UL)) ip = hit->ip;
                else {
                    char nb[33]; base.toCharArray(nb, sizeof(nb));
                    ip = (uint32_t)MDNS.queryHost(nb, 400);   // 연결된 보드에 mDNS로 질의
                    Entry* e = hit ? hit : slot;
                    strncpy(e->name, nb, 32); e->name[32] = 0; e->ip = ip; e->ts = now;
                }
            }
        }

        // 응답: 헤더 + 질문 그대로 + (A 질의이고 찾았으면) 답 1개
        const bool answer = ip && (qtype == 1 || qtype == 255);
        uint8_t out[600];
        memcpy(out, buf, qend);
        out[2] = 0x84 | (buf[2] & 0x01);                   // QR=1, AA=1, RD 유지
        out[3] = ip || (ours && qtype != 1) ? 0x00 : 0x03; // NOERROR / NXDOMAIN
        out[6] = 0; out[7] = answer ? 1 : 0;               // ANCOUNT
        out[8] = out[9] = out[10] = out[11] = 0;           // NS/AR 없음
        int o = qend;
        if (answer) {
            const uint8_t rr[] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4 };   // 이름포인터, A, IN, TTL 60, 길이 4
            memcpy(out + o, rr, sizeof(rr)); o += sizeof(rr);
            out[o++] = ip & 0xFF; out[o++] = (ip >> 8) & 0xFF; out[o++] = (ip >> 16) & 0xFF; out[o++] = ip >> 24;
        }
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write(out, o);
        udp.endPacket();
    }
}
