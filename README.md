# 아날로그 필름 로터리 프로세서 펌웨어 (europrocessor)

ESP32-S3 기반 **아날로그 필름 현상용 로터리 프로세서** 제어 펌웨어입니다.
스테퍼 모터로 현상 탱크를 정/역 교반하고, 약품 온도를 실시간 측정하며,
웹 UI와 물리 디스플레이(TFT + 로터리 인코더)로 레시피/수동 운전을 제어합니다.

`v4.0`에서 단일 `.ino` 모놀리식 구조를 **PlatformIO 기반 C++ OOP**로 전면 이관했습니다.
`v4.4~v4.9.3`: 안정성 전수 개선(PCNT 패닉 수정·노이즈 가드·운전 중 플래시 차단), 웹 OTA, 보드별 핀 배치(칩 MAC 자동 선택),
무디스플레이·N8R2 지원, WiFi 검색, 온도 보정, **정전·리셋 복구**, **하드웨어 감시(DIAG/VM)**,
**기존 제어 화면에서 여러 대 동시 제어(자동 탐색)**, **AP 이름 = 접속 주소(`이름.local`)**.

> 🧾 **현존 이슈·변경 이력·실측 결과:** [`ISSUES.md`](ISSUES.md)

> 📖 **전체 설명서(HTML):** [`docs/manual.html`](docs/manual.html) — 브라우저로 열면 사이드바 네비게이션이 있는 단일 파일 설명서. 하드웨어·전원·아키텍처·모듈 API·상태머신·크래시 분석·웹 API·트러블슈팅을 한 곳에 정리.

---

## 1. 하드웨어 사양

| 항목 | 사양 |
|---|---|
| **MCU** | ESP32-S3 N16R8 (16MB Flash / 8MB OPI PSRAM) |
| **모터** | NEMA17 + 3.71:1 유성기어, 1.8°/스텝 (200 step/rev) |
| **드라이버** | TMC2209 (STEP/DIR/EN), StealthChop, 1/8 마이크로스텝 → 1600 step/rev |
| **온도센서** | MAX31865 + PT100, 3선식, RREF 412Ω(실측) / RNOMINAL 100Ω — *현재 보드 불량으로 격리(`Cfg::TEMP_SENSOR_PRESENT=false`)* |
| **디스플레이** | ST7789 320×240 TFT (하드웨어 FSPI) — 패널 색반전 보정 `invertDisplay(false)` |
| **입력** | EC11 로터리 인코더(A/B/PUSH) + 확정 버튼(KO) |
| **전원** | USB-PD 100W → CH224K 디코이로 20V 트리거 → 모터 직결 + 벅(20V→5V)→ESP (↓ 전원 아키텍처) |

### 속도 계산
- 출력축 RPM → 모터 RPM × 3.71 → step/s
- 출력 80RPM(최대) = 모터 297RPM = **7,915 step/s** (`MAX_SPEED`)
- 80 초과 시 탈조는 코드 결함이 아니라 토크-속도 한계(back-EMF). 권장 운전 75RPM.

### 교반 시나리오
정방향 `rotIntSec` 구동 → 감속 정지 → **REST(EN=HIGH, 코일 전류 차단 = 관성 안정 + 발열 억제)** 2초 →
역방향 구동 → … 무한 반복. 레시피 모드에서는 각 단계가 `속도/지속시간/방향전환주기`를 가집니다.
REST는 가속도 곡선 감속이 **완전히 끝난 뒤**(`!isRunning()`) 시작하므로, 2초는 관성 정지 후의 순수 휴식 구간입니다.

### 전원 아키텍처

```
USB-C PD 충전기 (100W)
   │ USB-C
   ▼
[ CH224K ]  USB-PD 디코이/트리거 — CFG 저항으로 20V 선택
   │ 20V
   ├───────────────────────────────► TMC2209 VMOT (모터 전원, 직결)
   │
   ├─[ 470µF 캐패시터 ]  벌크 — 정/역 전환 트랜지언트 흡수
   │
   └──► [ 벅 컨버터 MP1584EN ]  20V → 5V 고정
            │ 5V
            ▼
        ESP32-S3 DevKit ─ 온보드 LDO → 3.3V ─ 로직 / 센서 / 디스플레이 / 인코더
```

- **CH224K (USB-PD 디코이):** PD 소스와 협상해 **20V 고정** 출력. 전압 선택은 **CFG 핀 저항**으로 설정. 100W(20V/5A) 소스라 모터+로직 피크 전류에 여유 충분.
- **470µF 캐패시터:** 20V 레일 벌크 캐패시터. 스테퍼 정/역 전환 시 역기전력·인덕티브 트랜지언트를 흡수. (계산상 모터발 레일 상승은 +1~2V 수준 — 캡이 충분히 억제)
- **벅 컨버터 (20V→5V):** ESP+디스플레이 공급(부하 ~0.5A). 부하는 가볍지만 **20V→5V 강하비가 커 발열·입력마진(MP1584 최대 26V)이 빠듯** → 양품 + **입력정격 여유 부품(XL4015/LM2596HV 등 36V급) 권장.** (과부하가 아니라 개체 불량·발열로 사망 사례 있음)
- **인러시/핫플러그:** PD가 5V→20V로 **부드럽게 램프업 + 전류제한** → 별도 NTC/소프트스타트/안티스파크 **불필요.** 단, 살아있는 20V에 커넥터 직삽은 지양.
- **전원 스위치(선택):** 비상정지는 **모터 전원(20V→TMC) 차단용**으로 두는 게 안전(펌웨어 소프트 e-stop의 물리 백업). 꽂을 땐 ON 유지, 비상시에만 OFF.

> 모터는 Core 1 + FastAccelStepper 하드웨어 ISR이라 디스플레이/웹 부하와 무관하게 타이밍 보장. 벅이 죽어도 모터 전원(20V 직결)은 영향 없음.

---

## 2. 핀 배정

```
─ A변 (3v3 쪽) ──────────────────────────────────────────────────
ST7789    SCK=GPIO12  MOSI=GPIO11  CS=GPIO10  DC=GPIO9  RST=GPIO8  BL=GPIO18 (FSPI)
EC11      A=GPIO17    B=GPIO16     PUSH=GPIO15
KO 버튼   GPIO7
─ B변 (V5in 쪽) ─────────────────────────────────────────────────
MAX31865  CS=GPIO13   MOSI=GPIO14  MISO=GPIO21  CLK=GPIO47  (소프트웨어 SPI, 연속 4핀)
TMC2209   EN=GPIO40   DIR=GPIO41   STEP=GPIO42  (EN: LOW=코일활성, HIGH=차단, 연속 3핀)
```

위는 기본 배치(`DEFAULT_PINS`, 바디1)입니다. **보드마다 배선이 달라 핀은 `src/PinMap.h` 한 파일에서 관리**합니다.

- 부팅 시 **칩 MAC으로 `BOARDS[]`에서 배치를 자동 선택**, 미등록 보드는 `DEFAULT_PINS`. 같은 `.bin`을 어느 보드에 OTA해도 안전.
- 항목: 핀 17개 + `hasDisplay`(false면 TFT/인코더 핀을 전혀 구동하지 않고 화면 태스크 미기동) + `rtdWires`(0=온도센서 격리, 2/3/4=결선)
  + `diagPin`·`vmPin`·`vmRatio`(하드웨어 감시, 0=미배선 — 아래 「안전 설계」).
- **금지 GPIO**(strapping 0/3/45/46, USB 19/20, Flash·PSRAM 26~37, UART0 43/44, RGB LED 48)·**중복 핀**·**인코더 핀 ≥32**는 `static_assert`로 빌드 단계에서 차단.
- 새 보드 등록: 웹 설정 → 디바이스 정보의 MAC 확인 → `BOARDS[]`에 한 줄 추가 → 빌드·OTA.

| 보드 | MAC | 사양 | 배치 |
|---|---|---|---|
| 바디1 | `A0:F2:62:E5:D9:B0` | N16R8, 디스플레이 | 기본 배치, 온도센서 격리(불량 보드) |
| 바디2 | `D0:CF:13:59:14:78` | N8R2, 무디스플레이 | STEP 5 · DIR 6 · EN 7 · MAX CS 2 / MOSI 38 / MISO 39 / CLK 40, PT100 2선 모드 |

---

## 3. 아키텍처

### 듀얼코어 분리
시간 크리티컬한 모터 제어와 비차단 통신/UI를 코어로 분리합니다.

| 코어 | 담당 |
|---|---|
| **Core 1** (`loop()`) | 모터 상태머신 + 레시피 타이밍 (시간 크리티컬). FastAccelStepper 하드웨어 ISR이 스텝 펄스를 독립 생성 |
| **Core 0** | WiFi 스택 + AsyncWebServer + `tempTask`(온도 1Hz) + `displayTask`(UI 25Hz) |

**핵심 규약**: 웹 핸들러·디스플레이(Core 0)는 모터/레시피 상태를 **직접 건드리지 않습니다.**
모든 조작은 `CommandQueue`로 enqueue → Core 1의 `loop()`이 dispatch하여 단일 코어에서만 상태를 변경합니다.
이로써 `mCtx`/`rCtx`/stepper 레이스를 원천 제거합니다.

### 크로스코어 동기화
- **CommandQueue** — 일반 명령(시작/정지/일시정지/확인/수동)을 직렬 전달
- **비상정지 플래그** — 큐가 가득 차도 유실되면 안 되므로 큐를 우회하는 `volatile` 플래그
- **레시피 스테이징** — `/api/start`는 뮤텍스 보호 버퍼에 적재만 하고, `loop()`이 안전한 시점에 꺼내 적용
- **뮤텍스** — 온도 스냅샷, 레시피 `steps` 벡터, 스테이징 버퍼 각각 보호

### 모듈 구성 (`src/`)

```
main.cpp              App 코디네이터 — 객체 소유·배선, setup/loop, 상태 전이
Config.h              물리 상수 (Cfg::, constexpr) · FW_VERSION · 노이즈/온도 임계값
PinMap.*              보드별 핀 배치 + hasDisplay + rtdWires (칩 MAC 자동 선택, static_assert 검증)
NoiseGuard.*          입력 노이즈 버스트 감시 → 패널 입력 차단/모터 정지, 연속 비정상 리셋 시 모터 잠금
HwSafety.*            TMC2209 DIAG / 모터 전원 VM 감시 → 즉시 차단·잠금 (PinMap에 배선된 보드만)
Types.h               공유 도메인 타입 (MotorState, StepInfo, CmdType, Cmd)
CommandQueue.h        크로스코어 명령 큐 + 비상정지 플래그
ISaver.h              화면보호기 설정 인터페이스 (+ 무디스플레이용 NullSaver)
MotionController.*     TMC2209 스테퍼 상태머신 (레시피 무관)
RecipeRunner.*         레시피 단계 시퀀서 (steps + 뮤텍스 소유)
TemperatureSensor.*    MAX31865 — Core 0 태스크, 뮤텍스 보호
WifiManager.*          AP(+STA, 항상 DHCP), AP 이름=mDNS 이름, 자체 DNS 태스크, Preferences
WebServer.*            ESPAsyncWebServer 라우트 + 상태 JSON + 스테이징
web_assets.h          웹 UI HTML/CSS/JS (PROGMEM 임베드)
web_multi.h           (구) 멀티 보드 페이지 /multi — IP 수동 입력. 집 공유기 공유 구성용으로 유지
DisplayUI.*            ST7789 + 인코더 + 버튼 UI (Core 0 태스크, ISaver 구현)
```

---

## 4. 모듈 상세

### `main.cpp` — App 코디네이터
모든 서브시스템 객체를 소유하고 참조를 주입해 배선합니다. 시스템 상태 전이는 여기에 집약됩니다.
- `stopAll()` — 비상정지(모터 즉시 정지 + 레시피 초기화 + 모드 리셋)
- `safeStop()` — 가속도 곡선 따라 감속 후 정지 (탈조/관성 충격 없음)
- `manualStart()` — 수동 운전 시작
- `dispatch()` — 큐에서 꺼낸 명령을 위 함수로 분기
- `setup()` — 부팅 핀 안전 고정 → 서브시스템 초기화 → 태스크 기동
- `loop()` — 비상정지 확인 → 명령 dispatch → 레시피 스테이징 적용 → DNS/모터/레시피 업데이트

### `MotionController` — 모터 상태머신
FastAccelStepper로 펄스를 생성하고 EN 핀은 수동 제어(REST 구간 코일 차단)합니다.
**레시피를 모릅니다** — 단계 종료는 `requestStepStop()`으로 받고, 감속 완료는 `takeStepStopped()` 이벤트로 알립니다.

상태: `IDLE → RUN_FWD → STOP_FWD → REST → RUN_REV → STOP_REV → REST → …`
레시피 단계 종료 시 `STOP_RECIPE`, 안전 정지 시 `STOP_SAFE`.

### `RecipeRunner` — 레시피 시퀀서
`steps` 벡터를 뮤텍스로 보호(웹 `buildStatus`가 Core 0에서 동시 읽기)하며,
모터는 `MotionController` 경유로만 구동합니다.
- `load()` → `startStep(0)` → 단계 지속시간 경과 시 `requestStepStop()`
- → 감속 완료(`takeStepStopped`) → `waitConfirm`(약품 교체 대기) → `confirm()` → 다음 단계
- `pauseToggle()` / `snapshot()`(상태 표시용 스냅샷)

### `TemperatureSensor` — 온도 측정
Core 0 전용 태스크에서 MAX31865를 1Hz 폴링(소프트웨어 SPI). 모터 코어에 영향 없음.
fault는 즉시 clear하지 않고 **연속 5회 누적 후에만** 시도 → 단선/접촉불량을 UI에서 끊김 없이 관찰.
- **결선 모드**: 보드별 `PinMap.h` `rtdWires`(2/3/4) — 보드 솔더점퍼와 일치 필수.
- **진단**: 부팅 시 threshold 레지스터 SPI 라운드트립 자가진단(칩·통신 검증) + fault 비트 디코드 + raw RTD 값 출력.
- **격리**: `rtdWires=0`이면 init·폴링 태스크 미생성 → `temperature()=TEMP_UNREAD`, fault 없음(화면 `--.-`). 바디1은 센서 보드 불량으로 격리.
- **오류 판정**: SPI 미통신(0x01), 유효범위(-20~100°C) 밖(0x02)을 칩 fault 비트와 함께 처리. 1초에 2°C 넘는 점프는 스파이크로 버림.
- **보정**: 측정저항에 `R = gain·R_meas + offset` 선형 보정(NVS `tcal`, 보드별). 웹 설정 "온도 보정"에서 1점/2점(예: 0°C 얼음물 + 38°C).

### `WifiManager` — 네트워크
홈 WiFi(STA) 미설정이면 AP 단독, 설정 시 AP+STA(**항상 DHCP — 고정 IP 옵션 없음**). 설정은 Preferences `wifi`에 영구 저장.
- **AP 이름 = 접속 주소**: AP SSID를 mDNS 호스트 이름으로 씀 → `http://이름.local`. 새 SSID는 규칙(영문 소문자·숫자·`-`, 1~32자, 하이픈 시작·끝 금지)을 웹 입력에서 변환·서버에서 검증(위반 시 400). 기존 규칙 밖 SSID는 유지하고 이름만 변환(`FilmProcessor` → `filmprocessor.local`).
- **자체 DNS(Core 0 태스크)**: AP 클라이언트 질의에 자기 이름 → AP IP, 연결된 다른 보드 이름 → mDNS로 찾아 대리 응답(60초 캐시), 그 외 NXDOMAIN. `.local`을 못 푸는 안드로이드도 이름으로 접속.
- **보드 번호(1~9)**: AP 대역 `192.168.(3+N).1` — 1번=4.1, 2번=5.1. 다른 보드 AP에 STA로 붙어도 대역 충돌 없음. AP 최대 접속 10.
- **STA 재연결 백오프** 30s→최대 5분(즉시 재시도 시 채널 스캔으로 AP 통신이 끊기던 문제 방지), WiFi 절전 OFF.

### 여러 대 동시 제어 (마스터/슬레이브)
1. 슬레이브 보드: 설정 → 홈 WiFi **[검색]** → 마스터 AP 선택 → 비밀번호 → 저장 (DHCP로 연결).
2. 폰을 마스터 AP에 연결 → 마스터 웹 화면 상태 탭 **🔗 동시 제어** 켜기 → `/api/peers`(마스터 AP 접속 기기 IP) + 각 IP의 `/api/status`로 보드를 자동 발견(MAC으로 기억).
3. 이 화면의 시작·정지(재시도)·안전정지·일시정지·다음 단계·건너뛰기·단계 이동·수동 운전이 선택된 보드 전체에 전달. 일시정지는 보드별 상태에 맞춰(멈출 보드만/재개할 보드만), 다음 단계는 확인대기 보드에만 → 보드 간 상태 엇갈림 방지. 실패한 보드는 알림.
- 모든 보드가 집 공유기에 함께 붙은 구성은 자동 탐색 대상이 아님 → `/multi`에서 IP 입력.

### `WebServer` — HTTP
ESPAsyncWebServer 기반. HTML은 `web_assets.h`에 PROGMEM 임베드.
의존성(motion/recipe/temp/cmd/wifi/saver)을 `begin()`에서 주입받아 소유하지 않습니다.
대용량 JSON(`/api/start`, 레시피 저장)은 청크 분할 수신 + reserve로 힙 단편화를 억제합니다.

### `DisplayUI` — 물리 UI
ST7789 320×240 + EC11 인코더 + KO 버튼. U8g2로 한글 UTF-8 폰트 출력.
캐시 기반 부분 렌더링으로 깜빡임 제거. `initTft`에서 **`invertDisplay(false)`로 패널 색반전 보정**(이 패널은 INVON이 색을 네거티브로 반전).
`ISaver`를 구현해 웹에서 화면보호기 설정을 읽고 씁니다.

**화면보호기 — RGB565 애니메이션 (온디바이스 GIF 디코드 완전 제거):**
- 온디바이스 GIF 디코드(AnimatedGIF)는 이 빌드에서 실패 → **라이브러리·코드 모두 제거**하고 **브라우저에서 사전 인코딩**한 RGB565 프레임을 기기는 디코드 없이 blit.
- 업로드 시 **브라우저(`web_assets.h`)**가 GIF를 160×120 RGB565 `ANM1` 애니메이션으로 트랜스코드(최대 8프레임, 비율 유지 레터박스) 후 전송. 기기는 순수 blit만.
- 우선순위: **`/saver.anim`(업로드) > `/saver.jpg`(정적) > 임베드 Nyan(기본)**.
- `/saver.anim` 포맷: `"ANM1"` + W,H,frames,delay(u16 LE) + frames×W×H×2 RGB565(LE). 기기는 160×120 프레임을 PSRAM 로드 후 **2×→320×240**로 blit.
- 임베드 Nyan은 `tools/gen_nyan.py`가 생성한 `nyan_frames.h`(flash 상수, RAM 0).

**크래시 방지 — 플래시op ↔ 인코더 인터럽트 게이팅:** WiFi NVS·LittleFS 쓰기 등 플래시 연산이 캐시를 끄는 순간 인코더 인터럽트(모터 EMI로 유발)가 뜨면 *"Cache disabled but cached memory region accessed"* Core 1 panic이 난다. `encISR`은 `REG_READ`+`DRAM_ATTR` 테이블로 IRAM-safe화했지만 Arduino `attachInterrupt` 디스패처가 플래시를 건드려 잔존. **최종 차단 = 모터 비-IDLE이면 모든 플래시op 금지** — 화면보호기 진입/렌더 안 함, 업로드는 시작 거부 + 진행 중 모터 기동 시 중단·부분파일 삭제, 삭제는 409. (PCNT 인코더로의 전환은 FastAccelStepper가 PCNT를 점유해 불가)

페이지: STATUS / 레시피경고 / 수동메뉴 / 속도·주기·화면보호기시간 편집 / 정보 / 화면보호기.

---

## 5. 웹 API

| 메서드 | 경로 | 설명 |
|---|---|---|
| GET | `/` | 웹 UI (HTML) |
| GET | `/api/status` | 모터/레시피/온도/STA 상태 JSON |
| POST | `/api/start` | 레시피 시작 (steps 배열, 청크 수신 → 스테이징) |
| POST | `/api/stop` | 비상정지 (큐 우회 플래그) |
| POST | `/api/pause` | 일시정지/재개 토글 |
| POST | `/api/confirm` | 다음 단계 확인 (약품 교체 후 진행) |
| POST | `/api/manual` | 수동 운전 (속도/방향/사이클/주기) |
| GET·POST | `/api/settings` | WiFi 설정 조회/저장(저장 시 재부팅) |
| GET·POST | `/api/recipes/load`·`/save` | 레시피 JSON 불러오기/저장(LittleFS, 원자적 교체) |
| GET·POST | `/api/saver/settings` | 화면보호기 활성/타임아웃 |
| POST | `/api/skip` · `/api/safestop` | 단계 건너뛰기 / 감속 정지 (둘 다 감속 후 동작) |
| POST | `/api/goto` | `{"step":N}` 지정 단계로 이동 (진행도 바 클릭 → 확인창) |
| GET | `/api/recipe` | 실행 중 레시피의 단계 목록(이름·시간·RPM) — 모든 클라이언트가 같은 진행도 바 |
| GET | `/multi` | 멀티 보드 제어 페이지 (상태·정지·일시정지·다음단계·레시피 동시 시작) |
| POST | `/api/ota` | 펌웨어 업로드(`X-OTA` 헤더 필수). 사양(플래시 용량) 불일치·운전 중·동시 업로드 거부. 크래시 루프 시 이전 펌웨어로 자동 롤백 |
| GET | `/api/wifi/scan` | 주변 WiFi 비동기 검색(`?refresh=1` 강제). 같은 SSID 병합·신호순 |
| POST | `/api/temp/cal` | 온도 보정 `{"gain","offset"}` 또는 `{"reset":true}` |
| GET | `/api/peers` | 이 보드 AP에 접속한 기기 IP 목록 `{"ap":[…],"noIp":n}` (동시 제어 자동 탐색) |
| POST | `/api/recovery` | 정전 복구 선택 `{"action":"resume"|"restart"|"next"|"discard"}` (모터 잠금·운전 중 409) |
| POST | `/api/hwfault/clear` | 하드웨어 고장 해제 (원인이 사라졌을 때만 실제 해제) |
| POST·DELETE | `/api/saver/image` | 화면보호기 업로드(매직바이트로 JPEG/GIF/`ANM1` 판정 → `/saver.{jpg,gif,anim}`, 파일핸들 유지로 대용량 안정)/삭제. **모터 운전 중 차단**(업로드 거부·진행 중 중단, 삭제 409 — 플래시op 크래시 회피) |

`/api/status` 추가 키: `fw`·`variant`·`name`·`boardId`·`mac`·`pinProfile`·`upSec`·`heapFree/Min/Max`·`guard`·`resetReason`·`rssi`·`apClients`·`tempFaultCode`·`rtdWires`·`rtdOhm`·`calGain/calOffset`·`hwFault`·`vm`·`recovery`(복구 대기 시 이름·단계·경과·상태).

**운전 중 차단**: 모터 회전·감속 중이거나 레시피 진행(일시정지·확인대기 포함) 중이면 플래시 쓰기·재부팅이 따르는 요청(설정 저장, 레시피 저장, 화면보호기, OTA, 온도 보정)은 모두 **409**. 웹은 레시피 편집을 "저장 대기"로 미뤘다가 정지 후 자동 저장.

---

## 6. 빌드 & 플래시 (PlatformIO)

**칩 사양이 2종이라 빌드 환경도 2개**입니다. 플래시 용량·PSRAM 종류는 빌드 타임 설정이라 바이너리가 다릅니다.

| 환경 | 칩 | 파티션 | 대상 |
|---|---|---|---|
| `esp32-s3-devkitc-1` | N16R8 (16MB / 8MB Octal PSRAM) | `partitions.csv` | 바디1 |
| `n8r2` | N8R2 (8MB / 2MB Quad PSRAM) | `partitions_8MB.csv` | 바디2 |

```bash
pio run -e esp32-s3-devkitc-1 -e n8r2               # 두 사양 모두 빌드
pio run -e n8r2 -t upload --upload-port COMx        # USB 업로드 (최초 1회 또는 파티션 변경 시)
pio device monitor --dtr 0 --rts 0                  # 시리얼 모니터 — ★DTR/RTS 끄기★
```

- **OTA(권장)**: 웹 설정 → "펌웨어 업데이트"에 `.pio/build/<환경>/firmware.bin` 업로드. 사양이 다른 `.bin`은 보드가 `wrong board variant`로 거부.
- **⚠ USB 시리얼 포트를 닫으면 보드가 리셋**됩니다(USB-Serial-JTAG). 운전 중 모니터를 닫거나 케이블을 뽑지 마세요. 크래시 로그는 UART0(GPIO43/44) TTL로 받는 게 안전합니다.
- `firmware/`, `backups/`(보드 플래시·레시피 백업)는 git 제외.

- `data/` 폴더가 없으므로 `uploadfs`는 불필요합니다(레시피·화면보호기는 런타임 생성).
- 시리얼 로그가 안 보이면 보드 **EN/RESET**을 한 번 누르세요(USB-CDC 재연결 타이밍).

### 빌드 플래그 (`platformio.ini`)
| 플래그 | 의미 |
|---|---|
| `UI_DISPLAY_PRESENT=1` | 화면 코드 포함 여부. 보드별 화면 유무는 `PinMap.h` `hasDisplay`로 런타임 결정 |
| `FW_VARIANT` | 칩 사양 태그(`N16R8`/`N8R2`) — 웹·로그 표시 |
| `BOARD_HAS_PSRAM` + `memory_type=qio_opi` | N16R8 OPI PSRAM 활성 |
| `ARDUINO_USB_CDC_ON_BOOT=1` | 네이티브 USB 포트로 Serial 출력 |
| `CORE_DEBUG_LEVEL=0` | 코어 로그 억제(디버깅 시 3) |

`partitions.csv` — 3MB APP ×2 (OTA) + 9.9MB LittleFS (16MB). `partitions_8MB.csv` — 3MB APP ×2 + 1.9MB LittleFS (8MB).

---

## 7. 도구 (`tools/`)

`gen_nyan.py` — GIF → RGB565 프레임 헤더(`src/nyan_frames.h`) 생성 (임베드 Nyan 화면보호기). LANCZOS로 160×120 리사이즈, 프레임/딜레이 자동 추출. Pillow 필요: `python -m pip install Pillow`.

---

## 8. 안전 설계 (물리 손상 방지)

- **부팅 즉시 TMC2209 EN=HIGH** — 리셋~setup 구간의 floating으로 코일이 통전된 채 정지 → 발열/소손 방지. 권장 HW 풀업: EN→3.3V 10K.
- **부팅 즉시 panel 신호 핀 idle 고정** — 부팅 직후 floating noise가 패널 컨트롤러를 비정상 state로 몰아 ESD/latch-up 손상 누적 방지. 권장 HW 풀업: TFT CS·RST→3.3V 10K.
- **디스플레이 배선 주의** — 신호선을 5V(VBUS) 근처로 라우팅하지 말 것. 5V가 신호핀에 닿으면 내부 ESD 다이오드를 통해 VCC 레일로 역주입되어 컨트롤러/백라이트가 손상될 수 있음(실손상 사례 있음).
- **냉각** — 75RPM 연속 2h+ 시 모터·전자부 ≈50°C(실측). 발열은 I²R 지배 → VREF 최소 유지 정책.
- **PCNT 패닉 회피** — IDF 4.4 pcnt 드라이버가 객체를 PSRAM에 잡아, 모터 운전 중 플래시 접근 시 `Cache disabled` 패닉(reset 4). FAS 초기화 동안 PSRAM을 점유해 내부 RAM 할당 강제(`MotionController.cpp`).
- **운전 중 플래시 쓰기·재부팅 금지** — 플래시 소거 동안 모터 펄스 큐 보충이 멈춰 덜컹일 수 있음. 웹 API 409 + NVS 쓰기 지연.
- **노이즈 가드** — 인코더/버튼 노이즈 버스트 → 패널 입력 3초 차단, 60초 내 3회 → 모터 정지(레시피는 일시정지). 연속 비정상 리셋 3회 → 모터 잠금(전원 재투입 해제).
- **감속 정지** — 일시정지·단계 이동·건너뛰기는 S-커브 감속 후 동작. 비상정지만 즉시.
- **정전·리셋 복구** — 레시피 진행 상태(단계 목록·현재 단계·경과·상태)를 NVS `rec`에 저장(모터가 멈춘 순간에만: 단계 시작 직전·확인대기·일시정지 완료·방향전환 휴지 중 10초마다 → 정전 시 경과는 휴지 주기 이내 근사). 재부팅 후 **모터 자동 구동 없음** — 웹 배너/TFT `RESUME?`에서 이어서·단계 처음부터·다음 단계·취소 선택. 비상정지·완료 시 기록 삭제.
- **하드웨어 감시 (`HwSafety`)** — `PinMap.h`에 `diagPin`/`vmPin`을 배선·등록하면 동작: DIAG HIGH 60ms → 코일 즉시 차단, 구동 중 VM<10.5V 0.5초 → 정지, VM>28V → 즉시 차단. 레시피는 일시정지로 보존(복구 가능), 모터 잠금 후 원인 해소 시 웹에서 해제. 권장 배선: DIAG→GPIO + 10kΩ 풀다운, VM—100kΩ—ADC1 핀(1~10)—10kΩ—GND(분압비 11) + 100nF.
- **전원** — 20V 라인에 퓨즈(1.5~2A), 드라이버 VM 근처 100~470µF, 5V 레귤레이터 출력에 쇼트키 다이오드(USB 5V 역류 방지) 권장. 전원 켠 채 모터 커넥터 탈착 금지.

---

## 9. 라이선스 / 작성

개인 프로젝트. 펌웨어는 Arduino-ESP32 + PlatformIO 환경에서 빌드합니다.
필요 라이브러리는 `platformio.ini`의 `lib_deps`에 명시되어 자동 설치됩니다.
