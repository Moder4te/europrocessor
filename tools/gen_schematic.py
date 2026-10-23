# -*- coding: utf-8 -*-
"""
gen_schematic.py — README용 전기 회로도(SVG) 생성
  docs/schematic_power.svg   : 전원 회로 (USB-PD 20V → 모터 / 5V → ESP32-S3 / 3V3)
  docs/schematic_signal.svg  : 신호 회로 (바디1 기본 배치 + 바디2 핀 차이)

핀 번호는 src/PinMap.h(DEFAULT_PINS, BOARDS[])와 맞춰 손으로 적은 값이다.
PinMap.h를 바꾸면 아래 PINS / BODY2 표도 같이 고치고 다시 실행:
  python tools/gen_schematic.py
표준 라이브러리만 사용.
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs")

FONT = "'Noto Sans KR','Malgun Gothic','Apple SD Gothic Neo','Segoe UI',sans-serif"
INK = "#1f2328"          # 선·글자
MUTED = "#57606a"
BOXF = "#f6f8fa"         # 부품 박스 채움
REC = "#d9480f"          # 권장/선택 부품 (미장착)
C20 = "#c92a2a"          # +20V
C5 = "#e67700"           # +5V
C33 = "#1971c2"          # 3V3
# 신호 그룹 색
G_TFT, G_ENC, G_MAX, G_TMC, G_AUX = "#1971c2", "#2f9e44", "#7048e8", "#c92a2a", "#868e96"


class Svg:
    def __init__(self, w, h):
        self.w, self.h, self.e = w, h, []

    def add(self, s):
        self.e.append(s)

    def text(self, x, y, s, size=13, anchor="start", color=INK, weight="normal", italic=False):
        st = ' font-style="italic"' if italic else ""
        s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        self.add(f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" fill="{color}" '
                 f'font-weight="{weight}"{st}>{s}</text>')

    def line(self, x1, y1, x2, y2, color=INK, w=1.6, dash=False):
        d = ' stroke-dasharray="5 3"' if dash else ""
        self.add(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="{w}"{d}/>')

    def path(self, d, color=INK, w=1.6, fill="none", dash=False):
        ds = ' stroke-dasharray="5 3"' if dash else ""
        self.add(f'<path d="{d}" stroke="{color}" stroke-width="{w}" fill="{fill}" '
                 f'stroke-linejoin="round" stroke-linecap="round"{ds}/>')

    def wire(self, pts, color=INK, w=1.8, dash=False):
        d = "M" + " L".join(f"{x},{y}" for x, y in pts)
        self.path(d, color, w, dash=dash)

    def rect(self, x, y, w, h, fill=BOXF, stroke=INK, rx=6, dash=False, sw=1.6):
        d = ' stroke-dasharray="6 4"' if dash else ""
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" '
                 f'stroke="{stroke}" stroke-width="{sw}"{d}/>')

    def dot(self, x, y, color=INK, r=3.5):
        self.add(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}"/>')

    def save(self, path, title):
        head = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
                f'viewBox="0 0 {self.w} {self.h}" font-family="{FONT}">\n'
                f'<title>{title}</title>\n'
                f'<rect width="100%" height="100%" fill="#ffffff"/>\n')
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(head + "\n".join(self.e) + "\n</svg>\n")
        print("wrote", os.path.relpath(path, ROOT))


# ── 기호 ────────────────────────────────────────────────────────────
def gnd(s, x, y, color=INK):
    """(x,y)에서 아래로 GND 기호"""
    s.line(x, y, x, y + 10, color)
    s.line(x - 9, y + 10, x + 9, y + 10, color, 2)
    s.line(x - 6, y + 14, x + 6, y + 14, color, 2)
    s.line(x - 3, y + 18, x + 3, y + 18, color, 2)


def res_h(s, x1, x2, y, color=INK, dash=False):
    """가로 저항 (x1~x2, 지그재그는 가운데 28px)"""
    m = (x1 + x2) / 2
    a, b = m - 14, m + 14
    s.line(x1, y, a, y, color, dash=dash)
    s.line(b, y, x2, y, color, dash=dash)
    pts = [(a, y)]
    for i in range(1, 7):
        pts.append((a + i * 4 - 2, y + (-5 if i % 2 else 5)))
    pts.append((b, y))
    s.path("M" + " L".join(f"{p[0]},{p[1]}" for p in pts), color, 1.6)


def res_v(s, x, y1, y2, color=INK, dash=False):
    m = (y1 + y2) / 2
    a, b = m - 14, m + 14
    s.line(x, y1, x, a, color, dash=dash)
    s.line(x, b, x, y2, color, dash=dash)
    pts = [(x, a)]
    for i in range(1, 7):
        pts.append((x + (-5 if i % 2 else 5), a + i * 4 - 2))
    pts.append((x, b))
    s.path("M" + " L".join(f"{p[0]},{p[1]}" for p in pts), color, 1.6)


def cap_v(s, x, y1, y2, color=INK, polar=False, dash=False):
    """세로 커패시터 (위 y1 → 아래 y2)"""
    m = (y1 + y2) / 2
    s.line(x, y1, x, m - 4, color, dash=dash)
    s.line(x - 12, m - 4, x + 12, m - 4, color, 2.4)
    if polar:
        s.path(f"M{x-12},{m+6} Q{x},{m+1} {x+12},{m+6}", color, 2.4)
        s.text(x - 20, m - 6, "+", 12, "middle", color)
    else:
        s.line(x - 12, m + 4, x + 12, m + 4, color, 2.4)
    s.line(x, m + (6 if polar else 4), x, y2, color, dash=dash)


def diode_h(s, x1, x2, y, color=INK):
    """가로 다이오드 (애노드 x1 → 캐소드 x2)"""
    m = (x1 + x2) / 2
    s.line(x1, y, m - 8, y, color)
    s.line(m + 8, y, x2, y, color)
    s.path(f"M{m-8},{y-8} L{m-8},{y+8} L{m+8},{y} Z", color, 1.6, fill=color)
    s.line(m + 8, y - 8, m + 8, y + 8, color, 2.2)


def fuse_h(s, x1, x2, y, color=INK):
    m = (x1 + x2) / 2
    s.line(x1, y, m - 16, y, color)
    s.line(m + 16, y, x2, y, color)
    s.rect(m - 16, y - 6, 32, 12, fill="#fff", stroke=color, rx=2, dash=True)
    s.line(m - 16, y, m + 16, y, color, 1.2)


def switch_h(s, x1, x2, y, color=INK):
    m = (x1 + x2) / 2
    s.line(x1, y, m - 14, y, color)
    s.line(m + 14, y, x2, y, color)
    s.dot(m - 14, y, color, 3)
    s.dot(m + 14, y, color, 3)
    s.line(m - 14, y, m + 12, y - 12, color, 1.8)


def button_v(s, x, y1, y2, color=INK):
    """세로 푸시버튼 (y1 → y2)"""
    m = (y1 + y2) / 2
    s.line(x, y1, x, m - 10, color)
    s.line(x, m + 10, x, y2, color)
    s.dot(x, m - 10, color, 2.5)
    s.dot(x, m + 10, color, 2.5)
    s.line(x - 14, m - 10, x - 14, m + 10, color, 2)
    s.line(x - 14, m, x - 22, m, color, 1.6)
    s.line(x - 22, m - 6, x - 22, m + 6, color, 1.6)


def netlabel(s, x, y, name, color, anchor="start"):
    """네트 이름 꼬리표 (깃발 모양)"""
    w = 9 + len(name) * 7.4
    if anchor == "start":
        s.path(f"M{x},{y} L{x+8},{y-9} L{x+8+w},{y-9} L{x+8+w},{y+9} L{x+8},{y+9} Z", color, 1.4, fill="#fff")
        s.text(x + 8 + w / 2, y + 4.5, name, 12, "middle", color, "bold")
    else:
        s.path(f"M{x},{y} L{x-8},{y-9} L{x-8-w},{y-9} L{x-8-w},{y+9} L{x-8},{y+9} Z", color, 1.4, fill="#fff")
        s.text(x - 8 - w / 2, y + 4.5, name, 12, "middle", color, "bold")


def tag_pull(s, x, y, label, kind="up", dash=True, left=False):
    """신호선 위 (x,y)에 붙는 권장 부품 꼬리표: 위로 올라가 가로 저항/커패시터 → 라벨
    left=True면 저항이 왼쪽으로 뻗고 라벨도 왼쪽 (박스 바로 앞 짧은 배선용)"""
    c = REC
    s.dot(x, y, c, 3)
    s.line(x, y, x, y - 12, c, 1.4, dash=dash)
    if left:
        res_h(s, x - 40, x, y - 12, c)
        s.text(x - 44, y - 8, label, 11, "end", c, "bold")
        return
    if kind == "cap":
        s.line(x, y - 12, x + 10, y - 12, c, 1.4)
        s.line(x + 10, y - 19, x + 10, y - 5, c, 2)
        s.line(x + 15, y - 19, x + 15, y - 5, c, 2)
        s.line(x + 15, y - 12, x + 24, y - 12, c, 1.4)
        s.text(x + 28, y - 8, label, 11, "start", c, "bold")
    else:
        res_h(s, x, x + 40, y - 12, c)
        s.text(x + 44, y - 8, label, 11, "start", c, "bold")


def legend(s, x, y):
    s.line(x, y, x + 34, y, INK, 2)
    s.text(x + 42, y + 4, "현재 장착", 12, color=INK)
    s.line(x + 120, y, x + 154, y, REC, 2, dash=True)
    s.text(x + 162, y + 4, "권장·선택 (미장착, ISSUES.md 2-A)", 12, color=REC)


# ══════════════════════════════════════════════════════════════════
# 1. 전원 회로
# ══════════════════════════════════════════════════════════════════
def power():
    s = Svg(1140, 640)
    s.text(30, 38, "europrocessor 전원 회로", 22, weight="bold")
    s.text(30, 62, "USB-PD 20V 단일 입력 → 모터 직결 + 벅 5V → ESP32-S3 → 온보드 LDO 3V3", 13, color=MUTED)
    legend(s, 690, 40)

    Y = 160   # 20V 주 레일 높이
    # J1 충전기
    s.rect(30, Y - 40, 150, 80)
    s.text(105, Y - 12, "J1 USB-C PD 충전기", 13, "middle", weight="bold")
    s.text(105, Y + 8, "100W (20V / 5A)", 12, "middle", color=MUTED)
    s.text(105, Y + 26, "1.5A 이상이면 충분", 11, "middle", color=MUTED)
    s.wire([(180, Y), (230, Y)], INK, 3)
    s.text(205, Y - 8, "USB-C", 11, "middle", color=MUTED)
    # U1 CH224K
    s.rect(230, Y - 40, 150, 80)
    s.text(305, Y - 12, "U1 CH224K", 13, "middle", weight="bold")
    s.text(305, Y + 8, "USB-PD 트리거(디코이)", 12, "middle", color=MUTED)
    s.text(305, Y + 26, "CFG 설정 = 20V 고정", 11, "middle", color=MUTED)
    gnd(s, 305, Y + 40)
    # F1 퓨즈 (권장)
    s.line(380, Y, 420, Y, C20, 2.2)
    fuse_h(s, 420, 490, Y, REC)
    s.text(455, Y - 16, "F1 퓨즈 1.5~2A", 11, "middle", REC, "bold")
    s.text(455, Y + 24, "(권장 H4)", 11, "middle", REC)
    s.line(490, Y, 560, Y, C20, 2.2)
    s.dot(560, Y, C20)
    netlabel(s, 520, Y - 30, "+20V", C20)
    s.line(520, Y - 30, 520, Y, C20, 1.2)
    s.dot(520, Y, C20)

    # 모터 가지: SW1 → VM
    s.line(560, Y, 600, Y, C20, 2.2)
    switch_h(s, 600, 670, Y, REC)
    s.text(635, Y - 22, "SW1 모터 전원 차단", 11, "middle", REC, "bold")
    s.text(635, Y + 24, "(선택·비상용)", 11, "middle", REC)
    s.line(670, Y, 880, Y, C20, 2.2)
    s.dot(820, Y, C20)
    cap_v(s, 820, Y, Y + 70, INK, polar=True)
    gnd(s, 820, Y + 70)
    s.text(784, Y + 30, "C1 470µF", 12, "end", weight="bold")
    s.text(784, Y + 46, "35V급", 11, "end", color=MUTED)
    s.text(784, Y + 62, "VM 핀 가까이", 11, "end", color=MUTED)
    # TMC2209 박스
    s.rect(880, Y - 60, 230, 170)
    s.text(995, Y - 36, "U4 TMC2209", 14, "middle", weight="bold")
    s.text(995, Y - 18, "스탠드얼론 · StealthChop", 11, "middle", color=MUTED)
    s.text(890, Y + 4, "VM", 12, weight="bold", color=C20)
    s.text(1100, Y + 4, "1A·1B·2A·2B → 모터", 11, "end", color=MUTED)
    s.text(890, Y + 28, "VIO ← 3V3", 12, weight="bold", color=C33)
    s.text(890, Y + 50, "VREF 0.7V ≈ 0.5A/상", 11, color=MUTED)
    s.text(890, Y + 68, "MS1·MS2 = LOW → 1/8 스텝", 11, color=MUTED)
    s.text(890, Y + 96, "GND", 12, weight="bold")
    s.line(905, Y + 110, 905, Y + 116)
    gnd(s, 905, Y + 116)
    s.text(995, Y + 150, "모터: NEMA17 + 3.71:1 유성기어", 11, "middle", color=MUTED)

    # 벅 가지
    Y2 = 400
    s.line(560, Y, 560, Y2, C20, 2.2)
    s.line(560, Y2, 600, Y2, C20, 2.2)
    s.text(580, Y2 - 8, "IN", 10, "middle", color=C20, weight="bold")
    s.rect(600, Y2 - 50, 210, 100)
    s.text(705, Y2 - 24, "U2 벅 컨버터 20V→5V", 13, "middle", weight="bold")
    s.text(705, Y2 - 4, "현재 MP1584EN (입력 최대 26V)", 11, "middle", color=MUTED)
    s.text(705, Y2 + 14, "교체 권장: 36V급 XL4015 /", 11, "middle", color=REC)
    s.text(705, Y2 + 30, "LM2596HV (발열·마진)", 11, "middle", color=REC)
    gnd(s, 705, Y2 + 50)
    s.line(810, Y2, 880, Y2, C5, 2.2)
    s.text(845, Y2 - 8, "+5V", 12, "middle", color=C5, weight="bold")
    diode_h(s, 880, 940, Y2, REC)
    s.text(910, Y2 - 16, "D1 SS34 (권장 H5)", 11, "middle", REC, "bold")
    s.text(910, Y2 + 24, "USB 5V 역류 차단", 11, "middle", REC)
    s.line(940, Y2, 970, Y2, C5, 2.2)
    # ESP32 박스
    s.rect(970, Y2 - 60, 140, 170)
    s.text(1040, Y2 - 36, "U3 ESP32-S3", 13, "middle", weight="bold")
    s.text(1040, Y2 - 18, "DevKitC-1", 11, "middle", color=MUTED)
    s.text(978, Y2 + 4, "5V (5Vin)", 12, weight="bold", color=C5)
    s.text(978, Y2 + 28, "온보드 LDO", 11, color=MUTED)
    s.text(978, Y2 + 52, "3V3 출력", 12, weight="bold", color=C33)
    s.text(978, Y2 + 76, "GND", 12, weight="bold")
    s.text(1040, Y2 + 100, "USB-C(로그·초기 플래시)", 10, "middle", color=MUTED)
    s.line(1090, Y2 + 110, 1090, Y2 + 116)
    gnd(s, 1090, Y2 + 116)

    # 3V3 부하 목록
    s.text(600, Y2 + 104, "3V3 출력 → TMC2209 VIO · MAX31865 VIN · ST7789 VCC · 풀업 저항", 11, color=C33, weight="bold")
    s.text(600, Y2 + 122, "(DevKitC 온보드 LDO 사용 — 별도 3.3V 레귤레이터 없음)", 11, color=MUTED)

    # VM 전압 감시 분압 (선택)
    X = 120
    YV = 330
    s.text(30, YV - 26, "VM 전압 감시 (선택 H3)", 13, color=REC, weight="bold")
    netlabel(s, X, YV, "+20V", C20, "end")
    s.line(X, YV, X + 20, YV, REC, 1.6, dash=True)
    res_v(s, X + 20, YV, YV + 70, REC)
    s.text(X + 32, YV + 40, "R1 100kΩ", 11, color=REC)
    s.dot(X + 20, YV + 70, REC)
    s.line(X + 20, YV + 70, X + 160, YV + 70, REC, 1.6, dash=True)
    netlabel(s, X + 160, YV + 70, "GPIO1 (ADC1)", REC)
    res_v(s, X + 20, YV + 70, YV + 140, REC)
    s.text(X + 32, YV + 110, "R2 10kΩ", 11, color=REC)
    s.dot(X + 110, YV + 70, REC)
    cap_v(s, X + 110, YV + 70, YV + 140, REC)
    s.text(X + 124, YV + 112, "C3 100nF", 11, color=REC)
    s.line(X + 20, YV + 140, X + 110, YV + 140, REC, 1.6)
    gnd(s, X + 65, YV + 140, REC)
    s.text(30, YV + 186, "분압비 11 → PinMap.h vmPin=1, vmRatio=11.0", 11, color=MUTED)
    s.text(30, YV + 202, "구동 중 VM<10.5V 0.5초 → 정지, VM>28V → 즉시 차단", 11, color=MUTED)

    # 주의 박스
    s.rect(30, 560, 1080, 64, fill="#fff8f0", stroke=REC, rx=8)
    s.text(46, 584, "⚠ USB만 꽂은 상태로 모터를 돌리지 말 것 (D1 장착 전엔 USB 5V가 벅을 거꾸로 거쳐 20V 라인으로 새어 모터가 약하게 돎)", 12, color=INK)
    s.text(46, 606, "⚠ 전원 켠 채 모터 커넥터 탈착 금지 · TMC2209 VIO는 반드시 3.3V (5V면 ESP32 GPIO 손상) · 신호선을 5V 근처로 배선하지 말 것", 12, color=INK)
    s.save(os.path.join(OUT, "schematic_power.svg"), "europrocessor 전원 회로")


# ══════════════════════════════════════════════════════════════════
# 2. 신호 회로 (바디1 기본 배치)
# ══════════════════════════════════════════════════════════════════
STEP = 34


def rows(start, groups, gaps):
    """그룹마다 간격(gaps)을 두고 y 좌표 목록 생성"""
    out, y = [], start
    for i, g in enumerate(groups):
        for _ in range(g):
            out.append(y)
            y += STEP
        if i < len(gaps):
            y += gaps[i]
    return out


def signal():
    s = Svg(1220, 960)
    s.text(30, 38, "europrocessor 신호 회로 — 바디1 기본 배치 (ESP32-S3 N16R8)", 22, weight="bold")
    s.text(30, 62, "핀은 src/PinMap.h DEFAULT_PINS 기준 · 부팅 시 칩 MAC으로 보드별 배치 자동 선택", 13, color=MUTED)
    legend(s, 760, 40)

    EX, EW = 470, 260            # ESP 박스
    PL, PR = EX - 24, EX + EW + 24   # 핀 끝
    # 왼쪽(A변): TFT 6 / EC11 3 / KO 1
    L = rows(150, [6, 3, 1], [64, 64])
    # 오른쪽(B변): MAX 4 / TMC 4 / 기타 3
    R = rows(150, [4, 4, 3], [64, 90])
    top, bot = 100, max(L[-1], R[-1]) + 60
    s.rect(EX, top, EW, bot - top, fill="#eef4ff")
    s.text(EX + EW / 2, top + 22, "ESP32-S3-DevKitC-1", 15, "middle", weight="bold")
    s.text(EX + EW / 2, top + 40, "A변 (3V3 쪽)   |   B변 (5Vin 쪽)", 11, "middle", color=MUTED)

    def lpin(y, gpio, color):
        s.line(PL, y, EX, y, color, 1.8)
        s.text(EX + 8, y + 4, gpio, 12, color=color, weight="bold")

    def rpin(y, gpio, color, dash=False):
        s.line(EX + EW, y, PR, y, color, 1.8, dash=dash)
        s.text(EX + EW - 8, y + 4, gpio, 12, "end", color=color, weight="bold")

    # ── 왼쪽: ST7789 ──
    tft = [("SCL", "GPIO12"), ("SDA", "GPIO11"), ("CS", "GPIO10"), ("DC", "GPIO9"), ("RES", "GPIO8"), ("BLK", "GPIO18")]
    BX, BW = 120, 150
    s.rect(BX, L[0] - 48, BW, L[5] - L[0] + 84)
    s.text(BX + BW / 2, L[0] - 28, "ST7789 TFT 320×240", 12, "middle", weight="bold")
    s.text(BX + BW / 2, L[0] - 14, "하드웨어 SPI (FSPI)", 10, "middle", color=MUTED)
    for (pn, g), y in zip(tft, L[0:6]):
        s.text(BX + BW - 8, y + 4, pn, 12, "end", G_TFT, "bold")
        s.line(BX + BW, y, PL, y, G_TFT, 1.8)
        lpin(y, g, G_TFT)
    s.text(BX + 8, L[5] + 26, "VCC ← 3V3 · GND", 11, color=C33)
    tag_pull(s, 300, L[2], "10k→3V3")
    tag_pull(s, 300, L[4], "10k→3V3")

    # ── 왼쪽: EC11 ──
    enc = [("A", "GPIO17"), ("B", "GPIO16"), ("SW", "GPIO15")]
    s.rect(BX, L[6] - 30, BW, L[8] - L[6] + 60)
    s.text(BX + 10, L[6] - 12, "EC11", 12, weight="bold")
    s.text(BX + 10, L[6] + 4, "로터리", 10, color=MUTED)
    s.text(BX + 10, L[6] + 18, "인코더", 10, color=MUTED)
    for (pn, g), y in zip(enc, L[6:9]):
        s.text(BX + BW - 8, y + 4, pn, 12, "end", G_ENC, "bold")
        s.line(BX + BW, y, PL, y, G_ENC, 1.8)
        lpin(y, g, G_ENC)
    s.text(BX + 10, L[6] + 34, "(내부 풀업)", 10, color=MUTED)
    s.text(BX + 10, L[8] + 22, "C·SW2 → GND", 10, color=MUTED)
    tag_pull(s, 300, L[6], "10nF→GND", "cap")
    tag_pull(s, 300, L[7], "10nF→GND", "cap")

    # ── 왼쪽: KO 버튼 ──
    y = L[9]
    s.line(BX + BW, y, PL, y, G_ENC, 1.8)
    lpin(y, "GPIO7", G_ENC)
    s.line(BX + BW, y, BX + BW - 30, y, G_ENC, 1.8)
    button_v(s, BX + BW - 30, y, y + 50, INK)
    gnd(s, BX + BW - 30, y + 50)
    s.text(BX + BW - 64, y + 4, "KO 버튼", 12, "end", weight="bold")
    s.text(BX + BW - 64, y + 20, "(확인/뒤로)", 10, "end", color=MUTED)

    # 왼쪽 전원 핀
    s.text(EX + 8, bot - 34, "3V3 · GND", 12, color=C33, weight="bold")

    # ── 오른쪽: MAX31865 ──
    MX, MW = 880, 150
    maxp = [("CS", "GPIO13"), ("SDI", "GPIO14"), ("SDO", "GPIO21"), ("CLK", "GPIO47")]
    s.rect(MX, R[0] - 48, MW, R[3] - R[0] + 84)
    s.text(MX + MW / 2, R[0] - 28, "MAX31865", 12, "middle", weight="bold")
    s.text(MX + MW / 2, R[0] - 14, "소프트웨어 SPI", 10, "middle", color=MUTED)
    for (pn, g), y in zip(maxp, R[0:4]):
        s.text(MX + 8, y + 4, pn, 12, color=G_MAX, weight="bold")
        s.line(PR, y, MX, y, G_MAX, 1.8)
        rpin(y, g, G_MAX)
    s.text(MX + 8, R[3] + 26, "VIN ← 3V3 · GND", 11, color=C33)
    # RTD 단자 + PT100
    rt = [("F+", R[0]), ("RTD+", R[1]), ("RTD−", R[2]), ("F−", R[3])]
    for pn, y in rt:
        s.text(MX + MW - 8, y + 4, pn, 11, "end", G_MAX, "bold")
    sx = MX + MW
    s.line(sx, R[0], sx + 40, R[0], "#c92a2a", 2)      # 짝선(빨강) → F+
    s.line(sx, R[1], sx + 40, R[1], "#c92a2a", 2)      # 짝선(빨강) → RTD+
    s.line(sx + 40, R[0], sx + 40, R[1], "#c92a2a", 2)
    s.line(sx, R[2], sx + 60, R[2], "#495057", 2)      # 단일선 → RTD−
    s.line(sx + 40, (R[0] + R[1]) / 2, sx + 60, (R[0] + R[1]) / 2, "#c92a2a", 2)
    res_v(s, sx + 60, (R[0] + R[1]) / 2, R[2], INK)
    s.text(sx + 72, (R[1] + R[2]) / 2 - 10, "PT100", 12, weight="bold")
    s.text(sx + 72, (R[1] + R[2]) / 2 + 6, "3선식", 10, color=MUTED)
    s.text(sx + 8, R[3] + 4, "RREF 412Ω · 2/3/4선 점퍼", 10, color=MUTED)
    s.text(sx + 8, R[3] + 20, "실측 결선(F8): 짝선→F+·RTD+,", 10, color=MUTED)
    s.text(sx + 8, R[3] + 34, "단일선→RTD−", 10, color=MUTED)

    # ── 오른쪽: TMC2209 ──
    TX_, TW = 880, 150
    tmc = [("EN", "GPIO40"), ("DIR", "GPIO41"), ("STEP", "GPIO42"), ("DIAG", "GPIO38")]
    s.rect(TX_, R[4] - 48, TW, R[7] - R[4] + 96)
    s.text(TX_ + TW / 2, R[4] - 28, "TMC2209", 12, "middle", weight="bold")
    s.text(TX_ + TW / 2, R[4] - 14, "EN LOW=코일 ON", 10, "middle", color=MUTED)
    for i, ((pn, g), y) in enumerate(zip(tmc, R[4:8])):
        dash = pn == "DIAG"
        col = REC if dash else G_TMC
        s.text(TX_ + 8, y + 4, pn, 12, color=col, weight="bold")
        s.line(PR, y, TX_, y, col, 1.8, dash=dash)
        rpin(y, g if not dash else "GPIO38*", col, dash)
    s.text(TX_ + 8, R[7] + 28, "VIO ← 3V3", 11, color=C33)
    s.text(TX_ + 8, R[7] + 42, "VM ← +20V (전원 회로)", 11, color=C20)
    tag_pull(s, 856, R[4], "10k→3V3", left=True)
    tag_pull(s, 856, R[7], "10k→GND", left=True)
    # 모터 출력 → NEMA17
    mo = TX_ + TW
    for pn, y in zip(["1A", "1B", "2A", "2B"], R[4:8]):
        s.text(mo - 8, y + 4, pn, 11, "end", G_TMC, "bold")
        s.line(mo, y, mo + 40, y, G_TMC, 1.8)
    cy = (R[4] + R[7]) / 2
    s.add(f'<circle cx="{mo+75}" cy="{cy}" r="34" fill="#fff" stroke="{INK}" stroke-width="1.8"/>')
    s.text(mo + 75, cy + 7, "M", 20, "middle", weight="bold")
    for y in R[4:8]:
        s.line(mo + 40, y, mo + 48, cy + (y - cy) * 0.55, G_TMC, 1.8)
    s.text(mo + 75, cy + 54, "NEMA17", 11, "middle", weight="bold")
    s.text(mo + 75, cy + 68, "+3.71:1 기어", 10, "middle", color=MUTED)

    # ── 오른쪽: 기타 (VM ADC, UART0 로그) ──
    aux = [("GPIO1*", "VM 분압 (ADC1, 선택)", REC, True),
           ("GPIO43 TX", "→ USB-TTL RX (크래시 로그)", G_AUX, False),
           ("GPIO44 RX", "← USB-TTL TX (GND 공통, VCC 연결 금지)", G_AUX, False)]
    for (g, d, c, dash), y in zip(aux, R[8:11]):
        rpin(y, g, c, dash)
        s.line(PR, y, PR + 40, y, c, 1.8, dash=dash)
        s.text(PR + 48, y + 4, d, 11, color=c)
    s.text(EX + EW - 8, bot - 34, "5V · GND", 12, "end", color=C5, weight="bold")

    # ── 범례/메모 ──
    YB = bot + 30
    s.rect(30, YB, 540, 170, fill="#fff", stroke="#d0d7de", rx=8)
    s.text(46, YB + 26, "바디2 (N8R2 · 무디스플레이) 핀 — PinMap.h BOARDS[]", 13, weight="bold")
    b2 = [("TMC2209", "STEP 5 · DIR 6 · EN 7 · DIAG 권장 GPIO15"),
          ("MAX31865", "CS 2 · SDI 38 · SDO 39 · CLK 40 (펌웨어 2선 모드)"),
          ("화면·인코더·KO", "없음 — 해당 핀은 구동하지 않음 (hasDisplay=false)")]
    for i, (a, b) in enumerate(b2):
        s.text(46, YB + 54 + i * 24, a, 12, weight="bold")
        s.text(170, YB + 54 + i * 24, b, 12)
    s.text(46, YB + 130, "바디1의 MAX31865는 보드 불량으로 격리 (rtdWires=0, 화면 --.-)", 11, color=MUTED)
    s.text(46, YB + 152, "* 표시 = 권장 배선 핀 (배선 후 PinMap.h diagPin/vmPin에 등록해야 동작)", 11, color=REC)

    s.rect(590, YB, 580, 170, fill="#fff", stroke="#d0d7de", rx=8)
    s.text(606, YB + 26, "메모", 13, weight="bold")
    memo = ["금지 GPIO: 0·3·45·46(스트래핑) 19·20(USB) 26~37(Flash/PSRAM) 43·44 48",
            "→ PinMap.h static_assert가 빌드 단계에서 차단",
            "인코더 A/B는 GPIO 0~31만 (ISR이 GPIO_IN_REG 직접 읽음)",
            "EN 10k 풀업(H1): 부팅·리셋 중 코일 통전 방지 — 가장 먼저 권장",
            "DIAG(H2): 10k 풀다운, VIO 3.3V 확인 후 연결",
            "TFT CS·RES 풀업(H9), 인코더 10nF(H8): 노이즈 대책"]
    for i, m in enumerate(memo):
        s.text(606, YB + 52 + i * 21, m, 11.5, color=INK if i != 1 else MUTED)
    s.h = YB + 190
    s.save(os.path.join(OUT, "schematic_signal.svg"), "europrocessor 신호 회로")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    power()
    signal()
