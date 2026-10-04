// ================================================================
//  web_multi.h — 멀티 보드 제어 페이지 (/multi)
//  ----------------------------------------------------------------
//  폰 하나로 여러 대 운용. 각 보드의 기존 REST API를 브라우저가 직접 호출
//  (보드 간 통신 없음 → 펌웨어 추가 로직 없음, 한 대가 꺼져도 나머지 무관).
//  구성: 1번 보드 AP에 2·3번 보드가 STA(고정 IP)로 접속 → 폰은 1번 AP에만 붙음.
//  보드 목록은 폰 브라우저 localStorage에 저장 (보드별 설정 아님).
// ================================================================
#pragma once
#include <Arduino.h>
#include <pgmspace.h>

const char MULTI_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0,viewport-fit=cover">
<title>멀티 보드</title>
<style>
:root{--bg:#f4f2ee;--card:#fff;--fg:#1d1b18;--muted:#8a857c;--border:#e6e2da;--accent:#d9730d;--ok:#2f9e44;--bad:#d6336c;--blue:#1c7ed6}
@media (prefers-color-scheme:dark){:root{--bg:#141311;--card:#1f1d1a;--fg:#ece8e1;--muted:#8f897f;--border:#2e2b27}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;font-size:14px;padding:16px;padding-bottom:40px}
h1{font-size:20px;margin:0 0 12px;display:flex;align-items:center;gap:8px}
h1 a{font-size:12px;color:var(--muted);text-decoration:none;margin-left:auto}
.card{background:var(--card);border:1px solid var(--border);border-radius:14px;padding:14px;margin-bottom:12px}
.row{display:flex;align-items:center;gap:8px;flex-wrap:wrap}
.title{font-weight:700;font-size:15px}
.muted{color:var(--muted);font-size:12px}
.dot{width:9px;height:9px;border-radius:50%;background:var(--muted);flex:none}
.dot.on{background:var(--ok)}.dot.off{background:var(--bad)}
.rpm{font-size:30px;font-weight:800;line-height:1}
.chip{font-size:11px;font-weight:700;padding:3px 8px;border-radius:99px;background:var(--border);color:var(--fg)}
.chip.fwd{background:var(--ok);color:#fff}.chip.rev{background:var(--blue);color:#fff}
.chip.rest{background:var(--accent);color:#fff}.chip.warn{background:var(--bad);color:#fff}
.btn{border:0;border-radius:10px;padding:9px 12px;font-weight:700;font-size:13px;cursor:pointer;background:var(--border);color:var(--fg)}
.btn.danger{background:var(--bad);color:#fff}.btn.primary{background:var(--accent);color:#fff}
.btn:disabled{opacity:.4}
.btn.big{width:100%;padding:14px;font-size:15px}
input,select{font:inherit;padding:8px 10px;border-radius:10px;border:1px solid var(--border);background:var(--bg);color:var(--fg);min-width:0}
.grow{flex:1}
.bar{height:5px;border-radius:3px;background:var(--border);overflow:hidden;margin-top:6px}
.bar>i{display:block;height:100%;background:var(--accent)}
.x{background:none;border:0;color:var(--muted);font-size:18px;cursor:pointer;margin-left:auto}
.sel{display:flex;gap:6px;flex-wrap:wrap;margin:8px 0}
.sel label{display:flex;align-items:center;gap:5px;padding:6px 10px;border:1px solid var(--border);border-radius:10px}
#msg{min-height:18px;margin-top:8px}
</style>
</head>
<body>
<h1>멀티 보드 <a href="/">← 단일 화면</a></h1>

<button class="btn danger big" onclick="stopAll()">■ 전체 정지</button>
<div id="boards" style="margin-top:12px"></div>

<div class="card">
  <div class="title">레시피 동시 시작</div>
  <div class="muted">이 보드에 저장된 레시피를 선택한 보드들에서 시작합니다.</div>
  <div class="row" style="margin-top:8px">
    <select id="cat" onchange="fillRecipes()"></select>
    <select id="rec" class="grow"></select>
  </div>
  <div class="sel" id="targets"></div>
  <button class="btn primary big" onclick="startSelected()">▶ 선택 보드에서 시작</button>
  <div id="msg" class="muted"></div>
</div>

<div class="card">
  <div class="title">보드 추가</div>
  <div class="muted">각 보드의 IP 입력 (예: 192.168.4.12). 보드 설정에서 고정 IP를 지정하세요.</div>
  <div class="row" style="margin-top:8px">
    <input id="newHost" class="grow" placeholder="192.168.4.12" inputmode="decimal">
    <button class="btn" onclick="addBoard()">추가</button>
  </div>
</div>

<script>
const LS='mb_boards';
let hosts;
try{hosts=JSON.parse(localStorage.getItem(LS))}catch(e){}
if(!Array.isArray(hosts)||!hosts.length)hosts=[location.host];
const st={};   // host → {d, online, busy}
let recipes={};

const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const url=(h,p)=>'http://'+h+p;
function save(){try{localStorage.setItem(LS,JSON.stringify(hosts))}catch(e){}}
function mmss(s){s=Math.max(0,s|0);return String(Math.floor(s/60)).padStart(2,'0')+':'+String(s%60).padStart(2,'0')}

async function req(h,path,opt={},ms=2000){
  const ac=new AbortController(),to=setTimeout(()=>ac.abort(),ms);
  try{return await fetch(url(h,path),{...opt,signal:ac.signal})}finally{clearTimeout(to)}
}
// 보드별 폴링: 응답 완료 후 1초 뒤 다음 요청 (적체 방지)
function poll(h){
  if(!hosts.includes(h))return;
  const s=st[h]||(st[h]={});
  (document.hidden?Promise.resolve():req(h,'/api/status').then(r=>r.json()).then(d=>{s.d=d;s.online=true;}).catch(()=>{s.online=false;}))
    .finally(()=>{render();setTimeout(()=>poll(h),1000)});
}
// 정지류: 반복 전송해도 결과 동일 → 재시도
async function stopReq(h,path){
  for(let i=0;i<5;i++){try{const r=await req(h,path,{method:'POST'},1500);if(r.ok)return true}catch(e){}await new Promise(r=>setTimeout(r,200))}
  return false;
}
async function stopAll(){
  const res=await Promise.all(hosts.map(h=>stopReq(h,'/api/stop')));
  const bad=hosts.filter((h,i)=>!res[i]);
  msg(bad.length?'⚠ 정지 실패: '+bad.join(', ')+' — 본체 버튼으로 정지하세요':'전체 정지 완료');
}
async function cmd(h,path){
  const ok=path==='/api/stop'?await stopReq(h,path):await req(h,path,{method:'POST'}).then(r=>r.ok).catch(()=>false);
  if(!ok)msg('⚠ '+(st[h]?.d?.name||h)+': 명령 전달 실패');
}

function render(){
  document.getElementById('boards').innerHTML=hosts.map(h=>{
    const s=st[h]||{},d=s.d;
    const dotc=s.online===undefined?'':s.online?'on':'off';
    if(!d)return `<div class="card"><div class="row"><span class="dot ${dotc}"></span><span class="title">${esc(h)}</span><span class="muted">${s.online===false?'연결 안 됨':'연결 중…'}</span>${h!==location.host?`<button class="x" onclick="rmBoard('${esc(h)}')">×</button>`:''}</div></div>`;
    const dir={FWD:['fwd','정방향'],REV:['rev','역방향'],REST:['rest','휴지'],STOP:['','감속'],IDLE:['','정지']}[d.motorDir]||['',d.motorDir];
    const mode=d.waitConfirm?'확인 대기':d.recipePause?'일시정지':d.recipeRun?'레시피':d.manualMode?'수동':'대기';
    const guard=d.guard&&d.guard!=='ok'?`<span class="chip warn">${d.guard==='locked'?'잠금':'노이즈'}</span>`:'';
    const temp=d.tempFault?'온도 오류':d.temperature>-100?d.temperature.toFixed(1)+' °C':'';
    let rec='';
    if(d.recipeRun){
      const pct=d.stepDurSec?Math.min(100,(d.stepDurSec-d.stepRemSec)*100/d.stepDurSec):0;
      rec=`<div style="margin-top:10px"><div class="row"><b>${esc(d.recipeName)}</b><span class="muted">${d.stepIdx+1}/${d.totalSteps} · ${esc(d.stepName)}</span><span style="margin-left:auto;font-weight:700">${mmss(d.stepRemSec)}</span></div><div class="bar"><i style="width:${pct}%"></i></div></div>`;
    }
    const active=d.recipeRun||d.recipePause||d.waitConfirm;
    return `<div class="card">
      <div class="row"><span class="dot ${dotc}"></span><span class="title">${esc(d.name)}</span><span class="muted">#${d.boardId} · ${esc(h)} · ${esc(d.fw)}</span>${guard}${h!==location.host?`<button class="x" onclick="rmBoard('${esc(h)}')">×</button>`:''}</div>
      <div class="row" style="margin-top:10px"><span class="rpm">${d.motorRpm}</span><span class="muted">RPM</span><span class="chip ${dir[0]}">${dir[1]}</span><span class="chip">${mode}</span><span class="muted" style="margin-left:auto">${temp}</span></div>
      ${rec}
      <div class="row" style="margin-top:10px">
        <button class="btn danger" onclick="cmd('${esc(h)}','/api/stop')">정지</button>
        ${active&&!d.waitConfirm?`<button class="btn" onclick="cmd('${esc(h)}','/api/pause')">${d.recipePause?'재개':'일시정지'}</button>`:''}
        ${d.waitConfirm?`<button class="btn primary" onclick="cmd('${esc(h)}','/api/confirm')">다음 단계</button>`:''}
        <a class="btn" style="margin-left:auto;text-decoration:none" href="${esc(url(h,'/'))}">열기</a>
      </div></div>`;
  }).join('');
  renderTargets();
}
function renderTargets(){
  const box=document.getElementById('targets'),prev={};
  box.querySelectorAll('input').forEach(i=>prev[i.value]=i.checked);
  const html=hosts.map(h=>{const n=st[h]?.d?.name||h,on=st[h]?.online;
    return `<label><input type="checkbox" value="${esc(h)}" ${prev[h]!==false?'checked':''} ${on?'':'disabled'}>${esc(n)}</label>`}).join('');
  if(box.dataset.k!==html.replace(/checked/g,'')){box.innerHTML=html;box.dataset.k=html.replace(/checked/g,'')}
}
function addBoard(){
  const h=document.getElementById('newHost').value.trim().replace(/^https?:\/\//,'').replace(/\/.*$/,'');
  if(!h||hosts.includes(h))return;
  hosts.push(h);save();document.getElementById('newHost').value='';poll(h);render();
}
function rmBoard(h){hosts=hosts.filter(x=>x!==h);delete st[h];save();render();}

async function loadRecipes(){
  try{recipes=await (await req(location.host,'/api/recipes/load',{},5000)).json()}catch(e){recipes={}}
  const cat=document.getElementById('cat');
  cat.innerHTML=Object.keys(recipes).map(c=>`<option>${esc(c)}</option>`).join('');
  fillRecipes();
}
function fillRecipes(){
  const list=recipes[document.getElementById('cat').value]||[];
  document.getElementById('rec').innerHTML=list.map((r,i)=>`<option value="${i}">${esc(r.name)} (${(r.steps||[]).length}단계)</option>`).join('');
}
async function startSelected(){
  const r=(recipes[document.getElementById('cat').value]||[])[document.getElementById('rec').value];
  if(!r||!(r.steps||[]).length){msg('레시피를 선택하세요');return}
  const tg=[...document.querySelectorAll('#targets input:checked')].map(i=>i.value);
  if(!tg.length){msg('시작할 보드를 선택하세요');return}
  const busy=tg.filter(h=>{const d=st[h]?.d;return d&&(d.recipeRun||d.manualMode||d.motorDir!=='IDLE')});
  if(!confirm(`"${r.name}"을(를) ${tg.length}대에서 시작할까요?`+(busy.length?`\n\n⚠ 동작 중인 보드(${busy.length}대)는 현재 작업이 중단됩니다.`:'')))return;
  const body=JSON.stringify({recipeName:r.name,steps:r.steps.map(s=>({name:s.name,speedRpm:Math.min(80,Math.max(1,s.speedRpm||50)),durationSec:Math.max(1,s.durSec),rotIntSec:Math.max(5,s.rotIntSec)}))});
  const res=await Promise.all(tg.map(h=>req(h,'/api/start',{method:'POST',headers:{'Content-Type':'application/json'},body},4000).then(x=>x.ok).catch(()=>false)));
  const bad=tg.filter((h,i)=>!res[i]);
  msg(bad.length?'⚠ 시작 실패: '+bad.map(h=>st[h]?.d?.name||h).join(', '):`${tg.length}대 시작`);
}
function msg(t){document.getElementById('msg').textContent=t}

hosts.forEach(poll);loadRecipes();render();
</script>
</body>
</html>
)HTML";
