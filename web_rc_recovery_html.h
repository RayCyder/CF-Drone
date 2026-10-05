#pragma once

#if WEB_RC_ENABLED
const char webRCRecoveryHtml[] PROGMEM = R"rawliteral(
<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no"><title>飞行控制恢复</title>
<style>*{box-sizing:border-box;touch-action:none}body{margin:0;background:#20242a;color:#fff;font:14px system-ui;padding:12px}.top{display:flex;justify-content:space-between;gap:8px;align-items:center}.warn{color:#ffd27a}.pads{display:flex;gap:12px;justify-content:center;margin:14px 0}.pad{width:min(42vw,42vh);height:min(42vw,42vh);max-width:260px;max-height:260px;border:2px solid #667;border-radius:50%;position:relative;background:#15191e}.knob{position:absolute;left:50%;top:50%;width:25%;height:25%;border-radius:50%;background:#ddd;transform:translate(-50%,-50%)}.actions{display:flex;gap:8px;justify-content:center;flex-wrap:wrap}button{border:0;border-radius:8px;padding:12px 18px;background:#3d566e;color:#fff;font-weight:700}button.danger{background:#a32e2e}button:disabled{opacity:.45}</style></head>
<body><div class="top"><strong>飞行控制恢复页</strong><span id="state" class="warn">正在恢复控制权…</span></div><p>此轻量页面用于飞行中误刷新。触摸摇杆后才发送控制量；左杆松手回悬停油门，右杆松手回中。</p>
<div class="pads"><div id="joystick-left" class="pad"><div class="knob"></div></div><div id="joystick-right" class="pad"><div class="knob"></div></div></div>
<div class="actions"><button onclick="emergency(3)">迫降</button><button onclick="emergency(1)">上锁</button><button class="danger" onclick="emergency(2)">急停</button><button id="full" onclick="location.reload()" disabled>返回完整页面</button></div>
<script>
let lease='',stop='',active=false,inFlight=null,pending=false,heartbeat=null;
let hoverRaw=0,left={x:0,y:0},right={x:0,y:0},values={th:0,r:0,p:0,y:0};
const state=document.getElementById('state');
try{stop=localStorage.getItem('cfDroneStopToken')||''}catch(_){}
if(!stop){try{stop=sessionStorage.getItem('cfDroneStopToken')||''}catch(_){}}
function timeoutFetch(url,options={},ms=3000){const c=new AbortController(),t=setTimeout(()=>c.abort(),ms);return fetch(url,{...options,signal:c.signal}).finally(()=>clearTimeout(t))}
async function acquire(){
  if(lease)return true;
  try{
    const r=await timeoutFetch('/web_rc/lease'+(stop?'?stop='+encodeURIComponent(stop):''),{method:'POST',cache:'no-store'},3500);
    const data=await r.json().catch(()=>({}));
    if(!r.ok||!data.lease){state.textContent=data.error==='web_rc_flight_takeover_forbidden'?'飞行中禁止此页面接管':'无法恢复控制权';return false}
    lease=data.lease;stop=data.stop||stop;
    try{localStorage.setItem('cfDroneStopToken',stop)}catch(_){} try{sessionStorage.setItem('cfDroneStopToken',stop)}catch(_){}
    state.textContent='控制权已恢复';return true;
  }catch(_){state.textContent='连接超时，正在重试';return false}
}
async function post(data,retry=true){
  if(!await acquire())return false;
  const used=lease,dataWithLease={...data,lease:used};
  try{
    const r=await timeoutFetch('/web_rc',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(dataWithLease)});
    const body=await r.json().catch(()=>({}));
    if(r.status===409&&(body.error==='web_rc_lease_expired'||body.error==='web_rc_lease_required')&&retry&&lease===used){lease='';return post(data,false)}
    if(r.status===409&&body.error==='web_rc_lease_in_use'){lease='';state.textContent='控制权已被其他页面接管'}
    return r.ok;
  }catch(_){state.textContent='控制请求超时';return false}
}
function snapshot(){values={th:left.y,r:right.x,p:right.y,y:left.x};return {t:1,th:Math.round(values.th),r:Math.round(values.r),p:Math.round(values.p),y:Math.round(values.y),ts:performance.now()}}
function sendLatest(){
  if(!active)return Promise.resolve(false);pending=true;if(inFlight)return inFlight;
  inFlight=(async()=>{let ok=false;while(pending){pending=false;ok=await post(snapshot())}return ok})().finally(()=>{inFlight=null});return inFlight;
}
function setKnob(pad,x,y){pad.querySelector('.knob').style.transform=`translate(calc(-50% + ${x}px),calc(-50% + ${y}px))`}
function updatePointer(e,side){
  const pad=e.currentTarget,rect=pad.getBoundingClientRect(),radius=rect.width*.375;
  let x=e.clientX-rect.left-rect.width/2,y=e.clientY-rect.top-rect.height/2,d=Math.hypot(x,y);if(d>radius){x=x/d*radius;y=y/d*radius}
  setKnob(pad,x,y);const target=side==='left'?left:right;target.x=x/radius*100;target.y=-y/radius*100;sendLatest();
}
for(const side of ['left','right']){const pad=document.getElementById('joystick-'+side);pad.onpointerdown=e=>{active=true;pad.setPointerCapture(e.pointerId);updatePointer(e,side)};pad.onpointermove=e=>{if(pad.hasPointerCapture(e.pointerId))updatePointer(e,side)};pad.onpointerup=pad.onpointercancel=e=>{if(side==='left'){left={x:0,y:hoverRaw}}else{right={x:0,y:0}}setKnob(pad,0,side==='left'?-hoverRaw/100*pad.clientWidth*.375:0);sendLatest()}}
function release(){if(!active)return;left={x:0,y:hoverRaw};right={x:0,y:0};sendLatest();active=false}
async function emergency(button){
  if(!stop){state.textContent='没有有效停机凭证';return}const action=button===2?'kill':button===3?'land':'lock';
  fetch(`${location.protocol}//${location.hostname}:82/${action}?s=${encodeURIComponent(stop)}`,{method:'POST',mode:'no-cors',keepalive:true}).catch(()=>{});
  await timeoutFetch('/web_rc',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({t:2,b:button,s:1,stop,ts:performance.now()})}).catch(()=>{});
}
async function refresh(){try{const r=await timeoutFetch('/web_rc/status',{cache:'no-store'}),d=await r.json();if(Number.isFinite(Number(d.hover_throttle_pct)))hoverRaw=Math.max(-100,Math.min(100,Number(d.hover_throttle_pct)*2-100));document.getElementById('full').disabled=d.armed===true;state.textContent=d.armed?'飞行中，轻量控制可用':'飞控已上锁，可返回完整页面'}catch(_){}}
document.addEventListener('visibilitychange',()=>{if(document.hidden)release()});window.addEventListener('pagehide',release);window.addEventListener('blur',release);
acquire();refresh();setInterval(()=>{if(!heartbeat)heartbeat=post({t:4,ts:performance.now()}).finally(()=>heartbeat=null)},2000);setInterval(refresh,2000);
</script></body></html>
)rawliteral";
static_assert(sizeof(webRCRecoveryHtml) <= 16384, "Flight recovery page must stay small");
#endif
