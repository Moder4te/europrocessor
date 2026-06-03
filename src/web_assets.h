// ================================================================
//  web_assets.h — 웹 UI (HTML/CSS/JS) PROGMEM 임베드
//  rotary_processor.ino 의 INDEX_HTML 원문을 그대로 이관 (무수정).
// ================================================================
#pragma once
#include <Arduino.h>
#include <pgmspace.h>

const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="mobile-web-app-capable" content="yes">
<title>Film Processor</title>
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0;}
:root{
  --bg:#F5F3EF;--surface:#FFF;--border:#E8E4DC;
  --accent:#D97706;--accent-lt:#FEF3C7;
  --text:#1C1917;--muted:#78716C;
  --danger:#DC2626;--success:#16A34A;--info:#2563EB;
  --radius:12px;--shadow:0 1px 3px rgba(0,0,0,.10),0 1px 2px rgba(0,0,0,.06);
  --sat:env(safe-area-inset-top,0px);--sab:env(safe-area-inset-bottom,0px);
  --sal:env(safe-area-inset-left,0px);--sar:env(safe-area-inset-right,0px);
  --nav-h:56px;
}
html{height:100%;}
body{height:100dvh;overflow:hidden;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:var(--bg);color:var(--text);}
#app{display:flex;flex-direction:column;height:100dvh;}
.bnav{display:flex;position:fixed;bottom:0;left:0;right:0;height:calc(var(--nav-h) + var(--sab));padding-bottom:var(--sab);padding-left:var(--sal);padding-right:var(--sar);background:var(--surface);border-top:1px solid var(--border);z-index:100;}
.bnav-btn{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:2px;border:none;background:none;cursor:pointer;color:var(--muted);font-size:10px;font-weight:600;transition:color .2s;-webkit-tap-highlight-color:transparent;padding-top:6px;}
.bnav-btn.active{color:var(--accent);}
.bnav-icon{font-size:20px;line-height:1;}
.page{flex:1;overflow-y:auto;padding:calc(12px + var(--sat)) 14px calc(var(--nav-h) + var(--sab) + 12px);display:none;}
.page.active{display:block;}
.card{background:var(--surface);border-radius:var(--radius);padding:14px;margin-bottom:10px;box-shadow:var(--shadow);border:1px solid var(--border);}
.card-title{font-size:10px;font-weight:700;color:var(--muted);text-transform:uppercase;letter-spacing:.08em;margin-bottom:10px;}
.cat-tabs{display:flex;gap:6px;margin-bottom:10px;flex-wrap:wrap;}
.cat-tab{padding:5px 13px;border-radius:20px;border:1.5px solid var(--border);background:var(--surface);cursor:pointer;font-size:13px;font-weight:600;color:var(--muted);transition:all .15s;}
.cat-tab.active{background:var(--accent);border-color:var(--accent);color:#fff;}
.recipe-list{list-style:none;}
.recipe-item{display:flex;align-items:center;padding:10px 8px;border-radius:8px;cursor:pointer;border:1.5px solid transparent;gap:8px;transition:background .1s;}
.recipe-item.selected{background:var(--accent-lt);border-color:var(--accent);}
.recipe-item-name{flex:1;font-weight:500;font-size:14px;}
.recipe-item-info{font-size:11px;color:var(--muted);}
.step-card{background:var(--bg);border-radius:8px;padding:11px;margin-bottom:7px;border:1px solid var(--border);}
.step-hdr{display:flex;align-items:center;gap:7px;margin-bottom:9px;}
.step-num{width:24px;height:24px;border-radius:50%;background:var(--accent);color:#fff;font-size:11px;font-weight:700;display:flex;align-items:center;justify-content:center;flex-shrink:0;}
.step-name-in{flex:1;border:1px solid var(--border);border-radius:6px;padding:6px 9px;font-size:13px;background:var(--surface);}
.step-params{display:grid;grid-template-columns:1fr 1fr 1fr;gap:7px;}
.sp label{display:block;font-size:9px;color:var(--muted);margin-bottom:3px;font-weight:700;text-transform:uppercase;}
.sp input{width:100%;border:1px solid var(--border);border-radius:6px;padding:6px 5px;font-size:14px;text-align:center;background:var(--surface);font-weight:600;}
.btn{padding:8px 14px;border-radius:8px;border:none;cursor:pointer;font-size:13px;font-weight:600;transition:all .15s;display:inline-flex;align-items:center;justify-content:center;gap:5px;-webkit-tap-highlight-color:transparent;}
.btn:active{transform:scale(.96);}
.btn-primary{background:var(--accent);color:#fff;}
.btn-danger{background:var(--danger);color:#fff;}
.btn-success{background:var(--success);color:#fff;}
.btn-info{background:var(--info);color:#fff;}
.btn-outline{background:none;border:1.5px solid var(--border);color:var(--text);}
.btn-sm{padding:4px 10px;font-size:11px;}
.btn-icon{padding:5px 7px;}
.btn-lg{width:100%;padding:15px;font-size:16px;border-radius:12px;}
.motor-row{display:flex;align-items:center;justify-content:space-between;padding:12px;background:var(--bg);border-radius:10px;margin-bottom:8px;}
.motor-pct{font-size:38px;font-weight:800;line-height:1;}
.motor-pct span{font-size:18px;font-weight:400;}
.badge{padding:4px 12px;border-radius:20px;font-size:11px;font-weight:700;}
.b-idle{background:#E5E7EB;color:#6B7280;}
.b-fwd{background:#D1FAE5;color:#065F46;}
.b-rev{background:#DBEAFE;color:#1E40AF;}
.b-rest{background:#FEF3C7;color:#92400E;}
.pwr-bar{height:7px;background:var(--border);border-radius:4px;overflow:hidden;}
.pwr-fill{height:100%;background:var(--accent);border-radius:4px;transition:width .3s;}
.timer-lbl{text-align:center;font-size:11px;color:var(--muted);margin-bottom:1px;}
.timer-big{text-align:center;font-size:64px;font-weight:800;font-variant-numeric:tabular-nums;letter-spacing:-3px;line-height:1.05;}
.timeline{display:flex;height:34px;border-radius:8px;overflow:hidden;gap:2px;}
.t-seg{display:flex;align-items:center;justify-content:center;font-size:9px;font-weight:700;color:rgba(255,255,255,.9);overflow:hidden;white-space:nowrap;padding:0 3px;transition:opacity .3s;min-width:6px;}
.t-seg.done{opacity:.3;}.t-seg.current{box-shadow:inset 0 0 0 2px rgba(0,0,0,.3);}.t-seg.pending{opacity:.6;}
.sc0{background:#D97706;}.sc1{background:#2563EB;}.sc2{background:#16A34A;}.sc3{background:#9333EA;}.sc4{background:#DC2626;}.sc5{background:#0891B2;}.sc6{background:#EA580C;}.sc7{background:#65A30D;}
.ctrl-bar{display:flex;gap:7px;}
.ctrl-bar .btn{flex:1;padding:13px 6px;font-size:13px;}
.sw-disp{text-align:center;font-size:36px;font-weight:600;font-variant-numeric:tabular-nums;letter-spacing:-1px;color:var(--muted);}
.sw-row{display:flex;gap:7px;justify-content:center;margin-top:8px;}
.pwr-slider-val{text-align:center;font-size:52px;font-weight:800;font-variant-numeric:tabular-nums;letter-spacing:-2px;color:var(--accent);line-height:1;margin:6px 0;}
.pwr-slider-val span{font-size:24px;font-weight:400;color:var(--muted);}
input[type=range]{-webkit-appearance:none;width:100%;height:6px;border-radius:3px;background:var(--border);outline:none;margin:10px 0;}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:28px;height:28px;border-radius:50%;background:var(--accent);cursor:pointer;box-shadow:0 2px 6px rgba(0,0,0,.25);}
.manual-btns{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;}
.manual-btns .btn{padding:16px 8px;font-size:14px;flex-direction:column;gap:3px;}
.toggle-row{display:flex;align-items:center;justify-content:space-between;padding:2px 0;}
.toggle{position:relative;width:48px;height:26px;flex-shrink:0;}
.toggle input{opacity:0;width:0;height:0;}
.slider-sw{position:absolute;inset:0;background:var(--border);border-radius:13px;cursor:pointer;transition:background .2s;}
.slider-sw::before{content:'';position:absolute;width:20px;height:20px;background:#fff;border-radius:50%;left:3px;top:3px;transition:transform .2s;box-shadow:0 1px 3px rgba(0,0,0,.3);}
.toggle input:checked+.slider-sw{background:var(--accent);}
.toggle input:checked+.slider-sw::before{transform:translateX(22px);}
.manual-live{display:flex;align-items:center;justify-content:center;gap:10px;padding:10px;background:var(--bg);border-radius:8px;}
.form-grp{margin-bottom:12px;}
.form-lbl{display:block;font-size:11px;font-weight:700;color:var(--muted);margin-bottom:5px;text-transform:uppercase;letter-spacing:.05em;}
.form-in{width:100%;padding:10px 13px;border-radius:8px;border:1.5px solid var(--border);font-size:15px;background:var(--surface);transition:border-color .2s;}
.form-in:focus{outline:none;border-color:var(--accent);}
.sect-lbl{font-size:11px;font-weight:700;color:var(--muted);text-transform:uppercase;letter-spacing:.06em;margin:14px 0 8px;padding-bottom:4px;border-bottom:1px solid var(--border);display:block;}
.chip{display:inline-flex;align-items:center;gap:4px;padding:3px 10px;border-radius:20px;font-size:11px;font-weight:700;}
.chip-ok{background:#D1FAE5;color:#065F46;}.chip-off{background:#E5E7EB;color:#6B7280;}
.info-row{display:flex;justify-content:space-between;padding:7px 0;border-bottom:1px solid var(--border);font-size:13px;}
.info-row:last-child{border-bottom:none;}
.info-val{font-weight:600;font-size:13px;}
.empty-state{text-align:center;padding:36px 16px;color:var(--muted);}
.empty-icon{font-size:44px;margin-bottom:6px;}
.row-act{display:flex;gap:7px;flex-wrap:wrap;margin-top:10px;}
h2{font-size:19px;font-weight:800;margin-bottom:12px;}
.temp-big{font-size:38px;font-weight:800;line-height:1;}
.temp-big span{font-size:18px;font-weight:400;}
.temp-row{display:flex;align-items:center;justify-content:space-between;padding:12px;background:var(--bg);border-radius:10px;}
.confirm-overlay{position:fixed;inset:0;z-index:300;background:rgba(0,0,0,.65);display:none;align-items:center;justify-content:center;padding:24px var(--sal) max(24px,var(--sab)) var(--sar);}
.confirm-overlay.show{display:flex;}
.confirm-card{background:var(--surface);border-radius:20px;padding:32px 24px 28px;text-align:center;max-width:360px;width:100%;animation:popIn .2s cubic-bezier(.34,1.56,.64,1);}
@keyframes popIn{from{transform:scale(.85);opacity:0;}to{transform:scale(1);opacity:1;}}
.co-icon{font-size:56px;margin-bottom:10px;}
.co-done{font-size:17px;font-weight:800;margin-bottom:6px;}
.co-msg{font-size:13px;color:var(--muted);margin-bottom:4px;line-height:1.5;}
.co-next{display:inline-block;background:var(--accent-lt);color:var(--accent);padding:5px 14px;border-radius:20px;font-size:13px;font-weight:700;margin:10px 0 16px;}
</style>
</head>
<body>
<div id="app">

<div id="page-recipe" class="page active">
  <h2 data-i18n="pg_recipe"></h2>
  <div class="cat-tabs" id="cat-tabs"></div>
  <div class="card">
    <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:10px;">
      <span class="card-title" data-i18n="recipes"></span>
      <button class="btn btn-primary btn-sm" onclick="addRecipe()" data-i18n="add_recipe"></button>
    </div>
    <ul class="recipe-list" id="recipe-list"></ul>
    <div id="recipe-empty" class="empty-state" style="display:none;">
      <div class="empty-icon">📷</div><div data-i18n="no_recipes"></div>
    </div>
  </div>
  <div class="card" id="step-editor" style="display:none;">
    <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:10px;">
      <span class="card-title" id="step-editor-title"></span>
      <button class="btn btn-danger btn-sm" onclick="deleteSelectedRecipe()" data-i18n="del_recipe"></button>
    </div>
    <div id="step-list"></div>
    <div class="row-act">
      <button class="btn btn-outline btn-sm" onclick="addStep()" data-i18n="add_step"></button>
      <button class="btn btn-success" onclick="runSelectedRecipe()" data-i18n="run_recipe"></button>
    </div>
  </div>
</div>

<div id="page-status" class="page">
  <h2 data-i18n="pg_status"></h2>
  <div class="card">
    <div class="card-title" data-i18n="motor_status"></div>
    <div class="motor-row">
      <div>
        <div style="font-size:11px;color:var(--muted);margin-bottom:1px;" data-i18n="speed_pct"></div>
        <div class="motor-pct" id="s-pct">0<span>RPM</span></div>
      </div>
      <span class="badge b-idle" id="s-dir">IDLE</span>
    </div>
    <div class="pwr-bar"><div class="pwr-fill" id="s-pwr" style="width:0%"></div></div>
  </div>
  <div class="card">
    <div class="card-title" data-i18n="temp_title"></div>
    <div class="temp-row">
      <div class="temp-big" id="s-temp">--.-<span>°C</span></div>
      <span class="badge b-idle" id="s-temp-fault" style="display:none;" data-i18n="temp_err"></span>
    </div>
  </div>
  <div class="card">
    <div class="card-title" data-i18n="recipe_info"></div>
    <div id="s-recipe-name" style="font-size:16px;font-weight:700;margin-bottom:8px;">—</div>
    <div class="info-row"><span data-i18n="current_step"></span><span class="info-val" id="s-cur">—</span></div>
    <div class="info-row"><span data-i18n="next_step"></span><span class="info-val" id="s-nxt">—</span></div>
  </div>
  <div class="card">
    <div class="timer-lbl" data-i18n="step_timer"></div>
    <div class="timer-big" id="s-countdown">--:--</div>
    <div class="pwr-bar" style="margin-top:7px;"><div class="pwr-fill" id="s-sprog" style="width:0%;background:var(--info);"></div></div>
  </div>
  <div class="card">
    <div class="card-title" data-i18n="timeline"></div>
    <div class="timeline" id="s-timeline"><div style="flex:1;background:var(--border);border-radius:8px;"></div></div>
    <div id="s-tl-hint" style="font-size:10px;color:var(--muted);margin-top:3px;"></div>
  </div>
  <div class="card">
    <div class="ctrl-bar">
      <button class="btn btn-success" onclick="startLastRecipe()" data-i18n="btn_start"></button>
      <button class="btn btn-outline" id="btn-pause" onclick="togglePause()" data-i18n="btn_pause"></button>
      <button class="btn btn-danger"  onclick="doStop()" data-i18n="btn_stop"></button>
    </div>
  </div>
  <div class="card">
    <div class="card-title" data-i18n="stopwatch"></div>
    <div class="sw-disp" id="sw-disp">00:00.0</div>
    <div class="sw-row">
      <button class="btn btn-outline" onclick="swToggle()" id="sw-start" data-i18n="sw_start"></button>
      <button class="btn btn-outline" onclick="swReset()" data-i18n="sw_reset"></button>
    </div>
  </div>
</div>

<div id="page-manual" class="page">
  <h2 data-i18n="pg_manual"></h2>
  <div class="card">
    <div class="manual-live">
      <span class="badge b-idle" id="m-dir-badge">IDLE</span>
      <span id="m-pct-live" style="font-size:15px;font-weight:700;">0 RPM</span>
      <span id="m-mode-tag" style="font-size:11px;color:var(--muted);"></span>
    </div>
  </div>
  <div class="card">
    <div class="card-title" data-i18n="manual_power"></div>
    <div class="pwr-slider-val"><span id="m-pct-val">50</span><span>RPM</span></div>
    <input type="range" id="m-slider" min="1" max="80" value="50"
           oninput="document.getElementById('m-pct-val').textContent=this.value">
  </div>
  <div class="card">
    <div class="card-title" data-i18n="manual_mode_title"></div>
    <div class="toggle-row" style="margin-bottom:10px;">
      <span style="font-size:14px;" data-i18n="auto_cycle"></span>
      <label class="toggle">
        <input type="checkbox" id="m-cycle-chk" checked onchange="onCycleToggle()">
        <span class="slider-sw"></span>
      </label>
    </div>
    <div id="m-rot-grp">
      <div class="form-grp" style="margin-bottom:0;">
        <label class="form-lbl" data-i18n="rot_interval"></label>
        <input class="form-in" id="m-rot-in" type="number" min="5" value="30">
      </div>
    </div>
  </div>
  <div class="card">
    <div class="manual-btns">
      <button class="btn btn-success" onclick="manualStart(true)">
        <span style="font-size:20px;">▶</span><span data-i18n="dir_fwd_short"></span>
      </button>
      <button class="btn btn-danger" onclick="doStop()">
        <span style="font-size:20px;">⏹</span><span data-i18n="btn_stop"></span>
      </button>
      <button class="btn btn-info" onclick="manualStart(false)">
        <span style="font-size:20px;">◀</span><span data-i18n="dir_rev_short"></span>
      </button>
    </div>
  </div>
</div>

<div id="page-settings" class="page">
  <h2 data-i18n="pg_settings"></h2>
  <div class="card">
    <span class="sect-lbl" data-i18n="ap_section"></span>
    <div class="form-grp">
      <label class="form-lbl" data-i18n="wifi_ssid"></label>
      <input class="form-in" id="set-ap-ssid" type="text" maxlength="32" autocomplete="off">
    </div>
    <div class="form-grp">
      <label class="form-lbl" data-i18n="wifi_pass"></label>
      <input class="form-in" id="set-ap-pass" type="password" maxlength="64" autocomplete="new-password" placeholder="(변경 시에만 입력)">
    </div>
    <span class="sect-lbl" data-i18n="sta_section"></span>
    <div class="form-grp">
      <label class="form-lbl" data-i18n="sta_ssid"></label>
      <input class="form-in" id="set-sta-ssid" type="text" maxlength="32" autocomplete="off">
    </div>
    <div class="form-grp" style="margin-bottom:14px;">
      <label class="form-lbl" data-i18n="sta_pass"></label>
      <input class="form-in" id="set-sta-pass" type="password" maxlength="64" autocomplete="new-password">
    </div>
    <div class="form-grp" style="margin-bottom:14px;display:flex;align-items:center;gap:10px;">
      <label style="margin:0;font-weight:600;" data-i18n="static_ip"></label>
      <label style="position:relative;display:inline-block;width:42px;height:24px;margin:0;">
        <input type="checkbox" id="set-sta-static" onchange="onStaticToggle()" style="opacity:0;width:0;height:0;">
        <span onclick="document.getElementById('set-sta-static').click()" style="position:absolute;cursor:pointer;inset:0;background:#ccc;border-radius:24px;transition:.3s;"></span>
      </label>
    </div>
    <div id="static-ip-fields" style="display:none;">
      <div class="form-grp" style="margin-bottom:10px;">
        <label class="form-lbl" data-i18n="lbl_ip"></label>
        <input class="form-in" id="set-sta-ip" type="text" maxlength="15" placeholder="192.168.1.100">
      </div>
      <div class="form-grp" style="margin-bottom:10px;">
        <label class="form-lbl" data-i18n="lbl_gw"></label>
        <input class="form-in" id="set-sta-gw" type="text" maxlength="15" placeholder="192.168.1.1">
      </div>
      <div class="form-grp" style="margin-bottom:10px;">
        <label class="form-lbl" data-i18n="lbl_sn"></label>
        <input class="form-in" id="set-sta-sn" type="text" maxlength="15" placeholder="255.255.255.0">
      </div>
      <div class="form-grp" style="margin-bottom:14px;">
        <label class="form-lbl" data-i18n="lbl_dns"></label>
        <input class="form-in" id="set-sta-dns" type="text" maxlength="15" placeholder="8.8.8.8">
      </div>
    </div>
    <button class="btn btn-primary" style="width:100%;" onclick="saveSettings()" data-i18n="save_restart"></button>
  </div>
  <div class="card">
    <div class="card-title" data-i18n="net_status"></div>
    <div class="info-row"><span data-i18n="ap_ip"></span><span class="info-val">192.168.4.1</span></div>
    <div class="info-row">
      <span data-i18n="home_wifi"></span>
      <span id="sta-chip" class="chip chip-off" data-i18n="sta_disconn"></span>
    </div>
    <div class="info-row" id="sta-ip-row" style="display:none;">
      <span data-i18n="home_ip"></span><span class="info-val" id="sta-ip-val">—</span>
    </div>
  </div>
  <div class="card">
    <div class="card-title" data-i18n="language"></div>
    <div class="toggle-row">
      <span style="font-size:14px;">English</span>
      <label class="toggle">
        <input type="checkbox" id="lang-chk" onchange="toggleLang()">
        <span class="slider-sw"></span>
      </label>
    </div>
    <div style="margin-top:7px;font-size:12px;color:var(--muted);" id="lang-hint"></div>
  </div>
  <div class="card">
    <div class="card-title" data-i18n="saver_title"></div>
    <div class="toggle-row" style="margin-bottom:10px;">
      <span style="font-size:14px;" data-i18n="saver_enable"></span>
      <label class="toggle">
        <input type="checkbox" id="saver-en-chk" onchange="onSaverEnableToggle()">
        <span class="slider-sw"></span>
      </label>
    </div>
    <div class="form-grp">
      <label class="form-lbl" data-i18n="saver_timeout"></label>
      <select class="form-in" id="saver-timeout-sel" onchange="onSaverTimeoutChange()">
        <option value="30">30 s</option>
        <option value="60">60 s</option>
        <option value="120">2 min</option>
        <option value="300">5 min</option>
        <option value="600">10 min</option>
        <option value="1800">30 min</option>
      </select>
    </div>
    <div class="form-grp">
      <label class="form-lbl" data-i18n="saver_image"></label>
      <input class="form-in" id="saver-file" type="file" accept="image/jpeg,image/gif">
      <div style="display:flex;gap:7px;margin-top:7px;">
        <button class="btn btn-primary btn-sm" onclick="uploadSaverImage()" data-i18n="saver_upload"></button>
        <button class="btn btn-outline btn-sm" onclick="deleteSaverImage()" data-i18n="saver_delete"></button>
      </div>
      <div id="saver-status" style="margin-top:7px;font-size:12px;color:var(--muted);"></div>
    </div>
  </div>
  <div class="card">
    <div class="card-title" data-i18n="about"></div>
    <div class="info-row"><span>Firmware</span><span class="info-val">v3.0</span></div>
    <div class="info-row"><span>MCU</span><span class="info-val">ESP32-S3</span></div>
    <div class="info-row"><span>Driver</span><span class="info-val">TMC2209 (Stepper)</span></div>
    <div class="info-row"><span>Temp</span><span class="info-val">MAX31865 + PT100</span></div>
  </div>
</div>

<nav class="bnav">
  <button class="bnav-btn active" onclick="switchTab('recipe',this)">
    <span class="bnav-icon">📋</span><span data-i18n="tab_recipe"></span>
  </button>
  <button class="bnav-btn" onclick="switchTab('status',this)">
    <span class="bnav-icon">▶️</span><span data-i18n="tab_status"></span>
  </button>
  <button class="bnav-btn" onclick="switchTab('manual',this)">
    <span class="bnav-icon">🎛️</span><span data-i18n="tab_manual"></span>
  </button>
  <button class="bnav-btn" onclick="switchTab('settings',this)">
    <span class="bnav-icon">⚙️</span><span data-i18n="tab_settings"></span>
  </button>
</nav>

<div class="confirm-overlay" id="confirm-overlay">
  <div class="confirm-card">
    <div class="co-icon" id="co-icon">⚗️</div>
    <div class="co-done" id="co-done"></div>
    <div class="co-msg"  id="co-msg"></div>
    <div class="co-next" id="co-next" style="display:none;"></div>
    <button class="btn btn-primary btn-lg" onclick="doConfirm()" id="co-btn"></button>
  </div>
</div>
</div>

<script>
'use strict';
const STR={
  ko:{
    pg_recipe:'레시피 관리',pg_status:'현재 상태',pg_manual:'수동 제어',pg_settings:'설정',
    tab_recipe:'레시피',tab_status:'상태',tab_manual:'수동',tab_settings:'설정',
    recipes:'레시피 목록',add_recipe:'+ 추가',no_recipes:'레시피가 없습니다. 추가해주세요.',
    del_recipe:'삭제',add_step:'+ 단계 추가',run_recipe:'▶ 실행',
    motor_status:'모터 상태',speed_pct:'출력축 속도',
    temp_title:'약품 온도',temp_err:'센서 오류',
    recipe_info:'레시피 진행',current_step:'현재 단계',next_step:'다음 단계',
    step_timer:'현재 단계 남은 시간',timeline:'단계 타임라인',
    btn_start:'▶ 시작',btn_pause:'⏸ 일시정지',btn_resume:'▶ 재개',btn_stop:'⏹ 정지',
    stopwatch:'수동 스톱워치',sw_start:'시작',sw_stop:'정지',sw_reset:'초기화',
    manual_power:'속도 설정',manual_mode_title:'작동 모드',
    auto_cycle:'자동 방향 전환',rot_interval:'방향당 운전 시간 (초)',
    dir_fwd_short:'순방향',dir_rev_short:'역방향',
    ap_section:'자체 AP 설정 (직접 접속)',wifi_ssid:'AP SSID',wifi_pass:'AP 비밀번호',
    sta_section:'홈 WiFi 연결 (같은 네트워크 접속)',sta_ssid:'홈 WiFi SSID',sta_pass:'홈 WiFi 비밀번호',
    static_ip:'고정 IP 사용',lbl_ip:'IP 주소',lbl_gw:'게이트웨이',lbl_sn:'서브넷 마스크',lbl_dns:'DNS 서버',
    save_restart:'저장 및 재시작',
    net_status:'네트워크 상태',ap_ip:'AP IP (직접 접속)',
    home_wifi:'홈 WiFi',sta_conn:'연결됨 ✓',sta_disconn:'미연결',home_ip:'홈 WiFi IP',
    language:'언어 설정',lang_hint:'현재: 한국어',about:'디바이스 정보',
    saver_title:'화면보호기',saver_enable:'활성화',saver_timeout:'진입 대기 시간',
    saver_image:'배경 이미지 (JPEG / GIF)',saver_upload:'업로드',saver_delete:'삭제',
    saver_ok:'업로드 완료',saver_fail:'업로드 실패',saver_none:'이미지 없음',
    saver_has:'설정됨',saver_choose:'파일을 먼저 선택해주세요.',
    saver_proc:'이미지 처리 중…',saver_up:'업로드 중…',
    ph_step_name:'단계 이름',lbl_power:'속도(RPM)',lbl_dur:'시간(초)',lbl_rot:'방향전환(초)',
    confirm_del:'이 레시피를 삭제하시겠습니까?',
    confirm_run:'레시피를 실행합니다. 현재 동작이 중단됩니다.',
    prompt_name:'레시피 이름:',default_name:'새 레시피',
    warn_no_steps:'단계를 먼저 추가해주세요.',warn_no_sel:'레시피를 먼저 선택해주세요.',
    warn_no_last:'레시피 탭에서 먼저 실행해주세요.',
    saved_ok:'저장됨. 재시작 중... 새 네트워크에 접속하세요.',err_ssid:'SSID를 입력해주세요.',
    dir_fwd:'순방향 ▶',dir_rev:'◀ 역방향',dir_rest:'방향전환',dir_idle:'정지',
    co_done_last:'🎉 레시피 완료!',co_done_step:'✅ 단계 완료',
    co_msg_last:'모든 단계가 완료되었습니다.',co_msg_step:'약품을 교체하고 다음 단계를 진행하세요.',
    co_next_pre:'다음 단계: ',co_btn_last:'확인',co_btn_next:'다음 단계 ▶',
    manual_tag:'수동 모드',
  },
  en:{
    pg_recipe:'Recipes',pg_status:'Status',pg_manual:'Manual',pg_settings:'Settings',
    tab_recipe:'Recipe',tab_status:'Status',tab_manual:'Manual',tab_settings:'Settings',
    recipes:'Recipes',add_recipe:'+ Add',no_recipes:'No recipes. Press + Add.',
    del_recipe:'Delete',add_step:'+ Add Step',run_recipe:'▶ Run',
    motor_status:'Motor Status',speed_pct:'Output Shaft',
    temp_title:'Solution Temp',temp_err:'Sensor Error',
    recipe_info:'Recipe Progress',current_step:'Current Step',next_step:'Next Step',
    step_timer:'Step Remaining',timeline:'Step Timeline',
    btn_start:'▶ Start',btn_pause:'⏸ Pause',btn_resume:'▶ Resume',btn_stop:'⏹ Stop',
    stopwatch:'Manual Stopwatch',sw_start:'Start',sw_stop:'Stop',sw_reset:'Reset',
    manual_power:'Speed Setting',manual_mode_title:'Operation Mode',
    auto_cycle:'Auto Direction Cycle',rot_interval:'Per-direction Time (sec)',
    dir_fwd_short:'Forward',dir_rev_short:'Reverse',
    ap_section:'AP Settings (Direct Connect)',wifi_ssid:'AP SSID',wifi_pass:'AP Password',
    sta_section:'Home WiFi (Same Network Access)',sta_ssid:'Home WiFi SSID',sta_pass:'Home WiFi Password',
    static_ip:'Use Static IP',lbl_ip:'IP Address',lbl_gw:'Gateway',lbl_sn:'Subnet Mask',lbl_dns:'DNS Server',
    save_restart:'Save & Restart',
    net_status:'Network Status',ap_ip:'AP IP (Direct)',
    home_wifi:'Home WiFi',sta_conn:'Connected ✓',sta_disconn:'Not connected',home_ip:'Home WiFi IP',
    language:'Language',lang_hint:'Current: English',about:'Device Info',
    saver_title:'Screensaver',saver_enable:'Enabled',saver_timeout:'Idle Timeout',
    saver_image:'Background Image (JPEG / GIF)',saver_upload:'Upload',saver_delete:'Delete',
    saver_ok:'Upload complete',saver_fail:'Upload failed',saver_none:'No image set',
    saver_has:'Image set',saver_choose:'Please pick a file first.',
    saver_proc:'Processing image…',saver_up:'Uploading…',
    ph_step_name:'Step name',lbl_power:'Speed(RPM)',lbl_dur:'Duration(s)',lbl_rot:'Rotation(s)',
    confirm_del:'Delete this recipe?',
    confirm_run:'Run recipe? Current operation will stop.',
    prompt_name:'Recipe name:',default_name:'New Recipe',
    warn_no_steps:'Add steps first.',warn_no_sel:'Select a recipe first.',
    warn_no_last:'Select and run a recipe from the Recipe tab.',
    saved_ok:'Saved. Restarting...',err_ssid:'Please enter an SSID.',
    dir_fwd:'Forward ▶',dir_rev:'◀ Reverse',dir_rest:'Switching',dir_idle:'Idle',
    co_done_last:'🎉 Recipe Complete!',co_done_step:'✅ Step Done',
    co_msg_last:'All steps completed.',co_msg_step:'Replace chemicals, then continue.',
    co_next_pre:'Next: ',co_btn_last:'Done',co_btn_next:'Next Step ▶',
    manual_tag:'Manual',
  }
};
let lang=localStorage.getItem('lang')||'ko';
function t(k){return STR[lang][k]||k;}
function applyLang(){
  document.querySelectorAll('[data-i18n]').forEach(el=>el.textContent=t(el.dataset.i18n));
  document.getElementById('lang-chk').checked=(lang==='en');
  document.getElementById('lang-hint').textContent=t('lang_hint');
  renderCatTabs();renderRecipeList();renderStepEditor();
}
function toggleLang(){lang=(lang==='ko')?'en':'ko';localStorage.setItem('lang',lang);applyLang();}

const CATS=['B&W','C-41','ECN-2','E-6'];
function mkE(){const r={};CATS.forEach(c=>r[c]=[]);return r;}
async function loadR(){
  try{
    const r=await fetch('/api/recipes/load');
    if(!r.ok)return mkE();
    const d=await r.json();
    CATS.forEach(c=>{if(!d[c])d[c]=[];});
    return d;
  }catch{return mkE();}
}
let _saveTimer=null;
function saveR(d){
  clearTimeout(_saveTimer);
  // 800ms 디바운스: 빠른 타이핑 중 불필요한 플래시 쓰기 방지
  _saveTimer=setTimeout(()=>postJ('/api/recipes/save',d),800);
}
let recipes=mkE(),activeCat=CATS[0],selIdx=-1,lastRun=null;

function renderCatTabs(){
  document.getElementById('cat-tabs').innerHTML=CATS.map(c=>`<button class="cat-tab ${c===activeCat?'active':''}" onclick="setCat('${c}')">${c}</button>`).join('');
}
function setCat(c){activeCat=c;selIdx=-1;renderCatTabs();renderRecipeList();renderStepEditor();}
function renderRecipeList(){
  const list=document.getElementById('recipe-list'),empty=document.getElementById('recipe-empty'),items=recipes[activeCat]||[];
  if(!items.length){list.innerHTML='';empty.style.display='';document.getElementById('step-editor').style.display='none';return;}
  empty.style.display='none';
  list.innerHTML=items.map((r,i)=>`<li class="recipe-item ${i===selIdx?'selected':''}" onclick="selRecipe(${i})"><span class="recipe-item-name">${esc(r.name)}</span><span class="recipe-item-info">${r.steps.length} steps</span></li>`).join('');
}
function selRecipe(i){selIdx=i;renderRecipeList();renderStepEditor();}
function addRecipe(){const name=prompt(t('prompt_name'),t('default_name'));if(!name)return;recipes[activeCat].push({name:name.trim(),steps:[]});saveR(recipes);selIdx=recipes[activeCat].length-1;renderRecipeList();renderStepEditor();}
function deleteSelectedRecipe(){if(selIdx<0||!confirm(t('confirm_del')))return;recipes[activeCat].splice(selIdx,1);saveR(recipes);selIdx=-1;renderRecipeList();renderStepEditor();}
function renderStepEditor(){
  const ed=document.getElementById('step-editor');
  if(selIdx<0||!recipes[activeCat][selIdx]){ed.style.display='none';return;}
  ed.style.display='';
  const rec=recipes[activeCat][selIdx];
  document.getElementById('step-editor-title').textContent=rec.name;
  document.getElementById('step-list').innerHTML=rec.steps.map((s,i)=>`
    <div class="step-card"><div class="step-hdr">
      <div class="step-num">${i+1}</div>
      <input class="step-name-in" placeholder="${t('ph_step_name')}" value="${esc(s.name)}" oninput="updS(${i},'name',this.value)">
      <button class="btn btn-outline btn-icon btn-sm" onclick="mvS(${i},-1)">↑</button>
      <button class="btn btn-outline btn-icon btn-sm" onclick="mvS(${i},1)">↓</button>
      <button class="btn btn-danger btn-icon btn-sm" onclick="rmS(${i})">✕</button>
    </div>
    <div class="step-params">
      <div class="sp"><label>${t('lbl_power')}</label><input type="number" min="1" max="80" value="${s.speedRpm}" oninput="updS(${i},'speedRpm',+this.value)"></div>
      <div class="sp"><label>${t('lbl_dur')}</label><input type="number" min="1" value="${s.durSec}" oninput="updS(${i},'durSec',+this.value)"></div>
      <div class="sp"><label>${t('lbl_rot')}</label><input type="number" min="5" value="${s.rotIntSec}" oninput="updS(${i},'rotIntSec',+this.value)"></div>
    </div></div>`).join('');
}
function addStep(){if(selIdx<0)return;const ss=recipes[activeCat][selIdx].steps;ss.push({name:'Step '+(ss.length+1),speedRpm:50,durSec:60,rotIntSec:30});saveR(recipes);renderStepEditor();}
function rmS(i){recipes[activeCat][selIdx].steps.splice(i,1);saveR(recipes);renderStepEditor();}
function mvS(i,d){const ss=recipes[activeCat][selIdx].steps,j=i+d;if(j<0||j>=ss.length)return;[ss[i],ss[j]]=[ss[j],ss[i]];saveR(recipes);renderStepEditor();}
function updS(i,k,v){recipes[activeCat][selIdx].steps[i][k]=v;saveR(recipes);}

async function runSelectedRecipe(){
  if(selIdx<0){alert(t('warn_no_sel'));return;}
  const rec=recipes[activeCat][selIdx];
  if(!rec.steps.length){alert(t('warn_no_steps'));return;}
  if(!confirm(t('confirm_run')))return;
  lastRun={recipeName:rec.name,steps:rec.steps.map(s=>({name:s.name,speedRpm:clamp(s.speedRpm||50,1,80),durationSec:Math.max(1,s.durSec),rotIntSec:Math.max(5,s.rotIntSec)}))};
  await postJ('/api/start',lastRun);
  switchTab('status',document.querySelectorAll('.bnav-btn')[1]);
}
async function startLastRecipe(){if(!lastRun){alert(t('warn_no_last'));return;}await postJ('/api/start',lastRun);}

let sd={motorRpm:0,motorDir:'IDLE',recipeRun:false,recipePause:false,waitConfirm:false,recipeName:'',stepName:'',stepNext:'',totalSteps:0,stepIdx:0,stepDurSec:0,stepRemSec:0,manualMode:false,staConn:false,staIP:'',temperature:-999,tempFault:false};
setInterval(fetchStatus,500);
async function fetchStatus(){try{const r=await fetch('/api/status');sd=await r.json();updateStatusUI();}catch(e){}}

function updateStatusUI(){
  document.getElementById('s-pct').innerHTML=sd.motorRpm+'<span>RPM</span>';
  document.getElementById('s-pwr').style.width=Math.min(100,sd.motorRpm*100/80)+'%';  // 최대 80RPM = 100%
  const dm={FWD:['b-fwd',t('dir_fwd')],REV:['b-rev',t('dir_rev')],REST:['b-rest',t('dir_rest')],IDLE:['b-idle',t('dir_idle')]};
  const [cls,txt]=dm[sd.motorDir]||['b-idle',t('dir_idle')];
  const b=document.getElementById('s-dir');b.className='badge '+cls;b.textContent=txt;

  // 온도 업데이트
  const te=document.getElementById('s-temp'),tf=document.getElementById('s-temp-fault');
  if(sd.tempFault){te.innerHTML='--.-<span>°C</span>';tf.style.display='';}
  else if(sd.temperature<=-100){te.innerHTML='--.-<span>°C</span>';tf.style.display='none';}
  else{te.innerHTML=sd.temperature.toFixed(1)+'<span>°C</span>';tf.style.display='none';}

  document.getElementById('s-recipe-name').textContent=sd.recipeName||'—';
  document.getElementById('s-cur').textContent=sd.stepName||'—';
  document.getElementById('s-nxt').textContent=sd.stepNext||'—';
  document.getElementById('s-countdown').textContent=sd.recipeRun?fmtSec(sd.stepRemSec):'--:--';
  const dur=sd.stepDurSec||1;
  document.getElementById('s-sprog').style.width=(sd.recipeRun?((dur-sd.stepRemSec)/dur*100).toFixed(1):0)+'%';
  renderTimeline(sd);
  const pb=document.getElementById('btn-pause');
  if(sd.recipePause){pb.textContent=t('btn_resume');pb.className='btn btn-success';}
  else{pb.textContent=t('btn_pause');pb.className='btn btn-outline';}
  const ov=document.getElementById('confirm-overlay');
  if(sd.waitConfirm){
    ov.classList.add('show');
    const isLast=!sd.stepNext;
    document.getElementById('co-icon').textContent=isLast?'🎉':'⚗️';
    document.getElementById('co-done').textContent=isLast?t('co_done_last'):t('co_done_step')+': '+sd.stepName;
    document.getElementById('co-msg').textContent=isLast?t('co_msg_last'):t('co_msg_step');
    const nx=document.getElementById('co-next');
    if(!isLast){nx.style.display='';nx.textContent=t('co_next_pre')+sd.stepNext;}else{nx.style.display='none';}
    document.getElementById('co-btn').textContent=isLast?t('co_btn_last'):t('co_btn_next');
  } else { ov.classList.remove('show'); }
  const mb=document.getElementById('m-dir-badge');mb.className='badge '+cls;mb.textContent=txt;
  document.getElementById('m-pct-live').textContent=sd.motorRpm+' RPM';
  document.getElementById('m-mode-tag').textContent=sd.manualMode?t('manual_tag'):'';
  const chip=document.getElementById('sta-chip'),ipRow=document.getElementById('sta-ip-row');
  if(sd.staConn){chip.className='chip chip-ok';chip.textContent=t('sta_conn');ipRow.style.display='';document.getElementById('sta-ip-val').textContent=sd.staIP||'—';}
  else{chip.className='chip chip-off';chip.textContent=t('sta_disconn');ipRow.style.display='none';}
}

function renderTimeline(d){
  const tl=document.getElementById('s-timeline'),hint=document.getElementById('s-tl-hint');
  if(!d.recipeRun||!d.totalSteps){tl.innerHTML='<div style="flex:1;background:var(--border);border-radius:8px;"></div>';hint.textContent='';return;}
  const steps=(lastRun&&lastRun.steps.length===d.totalSteps)?lastRun.steps:null;
  const total=steps?steps.reduce((a,s)=>a+s.durationSec,0):d.totalSteps;
  tl.innerHTML=Array.from({length:d.totalSteps},(_,i)=>{
    const cls=i<d.stepIdx?'done':i===d.stepIdx?'current':'pending';
    const w=steps?(steps[i].durationSec/total*100).toFixed(1)+'%':(100/d.totalSteps).toFixed(1)+'%';
    const lbl=steps?(steps[i].name.length>7?steps[i].name.slice(0,6)+'…':steps[i].name):(i+1);
    return `<div class="t-seg sc${i%8} ${cls}" style="flex:0 0 ${w}">${lbl}</div>`;
  }).join('');
  if(steps&&d.stepIdx<steps.length) hint.textContent=`${d.stepIdx+1}/${d.totalSteps}: ${steps[d.stepIdx].name}`;
}

async function togglePause(){await fetch('/api/pause',{method:'POST'});fetchStatus();}
async function doStop(){await fetch('/api/stop',{method:'POST'});fetchStatus();}
async function doConfirm(){await fetch('/api/confirm',{method:'POST'});fetchStatus();}

function onCycleToggle(){document.getElementById('m-rot-grp').style.display=document.getElementById('m-cycle-chk').checked?'':'none';}
async function manualStart(fwd){
  const pct=+document.getElementById('m-slider').value;
  const cycle=document.getElementById('m-cycle-chk').checked;
  const rotSec=Math.max(5,+document.getElementById('m-rot-in').value||30);
  await postJ('/api/manual',{speedRpm:pct,fwd:fwd,cycle:cycle,rotIntSec:rotSec});
  fetchStatus();
}

let swOn=false,swBase=0,swRef=0,swTmr=null;
function swToggle(){
  if(!swOn){swRef=Date.now();swOn=true;document.getElementById('sw-start').textContent=t('sw_stop');swTmr=setInterval(swTick,100);}
  else{clearInterval(swTmr);swBase+=Date.now()-swRef;swOn=false;document.getElementById('sw-start').textContent=t('sw_start');swTick();}
}
function swReset(){clearInterval(swTmr);swOn=false;swBase=0;document.getElementById('sw-start').textContent=t('sw_start');document.getElementById('sw-disp').textContent='00:00.0';}
function swTick(){const ms=swBase+(swOn?Date.now()-swRef:0),s=Math.floor(ms/1000),ds=Math.floor((ms%1000)/100);document.getElementById('sw-disp').textContent=pad2(Math.floor(s/60))+':'+pad2(s%60)+'.'+ds;}

function onStaticToggle(){
  const on=document.getElementById('set-sta-static').checked;
  document.getElementById('static-ip-fields').style.display=on?'block':'none';
  // 토글 스위치 색상 업데이트
  const span=document.querySelector('#set-sta-static+span');
  if(span)span.style.background=on?'#4CAF50':'#ccc';
}
async function loadSettings(){
  try{
    const r=await fetch('/api/settings');const d=await r.json();
    document.getElementById('set-ap-ssid').value=d.apSSID||'';
    document.getElementById('set-sta-ssid').value=d.staSSID||'';
    const st=d.staStatic||false;
    document.getElementById('set-sta-static').checked=st;
    document.getElementById('set-sta-ip').value=d.staStaticIP||'192.168.1.100';
    document.getElementById('set-sta-gw').value=d.staGW||'192.168.1.1';
    document.getElementById('set-sta-sn').value=d.staSN||'255.255.255.0';
    document.getElementById('set-sta-dns').value=d.staDNS||'8.8.8.8';
    onStaticToggle();
  }catch(e){}
}
async function saveSettings(){
  const apSSID=document.getElementById('set-ap-ssid').value.trim();
  if(!apSSID){alert(t('err_ssid'));return;}
  const staStatic=document.getElementById('set-sta-static').checked;
  await postJ('/api/settings',{
    apSSID,apPass:document.getElementById('set-ap-pass').value,
    staSSID:document.getElementById('set-sta-ssid').value.trim(),
    staPass:document.getElementById('set-sta-pass').value,
    staStatic,
    staStaticIP:document.getElementById('set-sta-ip').value.trim(),
    staGW:document.getElementById('set-sta-gw').value.trim(),
    staSN:document.getElementById('set-sta-sn').value.trim(),
    staDNS:document.getElementById('set-sta-dns').value.trim()
  });
  alert(t('saved_ok'));
}

function switchTab(name,btn){
  document.querySelectorAll('.page').forEach(p=>p.classList.remove('active'));
  document.querySelectorAll('.bnav-btn').forEach(b=>b.classList.remove('active'));
  document.getElementById('page-'+name).classList.add('active');
  btn.classList.add('active');
}

function fmtSec(s){s=Math.max(0,Math.floor(s));return pad2(Math.floor(s/60))+':'+pad2(s%60);}
function pad2(n){return String(n).padStart(2,'0');}
function clamp(v,lo,hi){return Math.max(lo,Math.min(hi,v));}
function esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');}
async function postJ(url,d){return fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)});}

// ─── 화면보호기 ───
async function loadSaver(){
  try{
    const r=await fetch('/api/saver/settings');
    if(!r.ok)return;
    const d=await r.json();
    document.getElementById('saver-en-chk').checked=!!d.enabled;
    document.getElementById('saver-timeout-sel').value=String(d.timeoutSec||60);
    const st=document.getElementById('saver-status');
    if(d.imageType==='gif')      st.textContent=t('saver_has')+' (GIF, '+Math.ceil(d.imageSize/1024)+' KB)';
    else if(d.imageType==='jpg') st.textContent=t('saver_has')+' (JPEG, '+Math.ceil(d.imageSize/1024)+' KB)';
    else                          st.textContent=t('saver_none');
  }catch{}
}
async function pushSaver(payload){
  await fetch('/api/saver/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
}
function onSaverEnableToggle(){
  pushSaver({enabled:document.getElementById('saver-en-chk').checked});
}
function onSaverTimeoutChange(){
  pushSaver({timeoutSec:parseInt(document.getElementById('saver-timeout-sel').value,10)});
}
/* ──────────────────────────────────────────────────────────────
   클라이언트(브라우저) 측 이미지 트랜스코드 — 보드 부하 경감
   · 디스플레이는 320x240(ST7789)이므로 그 안에 맞도록 강제 다운스케일
   · JPEG/기타 정적 이미지 : canvas 리사이즈 + 품질 압축 → JPEG
   · 애니메이션 GIF        : 전 프레임 디코드(LZW) → 320x240 리사이즈
                              → 256색 양자화 → 프레임 간 차분(투명) 최적화
                              → GIF89a 재인코딩 (애니메이션 유지, 용량↓)
   라이브러리 없이 순수 JS로 구현(오프라인 AP라 CDN 불가).
   ────────────────────────────────────────────────────────────── */
const SAVER_MAXW=320, SAVER_MAXH=240;       // 디스플레이 해상도
const SAVER_JPEG_MAX=1.5*1024*1024;         // JPEG 목표 상한(초과 시 품질↓)
const SAVER_MAX_FRAMES=64;                  // GIF 출력 최대 프레임(초과분은 솎음)

// 비율 유지하며 320x240 안에 맞춤(확대 없음, 축소만)
function fitSize(w,h){
  const s=Math.min(SAVER_MAXW/w, SAVER_MAXH/h, 1);
  return {w:Math.max(1,Math.round(w*s)), h:Math.max(1,Math.round(h*s))};
}
function canvasBlob(cv,type,q){ return new Promise(r=>cv.toBlob(r,type,q)); }
async function isGifFile(file){
  const b=new Uint8Array(await file.slice(0,4).arrayBuffer());
  return b[0]==0x47&&b[1]==0x49&&b[2]==0x46&&b[3]==0x38; // "GIF8"
}

// ===== 정적 이미지(JPEG/PNG 등) → 리사이즈 + JPEG 압축 =====
async function transcodeStill(file){
  const bmp=await createImageBitmap(file);
  const {w,h}=fitSize(bmp.width,bmp.height);
  const cv=document.createElement('canvas'); cv.width=w; cv.height=h;
  const ctx=cv.getContext('2d');
  ctx.fillStyle='#000'; ctx.fillRect(0,0,w,h);     // 투명 영역은 검정으로
  ctx.drawImage(bmp,0,0,w,h);
  if(bmp.close) bmp.close();
  let q=0.85, blob=await canvasBlob(cv,'image/jpeg',q);
  while(blob.size>SAVER_JPEG_MAX && q>0.4){ q-=0.1; blob=await canvasBlob(cv,'image/jpeg',q); }
  return {blob, name:'saver.jpg'};
}

// ===== GIF LZW 디코드 =====
function lzwDecode(data, minCodeSize, pixelCount){
  const out=new Uint8Array(pixelCount);
  const clearCode=1<<minCodeSize, eoiCode=clearCode+1;
  let codeSize=minCodeSize+1, dict=[], bitPos=0, oPos=0, prev=null;
  const initDict=()=>{ dict=[]; for(let i=0;i<clearCode;i++) dict.push([i]); dict.push(null); dict.push(null); };
  const readCode=()=>{
    let code=0;
    for(let i=0;i<codeSize;i++){
      const bi=bitPos>>3; if(bi>=data.length) return eoiCode;
      code |= ((data[bi]>>(bitPos&7))&1)<<i; bitPos++;
    }
    return code;
  };
  initDict();
  while(oPos<pixelCount){
    const code=readCode();
    if(code==eoiCode) break;
    if(code==clearCode){ initDict(); codeSize=minCodeSize+1; prev=null; continue; }
    let entry;
    if(code<dict.length && dict[code]!=null) entry=dict[code];
    else if(prev!=null) entry=prev.concat(prev[0]);
    else break;
    for(let i=0;i<entry.length && oPos<pixelCount;i++) out[oPos++]=entry[i];
    if(prev!=null) dict.push(prev.concat(entry[0]));
    prev=entry;
    if(dict.length==(1<<codeSize) && codeSize<12) codeSize++;
  }
  return out;
}
function deinterlace(px,w,h){
  const out=new Uint8Array(w*h); let row=0;
  const passes=[[0,8],[4,8],[2,4],[1,2]];
  for(const [start,step] of passes)
    for(let y=start;y<h;y+=step){ for(let x=0;x<w;x++) out[y*w+x]=px[row*w+x]; row++; }
  return out;
}

// ===== GIF 디코드 + 즉시 320x240 리사이즈(메모리 절약) =====
function decodeGifResized(buf){
  const d=new Uint8Array(buf); let p=0;
  const u8=()=>d[p++], u16=()=>{const v=d[p]|(d[p+1]<<8); p+=2; return v;};
  if(!(d[0]==0x47&&d[1]==0x49&&d[2]==0x46)) throw new Error('not a gif');
  p=6;
  const W=u16(), H=u16(), packed=u8(); u8(); u8();   // bgIndex, aspect 무시
  const ct=(off,size)=>{ const t=new Array(size); for(let i=0;i<size;i++){const o=off+i*3; t[i]=[d[o],d[o+1],d[o+2]];} return t; };
  let gct=null;
  if(packed&0x80){ const sz=2<<(packed&7); gct=ct(p,sz); p+=sz*3; }

  const {w:tw,h:th}=fitSize(W,H);
  const src=document.createElement('canvas'); src.width=W; src.height=H; const sctx=src.getContext('2d');
  const dst=document.createElement('canvas'); dst.width=tw; dst.height=th; const dctx=dst.getContext('2d');
  const srcImg=sctx.createImageData(W,H);
  const cv=srcImg.data;                              // 누적 합성 캔버스(RGBA), 초기 투명
  const frames=[];
  let gce={delay:0,tIndex:-1,disposal:0};
  let prevD=0, prevRect=null, prevSnap=null;

  const disposeBefore=()=>{
    if(prevD==3 && prevSnap){ cv.set(prevSnap); }
    else if(prevD==2 && prevRect){
      for(let y=prevRect.y;y<prevRect.y+prevRect.h;y++){ if(y<0||y>=H)continue;
        for(let x=prevRect.x;x<prevRect.x+prevRect.w;x++){ if(x<0||x>=W)continue;
          const o=(y*W+x)*4; cv[o]=cv[o+1]=cv[o+2]=cv[o+3]=0; } }
    }
  };
  while(p<d.length){
    const block=u8();
    if(block==0x3B) break;                            // trailer
    if(block==0x21){                                  // extension
      const label=u8();
      if(label==0xF9){                                // graphic control
        u8();                                         // block size(4)
        const pk=u8(); gce.delay=u16()*10; const ti=u8(); u8();
        gce.disposal=(pk>>2)&7; gce.tIndex=(pk&1)?ti:-1;
      } else { let s; while((s=u8())!=0) p+=s; }      // 기타 확장 skip
      continue;
    }
    if(block==0x2C){                                  // image descriptor
      const ix=u16(), iy=u16(), iw=u16(), ih=u16(), ip=u8();
      let table=gct;
      if(ip&0x80){ const sz=2<<(ip&7); table=ct(p,sz); p+=sz*3; }
      const interlace=ip&0x40, minCode=u8();
      const bytes=[]; let s; while((s=u8())!=0){ for(let i=0;i<s;i++) bytes.push(d[p++]); }
      let idx=lzwDecode(new Uint8Array(bytes), minCode, iw*ih);
      if(interlace) idx=deinterlace(idx,iw,ih);

      disposeBefore();
      const snap = gce.disposal==3 ? cv.slice() : null;   // 그리기 전 스냅샷
      for(let row=0;row<ih;row++){ const cy=iy+row; if(cy<0||cy>=H)continue;
        for(let col=0;col<iw;col++){ const cx=ix+col; if(cx<0||cx>=W)continue;
          const v=idx[row*iw+col]; if(v==gce.tIndex)continue;
          const c=table&&table[v]; if(!c)continue;
          const o=(cy*W+cx)*4; cv[o]=c[0];cv[o+1]=c[1];cv[o+2]=c[2];cv[o+3]=255; } }

      // 합성 결과를 320x240로 축소해 저장(투명 영역은 검정 위에 합성)
      sctx.putImageData(srcImg,0,0);
      dctx.fillStyle='#000'; dctx.fillRect(0,0,tw,th); dctx.drawImage(src,0,0,tw,th);
      frames.push({rgba:dctx.getImageData(0,0,tw,th).data, delay:gce.delay||100});

      prevD=gce.disposal; prevRect={x:ix,y:iy,w:iw,h:ih}; prevSnap=snap;
      gce={delay:0,tIndex:-1,disposal:0};
      continue;
    }
    break;                                            // 알 수 없는 블록
  }
  if(!frames.length) throw new Error('gif: no frames');
  return {w:tw,h:th,frames};
}

// 프레임 과다 시 균등 솎아내기(딜레이는 합산해 총 재생시간 유지)
function capFrames(frames,maxF){
  if(frames.length<=maxF) return frames;
  const step=frames.length/maxF, out=[];
  for(let i=0;i<maxF;i++){
    const a=Math.floor(i*step), b=Math.floor((i+1)*step);
    let delay=0; for(let j=a;j<b;j++) delay+=frames[j].delay;
    out.push({rgba:frames[Math.min(frames.length-1,a)].rgba, delay});
  }
  return out;
}

// ===== 색상 양자화(median-cut, 전 프레임 공용 팔레트) =====
function buildPalette(frames,maxColors){
  let total=0; for(const f of frames) total+=f.rgba.length/4;
  const step=Math.max(1,Math.floor(total/16000)), px=[];
  let c=0;
  for(const f of frames){ const d=f.rgba;
    for(let i=0;i<d.length;i+=4){ if((c++%step)==0) px.push([d[i],d[i+1],d[i+2]]); } }
  if(!px.length) return [[0,0,0]];
  const box=ps=>{ let r0=255,r1=0,g0=255,g1=0,b0=255,b1=0,rs=0,gs=0,bs=0;
    for(const q of ps){ if(q[0]<r0)r0=q[0];if(q[0]>r1)r1=q[0];if(q[1]<g0)g0=q[1];if(q[1]>g1)g1=q[1];
      if(q[2]<b0)b0=q[2];if(q[2]>b1)b1=q[2]; rs+=q[0];gs+=q[1];bs+=q[2]; }
    const n=ps.length||1;
    return {ps,rR:r1-r0,gR:g1-g0,bR:b1-b0,avg:[Math.round(rs/n),Math.round(gs/n),Math.round(bs/n)]}; };
  let boxes=[box(px)];
  while(boxes.length<maxColors){
    let bi=-1,best=-1;
    for(let i=0;i<boxes.length;i++){ const b=boxes[i]; if(b.ps.length<2)continue;
      const r=Math.max(b.rR,b.gR,b.bR); if(r>best){best=r;bi=i;} }
    if(bi<0) break;
    const b=boxes[bi];
    const ch=b.rR>=b.gR&&b.rR>=b.bR?0:(b.gR>=b.bR?1:2);
    b.ps.sort((x,y)=>x[ch]-y[ch]);
    const m=b.ps.length>>1;
    boxes.splice(bi,1,box(b.ps.slice(0,m)),box(b.ps.slice(m)));
  }
  return boxes.map(b=>b.avg);
}
function makeMapper(pal){
  const cache=new Int16Array(32768).fill(-1);
  return (r,g,b)=>{
    const key=((r>>3)<<10)|((g>>3)<<5)|(b>>3);
    let v=cache[key]; if(v>=0) return v;
    let best=0,bd=1e9;
    for(let i=0;i<pal.length;i++){ const dr=r-pal[i][0],dg=g-pal[i][1],db=b-pal[i][2];
      const dd=dr*dr+dg*dg+db*db; if(dd<bd){bd=dd;best=i; if(!dd)break;} }
    cache[key]=best; return best;
  };
}

// ===== GIF89a 인코드 (프레임 간 차분 투명 최적화) =====
function lzwEncode(idx,minCode,out){
  const clearCode=1<<minCode, eoiCode=clearCode+1;
  let codeSize=minCode+1, dict=new Map(), next=clearCode+2;
  let cur=0,bits=0; const sub=[];
  const write=code=>{ cur|=code<<bits; bits+=codeSize; while(bits>=8){ sub.push(cur&0xff); cur>>=8; bits-=8; } };
  out.push(minCode);
  write(clearCode);
  let prefix=idx[0];
  for(let i=1;i<idx.length;i++){
    const k=idx[i], key=(prefix<<8)|k;
    if(dict.has(key)){ prefix=dict.get(key); continue; }
    write(prefix);
    if(next<4096){ dict.set(key,next++); if(next==(1<<codeSize)&&codeSize<12)codeSize++; }
    else { write(clearCode); dict=new Map(); next=clearCode+2; codeSize=minCode+1; }
    prefix=k;
  }
  write(prefix); write(eoiCode);
  if(bits>0) sub.push(cur&0xff);
  for(let i=0;i<sub.length;i+=255){ const n=Math.min(255,sub.length-i); out.push(n);
    for(let j=0;j<n;j++) out.push(sub[i+j]); }
  out.push(0);
}
function encodeGif(w,h,frames,pal){
  const transp=pal.length;                          // 차분 최적화용 투명 인덱스
  let bits=1; while((1<<bits)<pal.length+1) bits++;  // 투명 슬롯까지 수용
  const minCode=Math.max(2,bits), tableSize=1<<bits;
  const out=[];
  const str=s=>{ for(let i=0;i<s.length;i++) out.push(s.charCodeAt(i)); };
  str('GIF89a');
  out.push(w&0xff,(w>>8)&0xff,h&0xff,(h>>8)&0xff);
  out.push(0x80|((bits-1)<<4)|(bits-1), 0, 0);       // GCT 사용, 색해상도/크기
  for(let i=0;i<tableSize;i++){ const c=pal[i]||[0,0,0]; out.push(c[0],c[1],c[2]); }
  out.push(0x21,0xFF,0x0B); str('NETSCAPE2.0'); out.push(0x03,0x01,0x00,0x00,0x00); // 무한 루프

  const map=makeMapper(pal), n=w*h;
  let displayed=null;                                // 현재 화면에 누적된 인덱스
  frames.forEach((f,fi)=>{
    const d=f.rgba, idx=new Uint8Array(n);
    for(let i=0,pi=0;i<n;i++,pi+=4) idx[i]=map(d[pi],d[pi+1],d[pi+2]);
    let useTransp=false;
    if(fi>0){                                        // 이전과 동일 픽셀은 투명 처리
      for(let i=0;i<n;i++){ if(idx[i]==displayed[i]){ idx[i]=transp; useTransp=true; } else displayed[i]=idx[i]; }
    } else displayed=idx.slice();
    const cs=Math.max(2,Math.round(f.delay/10));     // ms→1/100s
    // graphic control: disposal=1(유지), 필요 시 투명 플래그
    out.push(0x21,0xF9,0x04, useTransp?0x05:0x04, cs&0xff,(cs>>8)&0xff, useTransp?transp:0, 0x00);
    out.push(0x2C, 0,0, 0,0, w&0xff,(w>>8)&0xff, h&0xff,(h>>8)&0xff, 0x00); // image descriptor
    lzwEncode(idx, minCode, out);
  });
  out.push(0x3B);
  return new Uint8Array(out);
}

async function transcodeGif(file){
  let gif=decodeGifResized(await file.arrayBuffer());
  gif.frames=capFrames(gif.frames, SAVER_MAX_FRAMES);
  const pal=buildPalette(gif.frames, 255);           // 1색은 투명 예약
  const bytes=encodeGif(gif.w, gif.h, gif.frames, pal);
  return {blob:new Blob([bytes],{type:'image/gif'}), name:'saver.gif'};
}

// 업로드 직전 파일 종류 판정 → 적절한 트랜스코더 적용
async function prepareSaverImage(file){
  return (await isGifFile(file)) ? transcodeGif(file) : transcodeStill(file);
}

async function uploadSaverImage(){
  const inp=document.getElementById('saver-file');
  if(!inp.files||!inp.files[0]){alert(t('saver_choose'));return;}
  const file=inp.files[0];
  const st=document.getElementById('saver-status');
  st.textContent=t('saver_proc');
  try{
    const prepared=await prepareSaverImage(file);   // 클라이언트에서 리사이즈/압축
    const fd=new FormData();fd.append('image',prepared.blob,prepared.name);
    st.textContent=t('saver_up');
    const r=await fetch('/api/saver/image',{method:'POST',body:fd});
    if(r.ok){st.textContent=t('saver_ok');loadSaver();}
    else    {st.textContent=t('saver_fail');}
  }catch(e){console.error('[saver] transcode/upload 실패',e);st.textContent=t('saver_fail');}
}
async function deleteSaverImage(){
  await fetch('/api/saver/image',{method:'DELETE'});
  loadSaver();
}

window.addEventListener('DOMContentLoaded',async ()=>{
  applyLang();
  recipes=await loadR();   // 보드 플래시에서 레시피 로드
  renderCatTabs();renderRecipeList();loadSettings();loadSaver();fetchStatus();onCycleToggle();
});
</script>
</body>
</html>
)HTML";
