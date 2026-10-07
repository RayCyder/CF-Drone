#pragma once

#if WEB_RC_ENABLED
const char wifiConfigHtml[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="zh-CN"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CF-Drone Wi-Fi 配置</title>
<style>
body{margin:0;background:#252525;color:#fff;font:16px Arial,"Microsoft YaHei",sans-serif;padding:24px}
main{max-width:440px;margin:7vh auto;background:#333;padding:24px;border-radius:14px;box-shadow:0 8px 30px #111}
h1{font-size:1.35rem;margin-top:0}p{color:#ccc;line-height:1.55;font-size:.92rem}
label{display:block;margin:18px 0 6px}input{box-sizing:border-box;width:100%;padding:12px;border-radius:8px;border:1px solid #777;background:#222;color:#fff;font-size:1rem}
select{box-sizing:border-box;width:100%;padding:12px;border-radius:8px;border:1px solid #777;background:#222;color:#fff;font-size:1rem}
button{margin-top:12px;width:100%;padding:13px;border:0;border-radius:8px;background:#147efb;color:#fff;font-size:1rem;font-weight:bold}
a{display:inline-block;margin-top:18px;color:#9fc7ff}#status{min-height:1.5em;color:#ffd27a}
#boot-motor-check,#events{margin-top:22px;padding-top:14px;border-top:1px solid #555}#boot-motor-check h2,#events h2{font-size:1rem;margin:0 0 8px}.motor-check-card{padding:10px;background:#222;border-radius:8px;color:#ddd;font-size:.88rem;line-height:1.55}.motor-check-card strong{display:block;color:#fff}.motor-check-results{margin-top:6px;color:#aaa;font:12px/1.5 monospace}#refresh-motor-check{width:auto;padding:8px 12px;font-size:.85rem}#event-state{color:#aaa;font-size:.85rem}#event-list{max-height:220px;overflow:auto;padding:8px;background:#222;border-radius:8px;font:12px/1.5 monospace;white-space:pre-wrap;overflow-wrap:anywhere}.event-actions{display:flex;gap:8px}.event-actions button{flex:1;padding:9px;font-size:.88rem}
#saved-profile-list{list-style:none;padding:0;margin:8px 0}.saved-profile{display:flex;align-items:center;justify-content:space-between;gap:10px;background:#30353c;border-radius:7px;padding:8px 10px;margin:6px 0}.profile-actions{display:flex;gap:6px}.saved-profile button{width:auto;margin:0;padding:7px 10px;font-size:.85rem}.saved-profile .connect-profile{background:#147efb}.saved-profile .remove-profile{background:#8b3434}.muted{color:#aaa;font-size:.85rem}
</style></head><body><main>
<h1>无人机 Wi-Fi 配置</h1>
<p><strong>当前处于 Drone_WiFi 热点配置入口。</strong>保存下方网络后，设备会切换到 Wi-Fi 连接模式并重启；之后请使用路由器分配的地址访问。</p>
<p>最多保存 4 个 2.4 GHz 网络；无人机按优先顺序连接，当前添加或更新的网络会排在第一位。删除全部已保存网络后，设备会重启并恢复 Drone_WiFi。</p>
<form id="wifi-form"><label for="ssid">Wi-Fi 名称（SSID）</label>
<select id="networks" aria-label="附近的 Wi-Fi 网络"><option value="">点击扫描附近网络…</option></select>
<button id="scan" type="button">扫描 Wi-Fi</button>
<input id="ssid" name="ssid" maxlength="32" autocomplete="off" required>
<label for="password">Wi-Fi 密码</label>
<input id="password" name="password" type="password" maxlength="63" autocomplete="new-password">
<button type="submit">保存网络并连接</button></form><div id="status" role="status"></div>
<section id="saved-profiles"><h2>已保存网络</h2><ul id="saved-profile-list"><li class="muted">正在读取…</li></ul><div id="profile-capacity" class="muted"></div></section>
<section id="boot-motor-check"><h2>开机电机自检</h2><div id="motor-check-state" class="motor-check-card"><strong>正在读取自检结果…</strong></div><button id="refresh-motor-check" type="button">刷新自检结果</button></section>
<section id="events"><h2>启动与 Wi-Fi 自检日志</h2><div id="event-state">正在连接事件流…</div><pre id="event-list" aria-live="polite"></pre><div class="event-actions"><button id="download-events" type="button" disabled>下载日志</button><button id="clear-events" type="button">清空显示</button></div></section>
<a href="/">返回遥控页面</a></main>
<script>
const statusEl=document.getElementById('status'), networkList=document.getElementById('networks');
networkList.addEventListener('change',()=>{if(networkList.value)document.getElementById('ssid').value=networkList.value;});
document.getElementById('scan').addEventListener('click',async()=>{const button=document.getElementById('scan');button.disabled=true;networkList.replaceChildren(new Option('正在扫描附近网络…',''));statusEl.textContent='';try{let data;do{const r=await fetch(data?'/wifi/scan':'/wifi/scan?refresh=1');data=await r.json();if(data.state==='scanning')await new Promise(resolve=>setTimeout(resolve,700));}while(data.state==='scanning');networkList.replaceChildren();if(data.state!=='done')throw new Error(data.message||'扫描失败');if(!data.networks.length){networkList.add(new Option('未发现网络，请手动输入 SSID',''));}else{networkList.add(new Option('选择附近的 Wi-Fi 网络…',''));for(const n of data.networks){const suffix=(n.open?'开放':'需密码')+' · '+n.rssi+' dBm';networkList.add(new Option(n.ssid+' ('+suffix+')',n.ssid));}}statusEl.textContent='扫描完成；隐藏网络请手动填写 SSID。';}catch(_){networkList.replaceChildren(new Option('扫描失败，请重试或手动输入',''));statusEl.textContent='无法扫描网络，请重试或手动输入 SSID。';}finally{button.disabled=false;}});
async function refreshSavedProfiles(){const list=document.getElementById('saved-profile-list'),capacity=document.getElementById('profile-capacity');try{const r=await fetch('/wifi/profiles',{cache:'no-store'}),data=await r.json();list.replaceChildren();if(!data.profiles.length){list.append(Object.assign(document.createElement('li'),{className:'muted',textContent:'尚无已保存网络'}));}for(const profile of data.profiles){const item=document.createElement('li');item.className='saved-profile';const label=document.createElement('span');label.textContent=profile.priority+'. '+profile.ssid;const actions=document.createElement('div');actions.className='profile-actions';const connect=document.createElement('button');connect.type='button';connect.className='connect-profile';connect.textContent='切换';connect.onclick=async()=>{connect.disabled=true;remove.disabled=true;statusEl.textContent='正在切换到 '+profile.ssid+'…';try{const result=await fetch('/wifi/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({ssid:profile.ssid})}),reply=await result.json();statusEl.textContent=reply.message||'切换失败';statusEl.style.color=result.ok?'#8fe3a0':'#ff8b8b';}catch(_){statusEl.textContent='连接已断开；飞控可能正在重启，请连接目标 Wi-Fi 后重新访问。';statusEl.style.color='#ffd27a';}finally{connect.disabled=false;remove.disabled=false;}};const remove=document.createElement('button');remove.type='button';remove.className='remove-profile';remove.textContent='删除';remove.onclick=async()=>{connect.disabled=true;remove.disabled=true;try{const result=await fetch('/wifi/remove',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({ssid:profile.ssid})}),reply=await result.json();statusEl.textContent=reply.message||'删除失败';statusEl.style.color=result.ok?'#8fe3a0':'#ff8b8b';if(result.ok)await refreshSavedProfiles();}catch(_){statusEl.textContent='删除失败，连接中断';}finally{connect.disabled=false;remove.disabled=false;}};actions.append(connect,remove);item.append(label,actions);list.append(item);}capacity.textContent=`${data.profiles.length}/${data.limit} 个网络已保存 · 独立闪存双槽 ${data.storage_used}/${data.storage_total} 字节`;}catch(_){list.replaceChildren(Object.assign(document.createElement('li'),{className:'muted',textContent:'无法读取已保存网络'}));capacity.textContent='';}}
const motorCheckState=document.getElementById('motor-check-state');
const motorCheckReason={complete:'检测完成',boot_preflight_failed:'启动条件不满足',preflight_failed:'检测条件中途失效',baseline_unstable:'机体静止基线不稳定',baseline_capture_failed:'静止基线采集失败',motor_capture_failed:'电机采集启动失败',motor_test_rejected:'电机试转被安全策略拒绝',insufficient_imu_samples:'IMU 样本不足',safety_state_changed:'检测期间安全状态变化',motor_test_timeout:'电机试转超时',boot_timeout:'开机检测总超时',final_stop_failed:'电机停止确认失败'};
async function refreshMotorCheck(){const button=document.getElementById('refresh-motor-check');button.disabled=true;try{const r=await fetch('/vibration-calibration/status',{cache:'no-store'}),data=await r.json();if(!r.ok)throw new Error('状态读取失败');const active=['boot_wait','queued','baseline','running','settling'].includes(data.state);const title=data.state==='complete'?'四电机开机自检已完成':active?'开机电机自检正在进行':'开机电机自检未完成';const reason=motorCheckReason[data.reason]||data.reason||'未知';const motors=(data.motors||[]).map(m=>m.name+': '+(m.response==='detected'?'检测到响应':'未确认响应')+' ('+m.samples+' 样本)').join('<br>');motorCheckState.innerHTML='<strong>'+title+'</strong><span>状态：'+reason+'；已完成 '+data.step+'/4 路。</span>'+(motors?'<div class="motor-check-results">'+motors+'</div>':'');}catch(_){motorCheckState.innerHTML='<strong>无法读取开机电机自检结果</strong><span>请刷新页面或检查飞控状态。</span>';}finally{button.disabled=false;}}
document.getElementById('refresh-motor-check').onclick=refreshMotorCheck;
document.getElementById('wifi-form').addEventListener('submit',async e=>{e.preventDefault();statusEl.textContent='正在安全保存网络…';try{const r=await fetch('/wifi/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(e.target))});const d=await r.json();statusEl.textContent=d.message||'保存失败';statusEl.style.color=r.ok?'#8fe3a0':'#ff8b8b';if(r.ok){document.getElementById('password').value='';await refreshSavedProfiles();}}catch(_){statusEl.textContent='连接中断；请查看下方事件日志，确认飞控是否正在重启。';statusEl.style.color='#ffd27a';}});
const eventState=document.getElementById('event-state'),eventList=document.getElementById('event-list'),downloadEvents=document.getElementById('download-events');let events=[],seenEvents=new Set();
let eventSource=null;
function connectEventStream(){
if(eventSource)return;
const stream=eventSource=new EventSource(location.protocol+'//'+location.hostname+':81/stream');
stream.onopen=()=>{if(eventSource===stream)eventState.textContent='事件流已连接；启动和 Wi-Fi 状态会实时显示';};stream.onerror=()=>{if(eventSource===stream)eventState.textContent='事件流断开，浏览器正在自动重连；已收到的日志仍保留在此页面';};
stream.addEventListener('system-log',e=>{if(eventSource!==stream)return;if(e.lastEventId&&seenEvents.has(e.lastEventId))return;if(e.lastEventId)seenEvents.add(e.lastEventId);const parts=e.data.split('|');const line=(parts[0]||'?')+' ms  ['+(parts[1]||'SYSTEM')+'] '+parts.slice(2).join('|');events.push(line);if(events.length>500)events.shift();eventList.textContent=events.join('\n');eventList.scrollTop=eventList.scrollHeight;downloadEvents.disabled=events.length===0;});
}
function closeEventStream(){if(!eventSource)return;const stream=eventSource;eventSource=null;stream.close();}
window.addEventListener('pagehide',closeEventStream);
window.addEventListener('pageshow',connectEventStream);
connectEventStream();
downloadEvents.onclick=()=>{if(!events.length)return;const link=document.createElement('a');link.href=URL.createObjectURL(new Blob([events.join('\n')+'\n'],{type:'text/plain;charset=utf-8'}));link.download='cf-drone-system-log.txt';link.click();URL.revokeObjectURL(link.href);};
document.getElementById('clear-events').onclick=()=>{events=[];seenEvents.clear();eventList.textContent='';downloadEvents.disabled=true;};
refreshSavedProfiles();
refreshMotorCheck();
</script>
</body></html>
)rawliteral";
#endif
