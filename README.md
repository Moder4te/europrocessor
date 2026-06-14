# 아날로그 필름 로터리 프로세서 펌웨어 (europrocessor)

ESP32-S3 기반 **아날로그 필름 현상용 로터리 프로세서** 제어 펌웨어입니다.
스테퍼 모터로 현상 탱크를 정/역 교반하고, 약품 온도를 실시간 측정하며,
웹 UI와 물리 디스플레이(TFT + 로터리 인코더)로 레시피/수동 운전을 제어합니다.

`v4.0`에서 단일 `.ino` 모놀리식 구조를 **PlatformIO 기반 C++ OOP**로 전면 이관했습니다.

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

핀 상수는 모두 `src/Config.h`의 `namespace Pin`에 `constexpr`로 정의됩니다.
ESP32-S3 strapping(0/3/45/46), UART0(43/44), USB(19/20), Flash(26~32),
OPI PSRAM(35~37), RGB LED(48) 핀은 모두 회피했습니다.

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
Config.h              핀/물리 상수 (Pin:: / Cfg::, constexpr)
Types.h               공유 도메인 타입 (MotorState, StepInfo, CmdType, Cmd)
CommandQueue.h        크로스코어 명령 큐 + 비상정지 플래그
ISaver.h              화면보호기 설정 인터페이스 (+ 무디스플레이용 NullSaver)
MotionController.*     TMC2209 스테퍼 상태머신 (레시피 무관)
RecipeRunner.*         레시피 단계 시퀀서 (steps + 뮤텍스 소유)
TemperatureSensor.*    MAX31865 — Core 0 태스크, 뮤텍스 보호
WifiManager.*          AP+STA 동시 모드, DNS/mDNS, Preferences
WebServer.*            ESPAsyncWebServer 라우트 + 상태 JSON + 스테이징
web_assets.h          웹 UI HTML/CSS/JS (PROGMEM 임베드)
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
- **결선 모드**: `Cfg::RTD_WIRES`(2/3/4) — 보드 솔더점퍼와 일치 필수.
- **진단**: 부팅 시 threshold 레지스터 SPI 라운드트립 자가진단(칩·통신 검증) + fault 비트 디코드 + raw RTD 값 출력.
- **격리**: `Cfg::TEMP_SENSOR_PRESENT=false`면 init·폴링 태스크 미생성 → `temperature()=TEMP_UNREAD`, fault 없음(디스플레이 `--.-`). *현재 센서 보드 불량으로 격리됨 — 양품 장착 시 `true`로 복귀.*

### `WifiManager` — 네트워크
AP+STA 동시 모드. AP `http://192.168.4.1`, mDNS `http://europrocessor.local`, AP DNS(captive).
STA 자동 재연결 이벤트 처리. 설정은 Preferences `wifi` 네임스페이스에 영구 저장.

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
| POST·DELETE | `/api/saver/image` | 화면보호기 업로드(매직바이트로 JPEG/GIF/`ANM1` 판정 → `/saver.{jpg,gif,anim}`, 파일핸들 유지로 대용량 안정)/삭제. **모터 운전 중 차단**(업로드 거부·진행 중 중단, 삭제 409 — 플래시op 크래시 회피) |

`/api/status`의 JSON 키와 HTML은 마이그레이션 전후로 동일하여 프론트엔드는 변경 없이 동작합니다.

---

## 6. 빌드 & 플래시 (PlatformIO)

```bash
pio run                 # 빌드
pio run -t upload       # 펌웨어 업로드
pio device monitor      # 시리얼 모니터 (115200)
```

- `data/` 폴더가 없으므로 `uploadfs`는 불필요합니다(레시피·화면보호기는 런타임 생성).
- 시리얼 로그가 안 보이면 보드 **EN/RESET**을 한 번 누르세요(USB-CDC 재연결 타이밍).

### 빌드 플래그 (`platformio.ini`)
| 플래그 | 의미 |
|---|---|
| `UI_DISPLAY_PRESENT=1` | 디스플레이 결선 여부. `0`이면 `displayTask` 미생성(무디스플레이 변종) |
| `BOARD_HAS_PSRAM` + `memory_type=qio_opi` | N16R8 OPI PSRAM 활성 |
| `ARDUINO_USB_CDC_ON_BOOT=1` | 네이티브 USB 포트로 Serial 출력 |
| `CORE_DEBUG_LEVEL=0` | 코어 로그 억제(디버깅 시 3) |

`partitions.csv` — 3MB APP ×2 (OTA) + 9.9MB LittleFS (16MB Flash 기준).

---

## 7. 도구 (`tools/`)

`gen_nyan.py` — GIF → RGB565 프레임 헤더(`src/nyan_frames.h`) 생성 (임베드 Nyan 화면보호기). LANCZOS로 160×120 리사이즈, 프레임/딜레이 자동 추출. Pillow 필요: `python -m pip install Pillow`.

---

## 8. 안전 설계 (물리 손상 방지)

- **부팅 즉시 TMC2209 EN=HIGH** — 리셋~setup 구간의 floating으로 코일이 통전된 채 정지 → 발열/소손 방지. 권장 HW 풀업: EN→3.3V 10K.
- **부팅 즉시 panel 신호 핀 idle 고정** — 부팅 직후 floating noise가 패널 컨트롤러를 비정상 state로 몰아 ESD/latch-up 손상 누적 방지. 권장 HW 풀업: TFT CS·RST→3.3V 10K.
- **디스플레이 배선 주의** — 신호선을 5V(VBUS) 근처로 라우팅하지 말 것. 5V가 신호핀에 닿으면 내부 ESD 다이오드를 통해 VCC 레일로 역주입되어 컨트롤러/백라이트가 손상될 수 있음(실손상 사례 있음).
- **냉각** — 75RPM 연속 2h+ 시 모터·전자부 ≈50°C(실측). 발열은 I²R 지배 → VREF 최소 유지 정책.

---

## 9. 라이선스 / 작성

개인 프로젝트. 펌웨어는 Arduino-ESP32 + PlatformIO 환경에서 빌드합니다.
필요 라이브러리는 `platformio.ini`의 `lib_deps`에 명시되어 자동 설치됩니다.
