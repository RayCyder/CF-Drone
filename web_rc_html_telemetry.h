#pragma once

#if WEB_RC_ENABLED
const char telemetryHtml[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="zh-CN"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>飞行实时日志</title>
<style>body{margin:0;padding:20px;background:#20242a;color:#eef2f6;font:16px Arial,"Microsoft YaHei",sans-serif}main{max-width:720px;margin:auto}h1{font-size:1.4rem}.state{padding:10px;border-radius:8px;background:#343b44}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;margin:14px 0}.card{background:#343b44;padding:12px;border-radius:8px}.label{color:#aeb9c5;font-size:.85rem}.value{font:1.15rem monospace;margin-top:6px;overflow-wrap:anywhere}button{padding:11px 14px;border:0;border-radius:7px;background:#1683ff;color:white;font-weight:bold;margin:4px}button:disabled{opacity:.5}a{color:#9dcaff}.note{color:#bdc7d2;font-size:.9rem;line-height:1.5}</style></head>
<body><main><h1>飞行实时日志</h1><div id="state" class="state">正在连接飞控…</div>
<div class="grid" id="values"></div><p>已收到 <span id="received">0</span> 条 · 捕获 <span id="captured">0</span> 条</p>
<button id="capture">开始捕获</button><button id="download" disabled>下载当前页面 CSV</button><button id="snapshot">下载故障快照</button><button id="resume-log">恢复机载记录</button>
<p class="note" id="log-status">机载日志状态读取中…</p>
<p class="note">实时遥测以 2 Hz 推送。网页捕获数据保存在当前浏览器内存中；“故障快照”下载飞控冻结的机载 100 Hz CSV。电机数据是输出指令，不是转速反馈。</p><a href="/">返回遥控页面</a></main>
<script>
const stateEl=document.getElementById('state'),valuesEl=document.getElementById('values');
const receivedEl=document.getElementById('received'),capturedEl=document.getElementById('captured');
const selectedMetrics=['attitude.x','attitude.y','attitude.z','rates.x','rates.y','rates.z','gyro_x','gyro_y','gyro_z','acc_x','acc_y','acc_z','battery_v','motor_rl','motor_rr','motor_fr','motor_fl'];
let headers=[],metricValues=[],rows=[],capturing=false,received=0;
const source=new EventSource(location.protocol+'//'+location.hostname+':81/stream');
source.onopen=()=>stateEl.textContent='已连接 · 实时接收中';source.onerror=()=>stateEl.textContent='连接中断，浏览器正在自动重连…';
// Telemetry arrives at 2 Hz: build cards on schema changes, then update cached value nodes.
source.addEventListener('schema',e=>{
  headers=e.data.split(',');
  const headerIndices=new Map(headers.map((name,index)=>[name,index]));
  const fragment=document.createDocumentFragment();
  metricValues=[];
  for(const name of selectedMetrics){
    const index=headerIndices.get(name);
    if(index===undefined)continue;
    const card=document.createElement('div');card.className='card';
    const label=document.createElement('div');label.className='label';label.textContent=name;
    const value=document.createElement('div');value.className='value';value.textContent='—';
    card.append(label,value);fragment.append(card);metricValues.push({index,value});
  }
  valuesEl.replaceChildren(fragment);
});
source.addEventListener('sample',e=>{
  const values=e.data.split(',');
  received++;receivedEl.textContent=received;
  if(capturing&&rows.length<10000)rows.push(e.lastEventId+','+e.data);
  capturedEl.textContent=rows.length;
  for(const metric of metricValues)metric.value.textContent=values[metric.index]??'—';
});
document.getElementById('capture').onclick=()=>{capturing=!capturing;document.getElementById('capture').textContent=capturing?'停止捕获':'继续捕获';document.getElementById('download').disabled=rows.length===0;};
document.getElementById('download').onclick=()=>{if(!headers.length||!rows.length)return;const csv='sequence,'+headers.join(',')+'\n'+rows.join('\n')+'\n';const link=document.createElement('a');link.href=URL.createObjectURL(new Blob([csv],{type:'text/csv'}));link.download='flight-telemetry.csv';link.click();URL.revokeObjectURL(link.href);};
const logStateName=s=>({ROLLING:'滚动记录',POST_TRIGGER:'采集故障后数据',FROZEN:'快照已保留'}[s]||s||'未知');
function renderLogStatus(d,prefix='机载日志'){document.getElementById('log-status').textContent=`${prefix} ${logStateName(d.state)} · 批次 ${d.generation} · ${d.rowCount} 行 · 漏采 ${d.missedSamples||0} · 故障标记 0x${Number(d.reasonMask||0).toString(16)}`;}
async function refreshLogStatus(){try{const r=await fetch('/logs/status',{cache:'no-store'});const d=await r.json();if(!r.ok)throw new Error(d.error||'状态不可用');renderLogStatus(d);}catch(_){document.getElementById('log-status').textContent='机载日志状态不可用';}}
document.getElementById('snapshot').onclick=async()=>{const button=document.getElementById('snapshot');button.disabled=true;document.getElementById('log-status').textContent='正在准备故障快照…';try{const r=await fetch('/logs.csv',{cache:'no-store'});if(!r.ok)throw new Error(await r.text()||'快照下载失败');const expected=Number(r.headers.get('X-Flight-Log-Rows')||0);const text=await r.text();const lines=text.trimEnd()?text.trimEnd().split('\n'):[];const actual=Math.max(0,lines.length-1);if(expected&&actual!==expected)throw new Error(`下载中断未保存：预期 ${expected} 行，实际 ${actual} 行`);const blob=new Blob([text],{type:'text/csv;charset=utf-8'});const link=document.createElement('a');link.href=URL.createObjectURL(blob);link.download='cf-drone-flight-log.csv';link.click();URL.revokeObjectURL(link.href);await refreshLogStatus();}catch(error){document.getElementById('log-status').textContent='快照下载失败：'+String(error.message||error).trim();}finally{button.disabled=false;}};
document.getElementById('resume-log').onclick=async()=>{try{const r=await fetch('/logs/resume',{method:'POST'});const d=await r.json();if(r.ok)renderLogStatus(d,'机载日志已恢复');else document.getElementById('log-status').textContent=d.error||'恢复失败，需先上锁并停止电机';}catch(_){document.getElementById('log-status').textContent='恢复失败，连接中断';}};
refreshLogStatus();setInterval(refreshLogStatus,2000);
</script></body></html>
)rawliteral";
#endif
