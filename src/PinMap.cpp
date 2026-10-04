#include "PinMap.h"
#include <esp_system.h>   // esp_efuse_mac_get_default (IDF 4.4)
#include <cstring>

namespace {
struct Active {
    const PinMap* pins  = &DEFAULT_PINS;
    const char*   label = "기본 (미등록 보드)";
    bool          hasDisplay = true;
    uint8_t       rtdWires   = 0;
    char          mac[18] = "";
};
// 함수 내 static — 다른 전역 객체 생성자(TFT/MAX31865)가 먼저 불러도 그 시점에 1회 초기화
const Active& active() {
    static Active a = [] {
        Active r;
        uint8_t m[6] = {0};
        esp_efuse_mac_get_default(m);
        snprintf(r.mac, sizeof(r.mac), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
        for (const BoardPins& b : BOARDS) {
            if (strcmp(b.mac, r.mac) == 0) { r.pins = &b.pins; r.label = b.label; r.hasDisplay = b.hasDisplay; r.rtdWires = b.rtdWires; break; }
        }
        return r;
    }();
    return a;
}
}

const PinMap& pins()            { return *active().pins; }
const char*   pinProfileLabel() { return active().label; }
const char*   boardMac()        { return active().mac; }
bool          boardHasDisplay() { return active().hasDisplay; }
uint8_t       boardRtdWires()   { return active().rtdWires; }
