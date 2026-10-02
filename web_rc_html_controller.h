#pragma once

#if WEB_RC_ENABLED
const char webRCIndexHtml[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no">
<title>琛光无人机遥控器</title>
<style>
/*======== 响应式变量 ========*/
:root{
  --js-size:clamp(150px,min(50vw,62vh),420px);
  --knob-size:calc(var(--js-size)*0.25);
  --pad:clamp(6px,1.5vmin,15px);
  --gap:clamp(6px,1.5vmin,15px);
}
/*======== 通用样式 ========*/
*{margin:0;padding:0;box-sizing:border-box;-webkit-tap-highlight-color:transparent;touch-action:none;user-select:none}
html,body,.container{touch-action:pan-y}
 .container *{touch-action:pan-y}
 .joystick,.joystick *{touch-action:none}
body{font-family:'Roboto Mono',Arial,"Microsoft YaHei",sans-serif;background:#3c3c3c;color:#fff;overflow:hidden;height:100vh;height:100dvh;width:100vw}
.container{width:100%;height:100%;display:flex;flex-direction:column;padding:var(--pad);gap:var(--gap);max-width:1200px;margin:0 auto;overflow-x:hidden;overflow-y:auto;overscroll-behavior-y:contain;-webkit-overflow-scrolling:touch}

/*======== 顶部状态栏 ========*/
.header {
  text-align: center;
  padding: 6px 10px;
  background: rgba(30,30,30,.9);
  border-radius: 12px;
  border: 2px solid rgba(150,150,150,.3);
  box-shadow: 0 4px 16px rgba(0,0,0,.4);
  backdrop-filter: blur(10px)
}

.header h1 {
  font-size: 1.1rem;
  color: #ffffff;
  margin-bottom: 4px;
}

.header-tools{display:flex;justify-content:center;align-items:center;gap:6px;flex-wrap:wrap;margin:4px 0 6px}
.status-bar {
  display: flex;
  justify-content: center;
  align-items: center;
  gap: clamp(4px,1.2vmin,10px);
  flex-wrap: wrap;
  overflow: visible;
  margin-top: 4px;
}

.status-item {
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 2px 5px;
  background: rgba(0,0,0,.3);
  border-radius: 6px;
  border: 1px solid rgba(255,255,255,.1);
  font-size: clamp(0.58rem,1.8vmin,0.7rem);
  white-space: nowrap;
  flex-shrink: 1;
  min-width: 0;
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  display: inline-block;
}

.status-dot.connected { background: #0f8; box-shadow: 0 0 8px #0f8; }
.status-dot.disconnected { background: #f33; box-shadow: 0 0 8px #f33; }
.status-dot.warning { background: #ff9; box-shadow: 0 0 8px #ff9; }
.self-check-button{position:static;border:1px solid rgba(0,255,136,.55);border-radius:8px;background:rgba(0,255,136,.12);color:#aaffd4;padding:6px 10px;font-size:.75rem;font-weight:bold;cursor:pointer;touch-action:manipulation;white-space:nowrap}
.self-check-button.has-fault{border-color:rgba(255,80,80,.7);background:rgba(255,50,50,.18);color:#ffb0b0}
.header{position:relative;padding:8px 10px}
.diagnostic-page{position:fixed;inset:0;z-index:1000;display:none;background:#252525;overflow-y:auto;padding:clamp(14px,4vw,28px);touch-action:pan-y}
.diagnostic-shell{max-width:760px;margin:0 auto;display:flex;flex-direction:column;gap:14px}
.diagnostic-top{display:flex;align-items:center;justify-content:space-between;gap:12px}
.diagnostic-top h2{font-size:1.2rem}
.diagnostic-actions{display:flex;gap:8px}
.diagnostic-actions button{border:1px solid rgba(255,255,255,.2);border-radius:8px;background:#3b3b3b;color:#fff;padding:8px 12px;font-size:.85rem;cursor:pointer;touch-action:manipulation}
.diagnostic-summary{border:1px solid rgba(255,255,255,.14);border-radius:12px;padding:14px;background:rgba(0,0,0,.25)}
.diagnostic-summary.ok{border-color:rgba(0,255,136,.45)}
.diagnostic-summary.fault{border-color:rgba(255,80,80,.65);background:rgba(100,15,15,.25)}
.diagnostic-summary.offline{border-color:rgba(255,210,80,.55)}
.diagnostic-summary strong{display:block;font-size:1rem;margin-bottom:5px}
.diagnostic-summary small{color:#bbb;line-height:1.5}
.diagnostic-active{border:1px solid rgba(255,80,80,.75);border-radius:10px;padding:12px;background:rgba(100,15,15,.3);color:#ffe1e1}
.diagnostic-active strong{display:block;margin-bottom:6px}
.diagnostic-active ul{margin:0;padding-left:20px;line-height:1.6}
.diagnostic-active code{color:#ffc4c4}
.diagnostic-list{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:9px}
.diagnostic-item{background:rgba(0,0,0,.28);border:1px solid rgba(255,255,255,.1);border-radius:10px;padding:11px}
.diagnostic-item.active{border-color:rgba(255,80,80,.65)}
.diagnostic-item-title{display:flex;justify-content:space-between;gap:8px;font-weight:bold;margin-bottom:6px}
.diagnostic-item-state{font-size:.75rem;color:#8fdaae;white-space:nowrap}
.diagnostic-item.active .diagnostic-item-state{color:#ff9a9a}
.diagnostic-item p{font-size:.78rem;color:#c5c5c5;line-height:1.45}
.diagnostic-updated{text-align:right;font-size:.7rem;color:#999}

/*======== 内容区 ========*/
.content{display:flex;flex:1;gap:var(--gap);overflow:hidden;min-height:0}
.joystick-container{flex:1;display:flex;flex-direction:column;justify-content:center;align-items:center;background:rgba(0,0,0,.4);border-radius:20px;padding:clamp(8px,1.5vmin,15px);border:2px solid rgba(255,255,255,0.15);box-shadow:inset 0 0 30px rgba(0,0,0,.5)}
.joystick-title{font-size:clamp(0.75rem,2.2vmin,1.1rem);color:#cccccc}
.stick-readouts{display:flex;justify-content:center;gap:8px;margin-top:6px}
.stick-readout{padding:3px 8px;border:1px solid rgba(255,255,255,.12);border-radius:7px;background:rgba(0,0,0,.3);font-size:clamp(.68rem,1.8vmin,.85rem);color:#cbd5df;white-space:nowrap}
.stick-readout strong{color:#fff;font-variant-numeric:tabular-nums}
.joystick-wrapper{width:100%;height:var(--js-size);display:flex;justify-content:center;align-items:center;position:relative;margin-top:clamp(4px,2vh,20px);}
.joystick{width:var(--js-size);height:var(--js-size);background:radial-gradient(circle at 30% 30%,rgba(255,255,255,.1),rgba(0,0,0,.3));border-radius:50%;position:relative;border:2px solid rgba(255,255,255,0.2);box-shadow:inset 0 0 20px rgba(0,0,0,.5),0 8px 25px rgba(0,0,0,.5);overflow:hidden}
.joystick::before{content:'';position:absolute;top:50%;left:50%;width:2px;height:100%;background:linear-gradient(to bottom,transparent,rgba(255,255,255,.15),transparent);transform:translate(-50%,-50%)}
.joystick::after{content:'';position:absolute;top:50%;left:50%;width:100%;height:2px;background:linear-gradient(to right,transparent,rgba(255,255,255,.15),transparent);transform:translate(-50%,-50%)}
.joystick-knob{width:var(--knob-size);height:var(--knob-size);background:radial-gradient(circle at 30% 30%,#fff,#cccccc);border-radius:50%;position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);border:2px solid rgba(255,255,255,.7);box-shadow:0 4px 15px rgba(0,0,0,.5),inset 0 0 10px rgba(255,255,255,.5);cursor:move;transition:transform .1s ease-out;z-index:10}

/*======== 按钮区 ========*/
.buttons-container{flex:.55;display:flex;flex-direction:column;gap:10px;padding:12px;background:rgba(0,0,0,.4);border-radius:20px;border:2px solid rgba(150,150,150,.3);box-shadow:inset 0 0 20px rgba(0,0,0,.5)}
#descent-calibration-button{border-color:rgba(255,160,60,.65);background:rgba(255,140,0,.16);color:#ffd2a3}
#vibration-calibration-button{border-color:rgba(110,190,255,.65);background:rgba(30,120,200,.16);color:#c4e6ff}
.route-page{position:fixed;inset:0;z-index:1001;display:none;background:#252525;overflow-y:auto;padding:clamp(14px,4vw,28px);touch-action:pan-y}
.descent-calibration-page{position:fixed;inset:0;z-index:1002;display:none;background:#252525;overflow-y:auto;padding:clamp(14px,4vw,28px);touch-action:pan-y}
.vibration-calibration-page{position:fixed;inset:0;z-index:1003;display:none;background:#252525;overflow-y:auto;padding:clamp(14px,4vw,28px);touch-action:pan-y}
.descent-calibration-shell{max-width:860px;margin:0 auto;display:flex;flex-direction:column;gap:12px}
.calibration-card{border:1px solid rgba(255,255,255,.14);border-radius:12px;padding:14px;background:rgba(0,0,0,.25)}
.calibration-card p,.calibration-card small{color:#c4cbd3;font-size:.85rem;line-height:1.5}
.calibration-actions{display:flex;gap:8px;flex-wrap:wrap}
.calibration-actions button{border:0;border-radius:8px;padding:10px 14px;background:#444;color:#fff;font-size:.9rem;touch-action:manipulation}
.calibration-actions button.primary{background:#a85b00}.calibration-actions button:disabled{opacity:.45}
.calibration-fields{display:flex;align-items:center;gap:8px;flex-wrap:wrap}
.calibration-fields input{max-width:130px;background:#17191c;color:#fff;border:1px solid #777;border-radius:7px;padding:9px}
.calibration-fields button{border:0;border-radius:7px;padding:9px 12px;background:#444;color:#fff;font-size:.88rem;touch-action:manipulation}
.calibration-points{display:grid;gap:6px;font-size:.82rem;color:#d3d9e0}
.route-shell{max-width:860px;margin:0 auto;display:flex;flex-direction:column;gap:12px}
.route-intro{padding:12px 14px;border:1px solid rgba(255,180,70,.35);border-radius:10px;background:rgba(120,70,10,.16);color:#ffe0b0;font-size:.88rem;line-height:1.5}
.route-step{display:flex;flex-direction:column;gap:10px;padding:14px;border:1px solid rgba(255,255,255,.13);border-radius:12px;background:rgba(0,0,0,.2)}
.route-step-heading{display:flex;align-items:center;gap:9px}
.route-step-number{display:grid;place-items:center;flex:0 0 26px;height:26px;border-radius:50%;background:#285c85;color:#fff;font-size:.82rem;font-weight:bold}
.route-step-heading h3{font-size:1rem}
.route-step-note{color:#c4cbd3;font-size:.84rem;line-height:1.45}
.route-details{border:1px solid rgba(255,255,255,.12);border-radius:9px;padding:10px 12px;background:rgba(0,0,0,.16)}
.route-details summary{cursor:pointer;color:#cbd5df;font-size:.86rem;touch-action:manipulation}
.route-details .route-help{margin-top:9px}
.route-state-card{display:grid;gap:8px;padding:12px;border:1px solid rgba(100,180,255,.25);border-radius:10px;background:rgba(20,60,90,.18)}
.route-state-card strong{font-size:.88rem;color:#d9ecff}
.route-state-card .route-status{margin:0}
.route-editor{width:100%;min-height:48vh;padding:12px;border:1px solid #777;border-radius:9px;background:#17191c;color:#e9f1ff;font: .9rem/1.5 ui-monospace,SFMono-Regular,Consolas,monospace;white-space:pre;overflow:auto;touch-action:auto;user-select:text;-webkit-user-select:text}
.route-help{color:#c4cbd3;font-size:.85rem;line-height:1.5}.route-page-actions{display:flex;gap:8px;flex-wrap:wrap}.route-page-actions button{border:0;border-radius:8px;padding:10px 14px;background:#444;color:#fff;font-size:.9rem;touch-action:manipulation}.route-page-actions .run{background:#167c3a}.route-page-actions .stop{background:#a33}
.route-recorder{border:1px solid rgba(255,255,255,.14);border-radius:10px;padding:10px;background:rgba(0,0,0,.22);display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap}.route-recorder-actions{display:flex;gap:8px;flex-wrap:wrap}.route-recorder-actions button{border:0;border-radius:8px;padding:9px 12px;background:#555;color:#fff;font-size:.88rem;touch-action:manipulation}.route-recorder-actions .record{background:#a33}.route-recorder-actions .stop{background:#167c3a}.route-recorder-actions button:disabled{opacity:.45}.route-recorder-status{color:#ffd27a;font-size:.85rem;line-height:1.4;flex:1;min-width:220px}
.route-status{font-size:.9rem;color:#9fc7ff;margin-top:6px}
.buttons-grid{display:grid;grid-template-columns:repeat(6,1fr);grid-template-rows:repeat(2,1fr);gap:8px;flex:1}
.buttons-grid>button{grid-column:span 2}
.button{background:linear-gradient(145deg,#484848,#383838);border:none;border-radius:10px;color:#fff;font-size:.85rem;font-weight:bold;display:flex;flex-direction:column;justify-content:center;align-items:center;text-align:center;padding:10px 5px;cursor:pointer;transition:all .15s cubic-bezier(.4,0,.2,1);box-shadow:0 3px 10px rgba(0,0,0,.3),inset 0 1px 0 rgba(255,255,255,.1);position:relative}
.button:hover{background:linear-gradient(145deg,#565656,#464646);transform:translateY(-1px)}
.button.active{background:linear-gradient(145deg,#1a73e8,#0d47a1);box-shadow:0 0 15px rgba(26,115,232,.6),inset 0 1px 0 rgba(255,255,255,.2);transform:scale(.95)}
.button:active{transform:scale(.92)}
.button-icon{font-size:1.1rem;margin-bottom:4px}

/*======== 调试控制台 ========*/
.console-window{position:fixed;inset:0;z-index:1010;display:none;align-items:center;justify-content:center;padding:16px;background:rgba(0,0,0,.72);touch-action:pan-y}
.console-window[aria-hidden="false"]{display:flex}
.console-dialog{width:min(760px,100%);height:min(620px,85dvh);display:flex;flex-direction:column;gap:8px;padding:12px;background:#17191c;border:1px solid rgba(255,255,255,.2);border-radius:14px;box-shadow:0 16px 48px rgba(0,0,0,.65);touch-action:pan-y}
.console-dialog-header{display:flex;align-items:center;justify-content:space-between;gap:10px}
.console-dialog-header h2{font-size:1rem}
.console-dialog-header button{border:1px solid #666;border-radius:7px;background:#333;color:#fff;padding:7px 12px;touch-action:manipulation}
.console-panel{background:rgba(10,10,10,.95);border-radius:8px;border:1px solid rgba(100,100,100,.4);padding:10px;flex:1;min-height:0;display:flex;flex-direction:column;gap:8px;touch-action:pan-y}
.console-output{flex:1;min-height:0;overflow-y:auto;overscroll-behavior:contain;touch-action:pan-y;user-select:text;-webkit-user-select:text;font-family:'Courier New',monospace;font-size:0.7rem;color:#00ff88;word-break:break-all;-webkit-overflow-scrolling:touch}
.console-output,.console-output *{touch-action:pan-y;user-select:text;-webkit-user-select:text}
.console-output div{padding:1px 0;border-bottom:1px solid rgba(255,255,255,.03)}

/*======== 动画 ========*/
@keyframes pulse{0%{box-shadow:0 0 0 0 rgba(67,97,238,.7)}70%{box-shadow:0 0 0 12px rgba(67,97,238,0)}100%{box-shadow:0 0 0 0 rgba(67,97,238,0)}}
.joystick.active{animation:pulse 1.5s infinite}
@keyframes fadeIn{from{opacity:0;transform:translateY(20px)}to{opacity:1;transform:translateY(0)}}
.container>*{animation:fadeIn .5s ease-out}

/*======== 竖屏自适应 ========*/
@media (orientation:portrait){
  body{height:auto;min-height:100vh;min-height:100dvh;overflow-y:auto}
  .container{height:auto;min-height:100vh;min-height:100dvh;overflow:visible}
  :root{--js-size:clamp(120px,40vw,240px);--knob-size:calc(var(--js-size)*0.25)}
  .content{
    display:grid;
    grid-template-columns:repeat(2,minmax(0,1fr));
    grid-template-rows:auto auto;
    height:auto;
    flex:none;
    overflow:visible;
  }
  .buttons-container{grid-column:1/3;grid-row:1}
  .content>.joystick-container:first-child{grid-column:1;grid-row:2;min-width:0;overflow:hidden}
  .content>.joystick-container:last-child{grid-column:2;grid-row:2;min-width:0;overflow:hidden}
  .header h1{font-size:clamp(0.85rem,3.5vw,1.1rem)}
  .self-check-button{padding:5px 7px;font-size:.68rem}
  #route-page-button,#descent-calibration-button,#vibration-calibration-button{padding:5px 7px;font-size:.68rem}
  .status-bar{gap:5px;flex-wrap:wrap;justify-content:center}
  .status-item{font-size:clamp(0.6rem,2.5vw,0.7rem);padding:2px 5px}
}

/*======== 小屏横屏自适应（高度≤420px）========*/
@media (max-height:420px) and (orientation:landscape){
  :root{--js-size:clamp(120px,min(42vw,44vh),240px);--knob-size:calc(var(--js-size)*0.25)}
  .joystick-title{font-size:0.72rem}
  .header h1{font-size:0.82rem}
  .header{padding:3px 8px}
  .header h1{margin-bottom:2px}
  .status-bar{margin-top:2px;gap:4px}
  .status-item{font-size:0.58rem;padding:2px 4px}
  .button{font-size:0.68rem;padding:5px 3px}
  .button-icon{font-size:0.88rem;margin-bottom:2px}
}
/*======== 版权页脚 ========*/
.footer{text-align:center;font-size:0.5rem;color:rgba(255,255,255,.25);padding:0;flex-shrink:0;line-height:0.8;}
.footer a{color:rgba(255,255,255,.3);text-decoration:none}
.footer a:hover{color:rgba(255,255,255,.55)}
</style>
</head>
<body>
<div class="container">
  <!-- 顶部状态栏 -->
  <div class="header">
    <h1>琛光无人机网页遥控器</h1>
    <nav class="header-tools" aria-label="飞控工具">
      <button id="self-check-button" class="self-check-button" onclick="openSelfCheck()">自检状态</button>
      <button id="route-page-button" class="self-check-button" onclick="openRoutePage()">开环序列</button>
      <button id="descent-calibration-button" class="self-check-button" onclick="handleDescentCalibrationEntry()">迫降标定</button>
      <button id="vibration-calibration-button" class="self-check-button" onclick="openVibrationCalibrationPage()">电机扰动检测</button>
    </nav>
    <div class="status-bar">
      <div class="status-item"><span class="status-dot" id="status-dot"></span><span id="connection-text">连接中...</span></div>
      <div class="status-item" id="armed-status-item" style="background:rgba(255,51,51,0.15)"><span id="armed-status" style="color:#ff6666">已上锁</span></div>
      <div class="status-item"><span>飞行模式</span><span id="flight-mode">自稳</span></div>
      <button id="console-open-button" class="status-item" style="background:#24527a;color:white;border:0;cursor:pointer;touch-action:manipulation" onclick="toggleConsole()">调试</button>
      <button id="route-takeover-main" class="status-item" style="display:none;background:#167c3a;color:white;border:0" onclick="takeManualControl()">接管摇杆</button>
      <div class="status-item"><span>电池电压</span><span id="battery">-</span></div>
      <div class="status-item"><span>遥控延迟</span><span id="latency">-</span></div>
      <div class="status-item"><span>丢包率</span><span id="packet-loss">0%</span></div>
    </div>
  </div>

  <div class="content">
    <!-- 左摇杆 -->
    <div class="joystick-container">
      <div class="joystick-title">左摇杆 (油门/偏航；松手保持油门，需手动下推减油)</div>
      <div class="stick-readouts" role="group" aria-label="左摇杆输入值"><span class="stick-readout">油门 <strong id="left-y">0</strong>%</span><span class="stick-readout">偏航 <strong id="left-x">0</strong></span></div>
      <div class="joystick-wrapper">
        <div class="joystick" id="joystick-left"><div class="joystick-knob" id="knob-left"></div></div>
      </div>
    </div>

    <!-- 按钮区 -->
    <div class="buttons-container">
      <div class="buttons-grid" id="buttons-container"></div>
    </div>

    <!-- 右摇杆 -->
    <div class="joystick-container">
      <div class="joystick-title">右摇杆 (俯仰/横滚)</div>
      <div class="stick-readouts" role="group" aria-label="右摇杆输入值"><span class="stick-readout">横滚 <strong id="right-x">0</strong></span><span class="stick-readout">俯仰 <strong id="right-y">0</strong></span></div>
      <div class="joystick-wrapper">
        <div class="joystick" id="joystick-right"><div class="joystick-knob" id="knob-right"></div></div>
      </div>
    </div>
  </div>

  <!-- 调试控制台独立窗口 -->
  <section id="console-window" class="console-window" role="dialog" aria-modal="true" aria-labelledby="console-title" aria-hidden="true">
    <div class="console-dialog">
      <div class="console-dialog-header"><h2 id="console-title">调试控制台</h2><button id="console-close-button" type="button" onclick="toggleConsole()">关闭</button></div>
      <div id="console-panel" class="console-panel">
    <div id="console-output" class="console-output"></div>
    <div style="display:flex;gap:6px;touch-action:pan-y">
      <input id="console-input" placeholder="输入命令 (ps/imu/rc/arm/disarm/help)..."
        style="flex:1;background:rgba(0,0,0,.6);border:1px solid rgba(100,100,100,.5);border-radius:6px;color:#0f8;padding:4px 8px;font-size:0.7rem;font-family:'Courier New',monospace;touch-action:auto">
      <button onclick="downloadConsoleLogs()" style="background:#444;border:1px solid #777;border-radius:6px;color:#fff;padding:4px 8px;font-size:0.7rem;cursor:pointer;touch-action:manipulation;white-space:nowrap">下载日志</button>
      <button onclick="sendConsoleCmd()" style="background:#1a73e8;border:none;border-radius:6px;color:#fff;padding:4px 10px;font-size:0.7rem;cursor:pointer;touch-action:auto">发送</button>
    </div>
      </div>
    </div>
  </section>
  <section id="diagnostic-page" class="diagnostic-page" aria-hidden="true">
    <div class="diagnostic-shell">
      <div class="diagnostic-top">
        <h2>飞控自检状态</h2>
        <div class="diagnostic-actions">
          <button onclick="refreshSelfCheck()">刷新</button>
          <button onclick="closeSelfCheck()">关闭</button>
        </div>
      </div>
      <div id="diagnostic-summary" class="diagnostic-summary offline">
        <strong>正在读取诊断状态…</strong>
        <small>数据来自飞控当前运行状态。</small>
      </div>
      <div id="arm-readiness" class="diagnostic-summary offline">
        <strong>正在检查解锁条件…</strong>
        <small>此状态依据飞控当前实际解锁门槛。</small>
      </div>
      <div id="led-alert-reason" class="diagnostic-summary offline">
        <strong>正在读取蓝灯状态…</strong>
      </div>
      <div id="diagnostic-active" class="diagnostic-active" style="display:none"></div>
      <div id="diagnostic-list" class="diagnostic-list"></div>
      <section id="motor-self-check" class="diagnostic-summary offline">
        <strong>电机响应检测尚未执行</strong>
        <small>拆下全部桨叶并固定机体后，可启动四路自动检测。</small>
      </section>
      <div class="diagnostic-actions"><button id="motor-self-check-start" onclick="startVibrationCalibration()">启动四电机自动检测</button><button id="motor-self-check-stop" onclick="stopMotorSelfCheck()" disabled>停止检测</button></div>
      <div id="diagnostic-updated" class="diagnostic-updated">尚未获取</div>
    </div>
  </section>
  <section id="route-page" class="route-page" aria-hidden="true">
    <div class="route-shell">
      <div class="diagnostic-top"><h2>相对航线（遥控输出序列）</h2><div class="diagnostic-actions"><button onclick="closeRoutePage()">返回遥控器</button></div></div>
      <p class="route-intro">这是按时间回放遥控输入，不是坐标航点。正常完成或异常中断时，飞控可能转入定推力下降；手动接管会切回自稳。没有高度或触地反馈，飞手需保持接管能力并在触地后上锁。</p>
      <div class="route-state-card" aria-live="polite"><strong>当前飞控状态</strong><div class="route-status" id="route-status">正在读取飞控状态…</div><div class="route-status" id="route-message" role="status">当前内容尚未上传；上传不会解锁或启动。</div></div>

      <section class="route-step" aria-labelledby="route-step-edit-title">
        <div class="route-step-heading"><span class="route-step-number">1</span><h3 id="route-step-edit-title">录制或编辑序列</h3></div>
        <p class="route-step-note">可直接操作摇杆录制，也可在下方编辑每段遥控输入。</p>
        <div class="route-recorder"><div id="route-record-status" class="route-recorder-status">录制关闭；只在浏览器本地采样当前摇杆输出。</div><div class="route-recorder-actions"><button id="route-record-start" class="record" onclick="startRouteRecording()">开始录制</button><button id="route-record-stop" class="stop" onclick="stopRouteRecording('手动停止录制。')" disabled>停止录制</button></div></div>
        <textarea id="route-editor" class="route-editor" spellcheck="false" aria-label="开环控制序列"></textarea>
        <details class="route-details"><summary>序列格式与录制规则</summary><p class="route-help">每行按顺序填写：持续秒数、油门百分比、横滚输入、俯仰输入、偏航输入。持续时间 0.1–600 秒，油门 0–100%，其余输入 -100–100（不是角度）。最多 128 段、总时长 30 分钟、正文 4096 字节；空行和 # 注释不执行。</p><p class="route-help">上锁时可在浏览器本地录制；飞行中录制需要处于自稳模式并由当前页面持有遥控控制权。停止录制后会裁掉开头低于 6% 油门死区的等待段，并将结果写入编辑框及浏览器。录制序列按每 100 ms 采样的遥控值回放；手写序列仍按原有限速执行。</p></details>
      </section>

      <section class="route-step" aria-labelledby="route-step-upload-title">
        <div class="route-step-heading"><span class="route-step-number">2</span><h3 id="route-step-upload-title">保存并上传校验</h3></div>
        <p class="route-step-note">编辑内容先保存到浏览器，再在上锁、电机停止时上传。上传会读回校验，不会解锁或启动。</p>
        <div class="route-page-actions"><button onclick="saveRoute()">保存到浏览器</button><button id="route-device-load" onclick="loadDeviceRoute(false)">从设备加载（覆盖编辑框）</button><button id="route-upload" onclick="uploadRoute()">上传并校验</button></div>
      </section>

      <section class="route-step" aria-labelledby="route-step-play-title">
        <div class="route-step-heading"><span class="route-step-number">3</span><h3 id="route-step-play-title">准备回放</h3></div>
        <p class="route-step-note">上传校验成功后进入 AUTO；返回遥控器页面并由操作者解锁，飞控才会开始执行。</p>
        <div class="route-page-actions"><button id="route-auto" class="run" onclick="setAutoModeForRoute()">进入回放模式（AUTO）</button><button id="route-takeover" class="stop" onclick="takeManualControl()">接管摇杆</button></div>
      </section>

      <details class="route-details"><summary>回放行为与能力边界</summary><p class="route-help">每段的数值会在该段持续使用，不是相对上一段的增量。飞控没有位置或高度反馈，风、推力和机体响应都会影响实际轨迹；回放遥控指令不保证相同的飞行轨迹或着陆。序列正常完成、连接断开或执行周期中断时，飞控会转入定推力下降；若末段油门低于设定的下降推力，进入下降时油门可能上升。手动接管会切回自稳。下降推力需先用带桨实测标定，飞手需确认着陆并手动上锁。设备序列只保存在运行内存，飞控重启后需重新上传；需要迫降时使用遥控器上的“迫降”按钮。</p></details>
    </div>
  </section>
  <section id="descent-calibration-page" class="descent-calibration-page" aria-hidden="true">
    <div class="descent-calibration-shell">
      <div class="diagnostic-top"><h2>迫降推力标定</h2><div class="diagnostic-actions"><button onclick="closeDescentCalibrationPage()">返回遥控器</button></div></div>
      <div class="calibration-card"><strong>飞手手动下降，飞控只记录</strong><p>仅在自稳模式且已解锁时开始记录。开始后请关闭此页回到摇杆操作；可从顶部“停止标定”结束采集。单次最多 30 秒，记录每秒约 20 个样本。该功能不会自动改变飞行控制。</p><small>完成后输入这段记录对应的实测下降高度差，页面用高度差 ÷ 记录时长计算平均下降速度。IMU 不会提供可靠的垂直速度或离地高度。</small></div>
      <div id="descent-calibration-status" class="route-status" role="status">正在读取标定状态…</div>
      <div class="calibration-actions"><button id="descent-calibration-start" class="primary" onclick="startDescentCalibrationCapture()">开始记录当前手动下降</button><button id="descent-calibration-stop" onclick="stopDescentCalibrationCapture()">停止记录</button><button id="descent-calibration-download" onclick="downloadDescentCalibrationCsv()">下载原始记录</button></div>
      <div class="calibration-card"><strong>记录测量结果</strong><div class="calibration-fields"><label for="descent-drop-distance">实测高度差（米）</label><input id="descent-drop-distance" type="number" min="0.1" max="100" step="0.1" placeholder="例如 2.0"><button onclick="addDescentCalibrationPoint()">添加实测点</button></div><p id="descent-calibration-measurement" class="route-status">需要一段有效且稳定的标定记录。</p></div>
      <div class="calibration-card"><strong>实测点与推力建议</strong><div id="descent-calibration-points" class="calibration-points">此浏览器还没有保存实测点。</div><div class="calibration-fields"><label for="descent-target-speed">期望最大下降速度（米/秒）</label><input id="descent-target-speed" type="number" min="0.05" max="5" step="0.05" placeholder="输入目标"><button onclick="recommendDescentCalibrationPoint()">查找实测点</button></div><p id="descent-calibration-recommendation" class="route-status">只会推荐速度不超过目标值的实测点；不会插值或外推。</p><div class="calibration-actions"><button id="descent-calibration-apply" class="primary" onclick="applyDescentCalibrationRecommendation()" disabled>确认保存下降推力</button><button onclick="clearDescentCalibrationPoints()">清除此浏览器的实测点</button></div></div>
      <div class="calibration-card"><strong>能力边界</strong><p>这是经验推力标定，不是自动着陆。迫降仍是定推力下降；飞控没有高度、垂直速度或触地反馈，不能据此保证下降速度或避免撞地。飞手必须保持接管能力，并在触地后明确上锁。电池、载荷、螺旋桨、风和地面效应变化都会影响结果。</p></div>
    </div>
  </section>
  <section id="vibration-calibration-page" class="vibration-calibration-page" aria-hidden="true">
    <div class="descent-calibration-shell">
      <div class="diagnostic-top"><h2>四电机低功率扰动检测</h2><div class="diagnostic-actions"><button onclick="closeVibrationCalibrationPage()">返回遥控器</button></div></div>
      <div class="calibration-card"><strong>开始前：拆下全部桨叶并固定机体</strong><p>飞控保持上锁。系统先记录约 200 ms 静止基线，再按 FR、FL、RR、RL 顺序分别以 5% 输出短脉冲约 100 ms，并采集同步 IMU 数据。关闭页面不会中断流程，可用“停止检测”立即上锁并取消后续试转。</p><small>5% 短脉冲可能不足以起转或形成可测扰动；此时显示“转动未确认”，不会自动增加输出。请勿触碰机体或电机。</small></div>
      <div id="vibration-calibration-status" class="route-status" role="status">正在读取校准状态…</div>
      <div class="calibration-actions"><button id="vibration-calibration-start" class="primary" onclick="startVibrationCalibration()">开始四电机采集</button><button id="vibration-calibration-stop" onclick="stopMotorSelfCheck()" disabled>停止检测</button><button id="vibration-calibration-download" onclick="downloadVibrationCalibrationCsv()" disabled>下载行数表格 CSV</button></div>
      <div class="calibration-card"><strong>逐电机结果</strong><div id="vibration-calibration-results" class="calibration-points">尚无结果。</div><p id="vibration-calibration-analysis" class="route-status">比较四个电机的加速度振动 RMS；偏高只表示优先复核机械安装、紧固和电机，不直接判定损坏。</p></div>
      <div class="calibration-card"><strong>姿态算法评估边界</strong><p>该流程可筛查电机振动是否可能污染 IMU 输入。姿态算法的改进需另用静态、手动遥控飞行日志及可信姿态参考评估；仅凭单电机振动数据无法可靠地自动调节加速度计权重或滤波参数。</p></div>
    </div>
  </section>
  <section id="level-calibration-page" class="vibration-calibration-page" aria-hidden="true">
    <div class="descent-calibration-shell">
      <div class="diagnostic-top"><h2>机身水平校准</h2><div class="diagnostic-actions"><button onclick="closeLevelCalibrationPage()">返回遥控器</button></div></div>
      <div class="calibration-card"><strong>用机身基准面确认水平</strong><p>先上锁并停止全部电机，用水平仪将机身基准面放平、固定且保持静止。此功能采集约 1 秒 IMU 数据，检查重力模长与振动，再建议 IMU 安装横滚/俯仰角；不会修改六面加速度计偏置，也不能把空中悬停姿态当作水平基准。</p></div>
      <div id="level-calibration-live" class="route-status">正在读取估计姿态…</div>
      <div id="level-calibration-status" class="route-status" role="status">尚未采集。</div>
      <div class="calibration-actions"><button id="level-calibration-start" class="primary" onclick="startLevelCalibration()">采集水平基准</button><button id="level-calibration-apply" onclick="applyLevelCalibration()" disabled>确认保存安装角</button><button id="level-calibration-discard" onclick="discardLevelCalibration()">放弃建议</button></div>
      <div class="calibration-card"><small>保存后仍需保持机身水平，等待估计姿态收敛并复核 Roll/Pitch。航向角没有磁力计绝对参考，校准后不要求归零。</small></div>
    </div>
  </section>
  <!-- 版权页脚 -->
  <div class="footer"><a href="/wifi">Wi-Fi 设置</a> · <a href="/telemetry">实时日志</a> · <a href="#" onclick="openLevelCalibrationPage();return false">水平校准</a> · <a href="https://oshwhub.com/songge8/project_qqqyfdkm" target="_blank">琛光无人机开源项目</a></div>
</div>

<script>
/*======================== 全局变量 ========================*/
let lastAnimationTime = 0;
let connectionOk = false;
let packetStats = { sent: 0, lost: 0 };
let latencyHistory = new Array(10).fill(0);
let latencyIndex = 0;
const SEND_INTERVAL = 50;  // ~20Hz 摇杆检测频率
const FORCE_SEND_INTERVAL = 200; // 静止时强制重发间隔（ms），保持飞控数据新鲜
const stickReadoutElements = {
  throttle:document.getElementById('left-y'),
  yaw:document.getElementById('left-x'),
  roll:document.getElementById('right-x'),
  pitch:document.getElementById('right-y'),
};

const touches = new Map();
let leftStick  = {x:0, y:0, rawX:0, rawY:-100};
let rightStick = {x:0, y:0, rawX:0, rawY:0};

let lastSentValues = { throttle:0, roll:0, pitch:0, yaw:0 };
let lastForceSentTime = 0; // 上次强制重发的时间戳（performance.now()）
let currentValues  = { throttle:0, roll:0, pitch:0, yaw:0 };
const MIN_CHANGE_THRESHOLD = 0.5;

// 固定参数常量（替代前端参数调节面板，与后端 CONFIG_ 对应）
const DEADZONE = 3;   // 死区（已移至后端 stickDeadzone 统一处理，前端不再使用）
const EXPO    = 40;   // 指数曲线 40%
let consecutiveFails = 0; // 连续失败计数，>=3 才判定断连
let currentFlightMode = 2; // 当前飞行模式编号（与后端同步：2=自稳）
let currentArmed = false;
let armedStatusKnown = false;
// 解锁时不从遥控页面发送配置、控制台或诊断请求，给飞行指令让路。
// 保留飞行控制、失控处置以及这些流程所需的简短状态请求。
const flightRequestPaths = new Set([
  '/web_rc', '/web_rc/heartbeat', '/web_rc/lease', '/web_rc/status',
  '/route/takeover', '/route/status',
  '/descent-calibration/start', '/descent-calibration/stop', '/descent-calibration/status',
  '/console/disable'
]);
const nativeFetch = window.fetch.bind(window);
window.fetch = (input, options) => {
  const path = typeof input === 'string' ? input.split('?')[0] : new URL(input.url).pathname;
  if ((!armedStatusKnown || currentArmed) && !flightRequestPaths.has(path))
    return Promise.reject(new Error('飞控已解锁，非飞行请求已暂停'));
  return nativeFetch(input, options);
};
document.addEventListener('click', event => {
  const link = event.target.closest('a[href]');
  if ((!armedStatusKnown || currentArmed) && link && ['/wifi', '/telemetry'].includes(new URL(link.href).pathname)) {
    event.preventDefault();
    showToast('请先上锁后打开配置或日志页面');
  }
}, true);
let webRCLeaseToken = '';
let webRCStopToken = '';
try { webRCStopToken = sessionStorage.getItem('cfDroneStopToken') || ''; } catch (_) {}
let webRCLeasePromise = null;
let webRCLeaseBlocked = false;

let buttonStates     = new Array(16).fill(false);
let lastButtonStates = new Array(16).fill(false);

let consolePollingTimer = null;
let consoleLastTotal    = 0;   // 增量拉取游标：已展示到第 N 行
let consoleFetchInFlight = false; // 防并发：上次 fetch 未返回时跳过本次
let consolePanelOpen = false;
function setArmedState(armed) {
  const wasArmed = currentArmed;
  currentArmed = !!armed;
  armedStatusKnown = true;
  if (currentArmed && !wasArmed && consolePanelOpen) toggleConsole();
  if (routeRecording && routeRecordStartedArmed && !currentArmed && wasArmed) stopRouteRecording('飞控已上锁，录制已安全停止。', true);
  if (routeRecording && !routeRecordStartedArmed && currentArmed && !wasArmed) stopRouteRecording('飞控已解锁，本地录制已停止。', true);
  updateRouteControls();
}
let selfCheckOpen = false;
let selfCheckRequestSequence = 0;
let selfCheckHasData = false;
let routeTimer = null;
let descentCalibrationTimer = null;
let vibrationCalibrationTimer = null;
let descentCalibrationRecommendation = null;
let descentCalibrationLatestStatus = null;
const DESCENT_CALIBRATION_POINTS_KEY = 'cfDroneDescentCalibrationPointsV1';
let flightRouteRunning = false;
let routeStarting = false;
let routeHold = false;
let routePending='',routeUploadedText=null,routeUploadedRevision=0,routeServerState='empty',routeStatusBusy=false;
let routeRecording=false,routeRecordStartedArmed=false,routeRecordTimer=null,routeRecordStartMs=0,routeRecordSegmentStartMs=0,routeRecordLast=null,routeRecordSegments=[];
let routeRecordTrimmedMs=0;
const ROUTE_RECORD_SAMPLE_MS=100;
const ROUTE_RECORD_IDLE_THROTTLE_PCT=6; // 与飞控 throttleDeadzone 保持一致
const ROUTE_RECORD_HEADER='# WEB_RC_RECORDED_V1';
const ROUTE_RECORD_MAX_SEGMENTS=128;
const ROUTE_RECORD_MAX_BYTES=4096;
const ROUTE_RECORD_MAX_DURATION_MS=1800000;
const CONSOLE_BASE_POLL_MS = 500;
const CONSOLE_CATCHUP_POLL_MS = 80;
const CONSOLE_PAGE_LIMIT = 20;
const CONSOLE_REQUEST_TIMEOUT_MS = 3000;

/*======================== 按钮配置（2×3 六宫格）========================*/
const buttonConfigs = [
  {icon:"🔓",label:"解锁",   color:"#00ff88",desc:"解锁电机"},
  {icon:"🔒",label:"上锁",   color:"#ff3333",desc:"锁定电机"},
  {icon:"🛑",label:"急停",   color:"#ff0055",desc:"紧急停止"},
  {icon:"🛬",label:"迫降",   color:"#ff8c00",desc:"保持水平并进入自动下降；无高度/速度反馈"},
  {icon:"🔄",label:"切换模式", color:"#00cfff",desc:"自稳与特技切换；不支持定高"},
  {icon:"⏺",label:"录制序列",color:"#ff5555",desc:"录制当前摇杆输出序列；再次点击停止"}
];

/*======================== 初始化 ========================*/
function init() {
  initButtons();
  initNetwork();
  initPointerEvents();
  initConsoleTouchScrolling();
  loadRoute();
  refreshRouteStatus().then(()=>loadDeviceRoute(true));
  refreshDescentCalibrationStatus();
  requestAnimationFrame(animationLoop);
  requestAnimationFrame(initKnobPositions);
}

const defaultRouteText=`# 每行：持续秒数 油门百分比 横滚输入 俯仰输入 偏航输入
# 格式示例（注释不会执行）：1.0 0 0 0 0
# 请填写经机体验证的遥控输出序列；按段执行油门、横滚、俯仰和偏航输入，不使用坐标航点。`;
function routeMessage(message){document.getElementById('route-message').textContent=message;}
function parseRouteText(){
  const text=document.getElementById('route-editor').value;
  if(new TextEncoder().encode(text).length>4096)throw new Error('序列正文不能超过 4096 字节');
  const points=[];const lines=text.split(/\r?\n/);
  for(let i=0;i<lines.length;i++){
    const line=lines[i].trim();if(!line||line.startsWith('#'))continue;
    const fields=line.split(/[\s,]+/);
    if(fields.length!==5||fields.some(v=>v===''||!Number.isFinite(Number(v))))throw new Error(`第 ${i+1} 行需包含 5 个有限数字`);
    const [duration,throttle,roll,pitch,yaw]=fields.map(Number);
    if(duration<0.1||duration>600||throttle<0||throttle>100||Math.abs(roll)>100||Math.abs(pitch)>100||Math.abs(yaw)>100)throw new Error(`第 ${i+1} 行参数超出范围`);
    points.push({duration,throttle,roll,pitch,yaw});
  }
  if(!points.length)throw new Error('请至少填写一个有效动作段');
  if(points.length>128)throw new Error('最多支持 128 个动作段');
  if(points.reduce((sum,p)=>sum+Math.round(p.duration*1000),0)>1800000)throw new Error('序列总时长不能超过 30 分钟');
  return points;
}
function updateRouteControls(){
  const busy=routeStarting||!!routePending;
  const active=flightRouteRunning||routeHold||routeServerState==='start_pending';
  const editor=document.getElementById('route-editor');
  editor.disabled=busy||active||routeRecording;
  document.getElementById('route-device-load').disabled=busy||active||routeRecording||currentArmed||!connectionOk;
  document.getElementById('route-upload').disabled=busy||active||routeRecording||currentArmed||!connectionOk;
  document.getElementById('route-auto').disabled=busy||active||routeRecording||currentArmed||!connectionOk||routeUploadedText!==editor.value||!routeUploadedRevision||routeServerState!=='ready'||currentFlightMode===4;
  document.getElementById('route-takeover').disabled=busy||!active||!connectionOk;
  document.getElementById('route-takeover-main').style.display=active?'':'none';
  document.getElementById('route-takeover-main').disabled=busy||!connectionOk;
  const canRecord=!busy&&!active&&!routeRecording&&(!currentArmed||(connectionOk&&currentFlightMode===2&&!!webRCLeaseToken&&!webRCLeaseBlocked));
  const recordStart=document.getElementById('route-record-start'),recordStop=document.getElementById('route-record-stop');
  if(recordStart)recordStart.disabled=!canRecord;
  if(recordStop)recordStop.disabled=!routeRecording;
  const recordMain=document.getElementById('route-record-main');
  if(recordMain){
    recordMain.querySelector('.button-icon').textContent=routeRecording?'⏹':'⏺';
    recordMain.querySelector('.button-label').textContent=routeRecording?'停止录制':'录制序列';
    recordMain.disabled=routeRecording?false:!canRecord;
    recordMain.style.background=routeRecording?'linear-gradient(145deg,#a33,#762222)':'';
    recordMain.style.borderColor=routeRecording?'#ff7777':'#ff555588';
  }
}
function saveRoute(){
  try{localStorage.setItem('cfDroneOpenLoopSequence',document.getElementById('route-editor').value);routeMessage('已保存到此浏览器；尚未上传到飞控。');}
  catch(_){routeMessage('浏览器未允许本地保存，编辑内容仍保留在页面。');}
}
async function loadDeviceRoute(automatic){
  if(currentArmed||routeRecording||flightRouteRunning||routeHold)return;
  const editor=document.getElementById('route-editor');
  const original=editor.value;
  try{
    const response=await fetch('/route/plan',{cache:'no-store'});
    if(response.status===204){routeUploadedText=null;routeUploadedRevision=0;updateRouteControls();if(!automatic)routeMessage('设备尚无已上传序列；浏览器草稿保持不变。');return;}
    if(!response.ok)throw new Error('设备序列读取失败：HTTP '+response.status);
    const revision=Number(response.headers.get('X-Plan-Revision'));
    const text=await response.text();
    if(!Number.isSafeInteger(revision)||revision<=0||!text)throw new Error('设备返回的序列不完整');
    // An automatic refresh must preserve a saved browser draft that differs from the device.
    let synced='';try{synced=localStorage.getItem('cfDroneOpenLoopSyncedText')||'';}catch(_){}
    if(automatic&&original!==defaultRouteText&&original!==text&&original!==synced){
      routeMessage('设备已有序列；浏览器草稿不同，已保留草稿。点击“从设备加载”可覆盖编辑框。');
      return;
    }
    if(!automatic&&original!==defaultRouteText&&original!==text&&
       !window.confirm('从设备加载会覆盖编辑框中的本地内容。继续？'))return;
    // Do not replace edits made while the request was in flight.
    if(editor.value!==original){routeMessage('编辑框已改变，请重新从设备加载。');return;}
    const status=await fetch('/route/status',{cache:'no-store'}).then(r=>r.json());
    if(status.plan_revision!==revision||status.count===0||status.arm){routeMessage('设备序列或解锁状态已变化，请重新加载。');return;}
    editor.value=text;
    routeUploadedText=text;routeUploadedRevision=revision;
    try{localStorage.setItem('cfDroneOpenLoopSequence',text);localStorage.setItem('cfDroneOpenLoopSyncedText',text);}catch(_){}
    routeMessage('已从设备加载 '+status.count+' 段、'+Number(status.duration_s).toFixed(1)+' 秒；修改后请保存到浏览器并重新上传。');
    updateRouteControls();
  }catch(error){if(!automatic)routeMessage(error.message||'无法从设备加载序列');}
}
function loadRoute(){
  let value='';try{value=localStorage.getItem('cfDroneOpenLoopSequence')||'';}catch(_){}
  if(!value){try{const old=JSON.parse(localStorage.getItem('cfDroneOpenLoopRoute')||'[]');if(Array.isArray(old)&&old.length)value=old.map(p=>[p.duration,p.throttle,p.roll,p.pitch,p.yaw].join(' ')).join('\n');}catch(_){}}
  const editor=document.getElementById('route-editor');editor.value=value||defaultRouteText;
  editor.addEventListener('input',()=>{if(routeRecording)return;routeUploadedText=null;routeUploadedRevision=0;routeMessage('内容已修改，请在上锁状态重新上传校验。');updateRouteControls();});
  updateRouteControls();
}
function openRoutePage(){document.getElementById('route-page').style.display='block';document.getElementById('route-page').setAttribute('aria-hidden','false');refreshRouteStatus();startRouteMonitor();}
function closeRoutePage(){document.getElementById('route-page').style.display='none';document.getElementById('route-page').setAttribute('aria-hidden','true');}

function routeRecordStatus(message){const el=document.getElementById('route-record-status');if(el)el.textContent=message;}
function clampRouteValue(value,min,max){return Math.max(min,Math.min(max,value));}
function routeRecordSnapshot(){
  return {
    throttle:clampRouteValue(Math.round((currentValues.throttle+100)/2),0,100),
    roll:clampRouteValue(Math.round(currentValues.roll),-100,100),
    pitch:clampRouteValue(Math.round(currentValues.pitch),-100,100),
    yaw:clampRouteValue(Math.round(currentValues.yaw),-100,100)
  };
}
function sameRouteRecordValue(a,b){return a&&b&&a.throttle===b.throttle&&a.roll===b.roll&&a.pitch===b.pitch&&a.yaw===b.yaw;}
function formatRouteRecordLine(segment){
  return (segment.durationMs/1000).toFixed(1)+' '+segment.throttle+' '+segment.roll+' '+segment.pitch+' '+segment.yaw;
}
function routeRecordText(segments){return segments.length?ROUTE_RECORD_HEADER+'\n'+segments.map(formatRouteRecordLine).join('\n'):'';}
function appendRouteRecordSegment(durationMs,value){
  const roundedMs=Math.max(ROUTE_RECORD_SAMPLE_MS,Math.round(durationMs/ROUTE_RECORD_SAMPLE_MS)*ROUTE_RECORD_SAMPLE_MS);
  if(!routeRecordSegments.length&&value.throttle<ROUTE_RECORD_IDLE_THROTTLE_PCT){
    routeRecordTrimmedMs+=roundedMs;
    return '';
  }
  const segment={durationMs:roundedMs,throttle:value.throttle,roll:value.roll,pitch:value.pitch,yaw:value.yaw};
  const candidate=routeRecordSegments.concat([segment]);
  if(candidate.length>ROUTE_RECORD_MAX_SEGMENTS)return '超过 128 段上限，录制已停止；新片段未写入。';
  const totalMs=candidate.reduce((sum,item)=>sum+item.durationMs,0);
  if(totalMs>ROUTE_RECORD_MAX_DURATION_MS)return '超过 30 分钟上限，录制已停止；新片段未写入。';
  if(new TextEncoder().encode(routeRecordText(candidate)).length>ROUTE_RECORD_MAX_BYTES)return '超过 4096 字节上限，录制已停止；新片段未写入。';
  routeRecordSegments=candidate;
  return '';
}
function routeRecordElapsedText(){
  const now=performance.now();
  const elapsed=Math.max(0,now-routeRecordStartMs);
  return (elapsed/1000).toFixed(1)+' 秒，'+routeRecordSegments.length+' 段';
}
function toggleRouteRecordingMain(){
  if(routeRecording)stopRouteRecording('手动停止录制。',true);
  else startRouteRecording();
}
function startRouteRecording(){
  if(routeRecording)return;
  if(flightRouteRunning||routeHold||routeServerState==='start_pending'){routeRecordStatus('序列执行中，不能录制。');return;}
  if(currentArmed){
    if(!connectionOk){routeRecordStatus('连接断开，不能开始录制。');return;}
    if(currentFlightMode!==2){routeRecordStatus('录制需要自稳模式。');return;}
    if(!webRCLeaseToken||webRCLeaseBlocked){routeRecordStatus('当前页面没有遥控控制权；先操作一次摇杆或刷新页面取得控制权。');return;}
  }
  processJoystickInput();
  routeRecordStartedArmed=currentArmed;
  routeRecording=true;
  if(routeTimer){clearInterval(routeTimer);routeTimer=null;}
  routeRecordSegments=[];
  routeRecordTrimmedMs=0;
  routeRecordLast=routeRecordSnapshot();
  routeRecordStartMs=performance.now();
  routeRecordSegmentStartMs=routeRecordStartMs;
  routeRecordTimer=setInterval(sampleRouteRecording,ROUTE_RECORD_SAMPLE_MS);
  routeRecordStatus((routeRecordStartedArmed?'正在录制手动操作：':'正在本地录制：')+'0.0 秒，0 段。录制期间不发送额外设备消息。');
  routeMessage('正在录制当前摇杆输出；停止后会写入编辑器并保存到浏览器。');
  if(document.getElementById('route-page').getAttribute('aria-hidden')==='false')closeRoutePage();
  showToast('开始录制摇杆序列');
  updateRouteControls();
}
function sampleRouteRecording(){
  if(!routeRecording)return;
  if(routeRecordStartedArmed){
    if(!connectionOk){stopRouteRecording('连接断开，录制已安全停止。',true);return;}
    if(!currentArmed){stopRouteRecording('飞控已上锁，录制已安全停止。',true);return;}
    if(currentFlightMode!==2){stopRouteRecording('飞行模式已切换，录制已安全停止。',true);return;}
    if(!webRCLeaseToken||webRCLeaseBlocked){stopRouteRecording('页面失去遥控控制权，录制已安全停止。',true);return;}
  }else if(currentArmed){stopRouteRecording('飞控已解锁，本地录制已停止。',true);return;}
  processJoystickInput();
  const now=performance.now();
  if(now-routeRecordStartMs>=ROUTE_RECORD_MAX_DURATION_MS-ROUTE_RECORD_SAMPLE_MS){
    stopRouteRecording('接近 30 分钟上限，录制已停止并保存已完成片段。',true);
    return;
  }
  const value=routeRecordSnapshot();
  const segmentMs=now-routeRecordSegmentStartMs;
  if(sameRouteRecordValue(value,routeRecordLast)&&segmentMs<600000){
    routeRecordStatus('正在录制：'+routeRecordElapsedText()+'。');
    return;
  }
  const error=appendRouteRecordSegment(segmentMs,routeRecordLast);
  if(error){stopRouteRecording(error,true,true);return;}
  routeRecordLast=value;
  routeRecordSegmentStartMs=now;
  routeRecordStatus('正在录制：'+routeRecordElapsedText()+'。');
}
function stopRouteRecording(reason,automatic=false,skipOpenSegment=false){
  if(!routeRecording)return;
  const now=performance.now();
  if(routeRecordTimer){clearInterval(routeRecordTimer);routeRecordTimer=null;}
  routeRecording=false;
  routeRecordStartedArmed=false;
  if(!skipOpenSegment&&routeRecordLast){
    const error=appendRouteRecordSegment(now-routeRecordSegmentStartMs,routeRecordLast);
    if(error)reason=error;
  }
  const text=routeRecordText(routeRecordSegments);
  if(text){
    const editor=document.getElementById('route-editor');
    editor.value=text;
    const trimmedText=routeRecordTrimmedMs?' 已裁掉开头 '+(routeRecordTrimmedMs/1000).toFixed(1)+' 秒空油门。':'';
    try{parseRouteText();try{localStorage.setItem('cfDroneOpenLoopSequence',text);routeRecordStatus((reason||'录制已停止。')+' 已写入 '+routeRecordSegments.length+' 段并保存到浏览器。'+trimmedText);}catch(_){routeRecordStatus((reason||'录制已停止。')+' 已写入 '+routeRecordSegments.length+' 段；浏览器未允许本地保存。'+trimmedText);}}
    catch(error){routeRecordStatus((reason||'录制已停止。')+' 生成内容未通过校验：'+error.message);}
    routeUploadedText=null;routeUploadedRevision=0;
    routeMessage('录制结果已写入编辑器'+trimmedText+' 回放将按录制值逐段输入；请上锁后上传并校验。');
  }else{
    routeRecordStatus((reason||'录制已停止。')+' 只有空油门，没有可写入的有效片段；原序列已保留。');
  }
  if(automatic)showToast(reason||'录制已停止');
  if(document.getElementById('route-page').getAttribute('aria-hidden')==='false')startRouteMonitor();
  updateRouteControls();
}

function handleDescentCalibrationEntry(){
  if(descentCalibrationTimer){stopDescentCalibrationCapture();return;}
  openDescentCalibrationPage();
}
function openDescentCalibrationPage(){
  const page=document.getElementById('descent-calibration-page');page.style.display='block';page.setAttribute('aria-hidden','false');
  renderDescentCalibrationPoints();refreshDescentCalibrationStatus();
}
function closeDescentCalibrationPage(){
  const page=document.getElementById('descent-calibration-page');page.style.display='none';page.setAttribute('aria-hidden','true');
}
function setDescentCalibrationRecording(recording){
  if(recording&&!descentCalibrationTimer)descentCalibrationTimer=setInterval(refreshDescentCalibrationStatus,1000);
  if(!recording&&descentCalibrationTimer){clearInterval(descentCalibrationTimer);descentCalibrationTimer=null;}
  const button=document.getElementById('descent-calibration-button');
  button.textContent=recording?'停止标定':'迫降标定';button.classList.toggle('has-fault',recording);
  updateDescentCalibrationControls();
}
function updateDescentCalibrationControls(){
  const recording=!!descentCalibrationTimer;
  document.getElementById('descent-calibration-start').disabled=recording||!connectionOk||!currentArmed||currentFlightMode!==2;
  document.getElementById('descent-calibration-stop').disabled=!recording;
  const status=descentCalibrationLatestStatus;
  document.getElementById('descent-calibration-download').disabled=currentArmed||!status||status.samples===0||status.state==='recording';
  document.getElementById('descent-drop-distance').disabled=!status||!status.usable;
  const button=document.getElementById('descent-calibration-apply');
  if(button){
    const pageOpen=document.getElementById('descent-calibration-page').getAttribute('aria-hidden')==='false';
    button.disabled=!descentCalibrationRecommendation||!pageOpen||!connectionOk||currentArmed;
  }
}
async function refreshDescentCalibrationStatus(){
  try{
    const response=await fetch('/descent-calibration/status',{cache:'no-store'});if(!response.ok)throw new Error('状态读取失败');
    const data=await response.json();descentCalibrationLatestStatus=data;setDescentCalibrationRecording(data.state==='recording');
    const status=document.getElementById('descent-calibration-status');
    const reason=({empty:'尚无标定记录',recording:'正在记录。请关闭标定页，使用摇杆手动下降；从顶部按钮结束记录。',complete:'记录完成',aborted:'记录已中止'})[data.state]||'标定状态未知';
    status.textContent=`${reason}；${data.samples} 个样本，${(Number(data.duration_ms)/1000).toFixed(1)} 秒。`+
      (data.state==='complete'?`中位推力 ${Number(data.median_thrust).toFixed(2)}，平均电池 ${Number(data.mean_battery_v).toFixed(2)} V，最大倾角 ${Number(data.max_tilt_deg).toFixed(1)}°。${data.usable?'记录质量通过检查。':'不可用于标定：'+data.reason+'。'}`:'');
    document.getElementById('descent-calibration-measurement').textContent=data.usable
      ? `记录有效，持续 ${(Number(data.duration_ms)/1000).toFixed(2)} 秒；中位推力 ${Number(data.median_thrust).toFixed(2)}。请输入对应高度差。`
      : `当前记录尚不能用于参数标定${data.reason&&data.state!=='empty'?'：'+data.reason:''}。`;
    updateDescentCalibrationControls();
  }catch(error){document.getElementById('descent-calibration-status').textContent=error.message||'无法读取标定状态';}
}
async function startDescentCalibrationCapture(){
  if(!connectionOk||!currentArmed||currentFlightMode!==2){showToast('请连接飞控并在自稳模式、已解锁状态下开始记录');return;}
  try{
    const response=await controlFetch('/descent-calibration/start',{method:'POST'});const result=await response.json();
    if(!response.ok||!result.ok)throw new Error(result.error||'无法开始记录');
    setDescentCalibrationRecording(true);closeDescentCalibrationPage();showToast('开始记录手动下降；顶部按钮可停止');
  }catch(error){document.getElementById('descent-calibration-status').textContent=error.message;}
}
async function stopDescentCalibrationCapture(){
  try{
    const response=await fetch('/descent-calibration/stop',{method:'POST'});const result=await response.json();
    if(!response.ok||!result.ok)throw new Error(result.error||'当前没有进行中的记录');
    setDescentCalibrationRecording(false);openDescentCalibrationPage();refreshDescentCalibrationStatus();
  }catch(error){showToast(error.message||'停止记录失败');}
}
function readDescentCalibrationPoints(){try{const value=JSON.parse(localStorage.getItem(DESCENT_CALIBRATION_POINTS_KEY)||'[]');return Array.isArray(value)?value:[];}catch(_){return [];}}
function saveDescentCalibrationPoints(points){try{localStorage.setItem(DESCENT_CALIBRATION_POINTS_KEY,JSON.stringify(points.slice(-12)));}catch(_){throw new Error('浏览器无法保存标定点');}}
function renderDescentCalibrationPoints(){
  const points=readDescentCalibrationPoints();const root=document.getElementById('descent-calibration-points');
  root.innerHTML=points.length?points.map((point,index)=>`<div>点 ${index+1}：${Number(point.speed).toFixed(2)} m/s，推力 ${Number(point.thrust).toFixed(2)}，电池 ${Number(point.battery).toFixed(2)} V，${new Date(point.at).toLocaleString()}</div>`).join(''):'此浏览器还没有保存实测点。';
}
async function addDescentCalibrationPoint(){
  const distance=Number(document.getElementById('descent-drop-distance').value);
  if(!Number.isFinite(distance)||distance<0.1||distance>100){showToast('请输入 0.1 到 100 米之间的实测高度差');return;}
  try{
    const response=await fetch('/descent-calibration/status',{cache:'no-store'});const data=await response.json();
    if(!response.ok||!data.usable)throw new Error('当前记录不满足质量条件');
    const duration=Number(data.duration_ms)/1000;const speed=distance/duration;
    if(!Number.isFinite(speed)||speed<=0||speed>5)throw new Error('计算速度超出 0 到 5 m/s 范围，请检查高度差和记录区间');
    const points=readDescentCalibrationPoints();points.push({speed,thrust:Number(data.median_thrust),battery:Number(data.mean_battery_v),tilt:Number(data.max_tilt_deg),at:Date.now()});
    saveDescentCalibrationPoints(points);renderDescentCalibrationPoints();
    document.getElementById('descent-calibration-recommendation').textContent=`已保存实测点：平均下降速度 ${speed.toFixed(2)} m/s，对应推力 ${Number(data.median_thrust).toFixed(2)}。`;
  }catch(error){showToast(error.message||'无法添加实测点');}
}
function recommendDescentCalibrationPoint(){
  const target=Number(document.getElementById('descent-target-speed').value);
  if(!Number.isFinite(target)||target<0.05||target>5){showToast('请输入 0.05 到 5 m/s 的目标最大下降速度');return;}
  const candidates=readDescentCalibrationPoints().filter(point=>Number.isFinite(Number(point.speed))&&Number(point.speed)<=target);
  if(!candidates.length){descentCalibrationRecommendation=null;document.getElementById('descent-calibration-recommendation').textContent='没有速度不超过目标值的实测点；请补充更慢的实测下降数据。';refreshDescentCalibrationSaveState();return;}
  candidates.sort((a,b)=>Number(b.speed)-Number(a.speed));descentCalibrationRecommendation=candidates[0];
  document.getElementById('descent-calibration-recommendation').textContent=`推荐已测点：${Number(descentCalibrationRecommendation.speed).toFixed(2)} m/s，对应 SF_DESCEND_THRUST=${Number(descentCalibrationRecommendation.thrust).toFixed(2)}。不会外推。`;
  refreshDescentCalibrationSaveState();
}
async function refreshDescentCalibrationSaveState(){
  updateDescentCalibrationControls();
}
async function applyDescentCalibrationRecommendation(){
  if(!descentCalibrationRecommendation||currentArmed||!connectionOk){showToast('保存参数前请连接飞控并确认已上锁');return;}
  const value=Number(descentCalibrationRecommendation.thrust);
  try{
    const response=await controlFetch('/descent-calibration/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({value:String(value)})});
    const result=await response.json();if(!response.ok||!result.ok)throw new Error(result.error||'参数保存未排队');
    document.getElementById('descent-calibration-recommendation').textContent='已提交保存；等待飞控写入并确认…';
    for(let i=0;i<12;i++){
      await new Promise(resolve=>setTimeout(resolve,1000));
      const status=await fetch('/descent-calibration/save-status?value='+encodeURIComponent(value),{cache:'no-store'}).then(r=>r.json());
      if(status.saved){document.getElementById('descent-calibration-recommendation').textContent=`参数已写入并读回确认：SF_DESCEND_THRUST=${Number(status.value).toFixed(2)}。`;return;}
    }
    throw new Error('参数仍未确认写入；请检查 NVS 状态，勿重复飞行验证');
  }catch(error){document.getElementById('descent-calibration-recommendation').textContent=error.message||'参数保存失败';}
}
function clearDescentCalibrationPoints(){
  try{localStorage.removeItem(DESCENT_CALIBRATION_POINTS_KEY);}catch(_){}
  descentCalibrationRecommendation=null;renderDescentCalibrationPoints();refreshDescentCalibrationSaveState();
  document.getElementById('descent-calibration-recommendation').textContent='已清除此浏览器保存的实测点。';
}
function downloadDescentCalibrationCsv(){
  if(currentArmed){showToast('请先上锁后下载标定数据');return;}
  const link=document.createElement('a');link.href='/descent-calibration.csv';link.download='cf-drone-descent-calibration.csv';link.click();
}
let levelCalibrationTimer=null;
function openLevelCalibrationPage(){
  const page=document.getElementById('level-calibration-page');page.style.display='block';page.setAttribute('aria-hidden','false');
  refreshLevelCalibrationStatus();
  if(!levelCalibrationTimer)levelCalibrationTimer=setInterval(refreshLevelCalibrationStatus,1000);
}
function closeLevelCalibrationPage(){
  const page=document.getElementById('level-calibration-page');page.style.display='none';page.setAttribute('aria-hidden','true');
  if(levelCalibrationTimer){clearInterval(levelCalibrationTimer);levelCalibrationTimer=null;}
}
async function refreshLevelCalibrationStatus(){
  const status=document.getElementById('level-calibration-status');
  try{
    const response=await fetch('/level-calibration/status',{cache:'no-store'});
    if(!response.ok)throw new Error('飞控未返回水平校准状态');
    const data=await response.json();
    document.getElementById('level-calibration-live').textContent=`当前估计姿态：Roll ${Number(data.roll_deg).toFixed(2)}° · Pitch ${Number(data.pitch_deg).toFixed(2)}°（机身水平时应接近 0°）`;
    const names={empty:'尚未采集',queued:'已排队，等待飞控主循环开始采集',collecting:'正在采集静止 IMU',processing:'正在分块处理 IMU 数据',ready:'数据合格，等待确认',applying:'正在保存',applied:'安装角已应用',rejected:'本次采集未通过',cancelling:'正在取消采集'};
    let detail=names[data.state]||'状态未知';
    if(data.state==='ready'||data.state==='applied'){
      const r=(Number(data.new_rot_roll_rad)-Number(data.old_rot_roll_rad))*180/Math.PI;
      const p=(Number(data.new_rot_pitch_rad)-Number(data.old_rot_pitch_rad))*180/Math.PI;
      detail+=`；采集前重力倾角 Roll ${Number(data.before_roll_deg).toFixed(2)}° / Pitch ${Number(data.before_pitch_deg).toFixed(2)}°；建议安装角变化 X ${r.toFixed(2)}° / Y ${p.toFixed(2)}°；加速度模长 ${Number(data.acc_norm).toFixed(3)} m/s²、最大轴向标准差 ${Number(data.acc_sd).toFixed(3)} m/s²`;
    }
    if(data.state==='rejected')detail+=`（${data.reason}）；请确认机身静止、水平和 IMU 正常后重试`;
    if(data.state==='applied')detail+=data.persist_pending?'；等待参数写入，完成后必须重启飞控才能解锁':'；参数写入已完成，必须重启飞控才能解锁';
    if(data.pending&&data.state!=='applied')detail+='；请求已提交，等待飞控处理';
    status.textContent=detail;
    const busy=['queued','collecting','processing','applying','cancelling'].includes(data.state);
    document.getElementById('level-calibration-start').disabled=data.armed||busy||data.state==='ready'||data.state==='applied';
    document.getElementById('level-calibration-apply').disabled=data.armed||data.state!=='ready';
    const discard=document.getElementById('level-calibration-discard');
    discard.textContent=['queued','collecting','processing','cancelling'].includes(data.state)?'取消采集':'放弃建议';
    discard.disabled=data.state==='applying'||data.state==='applied'||data.state==='cancelling';
  }catch(error){status.textContent=error.message||'水平校准状态读取失败';}
}
async function startLevelCalibration(){
  if(currentArmed){showToast('请先上锁并停止电机');return;}
  if(!window.confirm('请确认机身基准面已用水平仪放平并固定，全部电机停止。开始采集水平基准？'))return;
  try{
    const response=await controlFetch('/level-calibration/start',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({confirm:'1'})});
    const result=await response.json();if(!response.ok||!result.ok)throw new Error(result.error||'采集启动失败');
    document.getElementById('level-calibration-status').textContent='采集请求已提交，等待飞控主循环开始…';
    refreshLevelCalibrationStatus();
  }catch(error){document.getElementById('level-calibration-status').textContent=error.message||'采集启动失败';}
}
async function applyLevelCalibration(){
  if(currentArmed){showToast('请先上锁');return;}
  if(!window.confirm('机身仍保持水平且静止？确认保存刚才测得的 IMU 安装角。'))return;
  try{
    const response=await controlFetch('/level-calibration/apply',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({confirm:'1'})});
    const result=await response.json();if(!response.ok||!result.ok)throw new Error(result.error||'安装角保存失败');
    refreshLevelCalibrationStatus();
  }catch(error){document.getElementById('level-calibration-status').textContent=error.message||'安装角保存失败';}
}
async function discardLevelCalibration(){
  try{
    const response=await controlFetch('/level-calibration/discard',{method:'POST'});
    const result=await response.json();if(!response.ok||!result.ok)throw new Error(result.error||'无法放弃建议');
    refreshLevelCalibrationStatus();
  }catch(error){document.getElementById('level-calibration-status').textContent=error.message||'无法放弃建议';}
}
function openVibrationCalibrationPage(){
  const page=document.getElementById('vibration-calibration-page');page.style.display='block';page.setAttribute('aria-hidden','false');
  refreshVibrationCalibrationStatus();
}
function closeVibrationCalibrationPage(){
  const page=document.getElementById('vibration-calibration-page');page.style.display='none';page.setAttribute('aria-hidden','true');
}
function setVibrationCalibrationPolling(active){
  if(active&&!vibrationCalibrationTimer)vibrationCalibrationTimer=setInterval(refreshVibrationCalibrationStatus,1000);
  if(!active&&vibrationCalibrationTimer){clearInterval(vibrationCalibrationTimer);vibrationCalibrationTimer=null;}
}
function renderVibrationCalibrationResults(motors){
  const complete=motors&&motors.length===4&&motors.every(item=>Number(item.samples)>0);
  const root=document.getElementById('vibration-calibration-results');
  if(!complete){root.textContent='尚无完整四路结果。';return;}
  const values=motors.map(item=>Number(item.accel_rms)).sort((a,b)=>a-b);const median=(values[1]+values[2])/2;
  const outliers=motors.filter(item=>median>0&&Number(item.accel_rms)>median*1.5);
  root.innerHTML='<div><strong>电机　加速度 RMS (m/s²)　陀螺仪 RMS (rad/s)　样本数　相对中位数</strong></div>'+motors.map(item=>{
    const ratio=median>0?Number(item.accel_rms)/median:0;const flag=median>0&&ratio>1.5;
    return `<div>${item.name}　${Number(item.accel_rms).toFixed(4)}　${Number(item.gyro_rms).toFixed(5)}　${item.samples}　${median>0?ratio.toFixed(2)+'×':''}　${item.response==='detected'?'检测到转动响应':'转动未确认'}${flag?'（优先复核）':''}</div>`;
  }).join('');
  document.getElementById('vibration-calibration-analysis').textContent=outliers.length
    ? `${outliers.map(item=>item.name).join('、')} 的加速度 RMS 高于四路中位数 1.5 倍，建议优先复核机械安装、紧固和电机后复测。该相对阈值用于筛查，不是故障判据。`
    : '未发现高于四路中位数 1.5 倍的电机；这不等同于绝对振动合格，也不能排除带桨或飞行状态下的问题。';
}
function renderMotorSelfCheck(data){
  const card=document.getElementById('motor-self-check');
  const button=document.getElementById('motor-self-check-start');
  if(!card||!button)return;
  const active=data.state==='boot_wait'||data.state==='queued'||data.state==='baseline'||data.state==='running';
  button.disabled=active||!connectionOk||currentArmed;
  document.getElementById('motor-self-check-stop').disabled=!active;
  if(data.state==='complete'){
    const motors=Array.isArray(data.motors)?data.motors:[];
    const detected=motors.filter(item=>item.response==='detected').length;
    card.className='diagnostic-summary '+(detected===4?'ok':'offline');
    card.innerHTML=`<strong>四电机自动检测：${detected}/4 路有转动响应</strong><small>${motors.map(item=>`${item.name} ${item.response==='detected'?'有转动响应':'转动未确认'}`).join('；')}。未确认的电机需检查接线、机械耦合并复测。此结果不验证转速、旋向、桨叶或带载推力。</small>`;
  }else if(data.state==='aborted'){
    card.className='diagnostic-summary offline';
    card.innerHTML=`<strong>电机检测未完成</strong><small>原因：${data.reason}。请排查条件后重新采集。</small>`;
  }else if(active){
    card.className='diagnostic-summary offline';
    const phase=data.state==='boot_wait'?'上电自动检测等待期':data.state==='baseline'?'采集静止基线':`已完成 ${data.step}/4 路`;
    card.innerHTML=`<strong>电机检测进行中</strong><small>${phase}；结束前保持机体固定并勿解锁。</small>`;
  }else{
    card.className='diagnostic-summary offline';
    card.innerHTML='<strong>电机响应检测尚未执行</strong><small>拆下全部桨叶并固定机体后，点击下方按钮自动测试四路。</small>';
  }
}
async function refreshVibrationCalibrationStatus(){
  try{
    const response=await fetch('/vibration-calibration/status',{cache:'no-store'});if(!response.ok)throw new Error('状态读取失败');
    const data=await response.json();
    const names=['FR','FL','RR','RL'];
    const stateText=({empty:'尚无记录',boot_wait:'上电自动检测等待中',queued:'已排队，准备启动',baseline:'正在采集静止基线',running:'正在采集',complete:'四路采集完成',aborted:'采集已中止'})[data.state]||'状态未知';
    const step=Math.min(Number(data.step)||0,4);
    document.getElementById('vibration-calibration-status').textContent=`${stateText}；已完成 ${step}/4 路${data.state==='running'?`，当前 ${names[Math.min(step,3)]} 电机测试中`:''}${data.state==='aborted'?'；原因：'+data.reason:''}`;
    document.getElementById('vibration-calibration-start').disabled=data.state==='boot_wait'||data.state==='queued'||data.state==='baseline'||data.state==='running'||!connectionOk||currentArmed;
    document.getElementById('vibration-calibration-stop').disabled=!(data.state==='boot_wait'||data.state==='queued'||data.state==='baseline'||data.state==='running');
    document.getElementById('vibration-calibration-download').disabled=data.state!=='complete'||currentArmed;
    renderVibrationCalibrationResults(data.motors);
    renderMotorSelfCheck(data);
    if(data.state==='boot_wait'||data.state==='queued'||data.state==='baseline'||data.state==='running')setVibrationCalibrationPolling(true);else setVibrationCalibrationPolling(false);
  }catch(error){
    document.getElementById('vibration-calibration-status').textContent=error.message||'无法读取校准状态';
    if(selfCheckOpen)document.getElementById('motor-self-check').innerHTML='<strong>电机检测状态暂不可读</strong><small>请检查连接后刷新。</small>';
  }
}
async function startVibrationCalibration(){
  if(!connectionOk||currentArmed){showToast('请连接飞控并保持上锁');return;}
  if(!window.confirm('请确认：全部桨叶已拆除、机体已固定，周围无人且电机测试区域安全。现在依次执行四路 5%、约 100 ms 短脉冲？'))return;
  try{
    const response=await controlFetch('/vibration-calibration/start',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({confirm:'1'})});
    const result=await response.json();if(!response.ok||!result.ok)throw new Error(result.error||'无法开始采集');
    setVibrationCalibrationPolling(true);refreshVibrationCalibrationStatus();
  }catch(error){
    document.getElementById('vibration-calibration-status').textContent=error.message||'启动失败';
    if(selfCheckOpen)document.getElementById('motor-self-check').innerHTML=`<strong>电机检测未启动</strong><small>${error.message||'启动失败'}</small>`;
  }
}
function stopMotorSelfCheck(){
  sendButtonData(1,1);
  setTimeout(()=>sendButtonData(1,0),100);
  showToast('已发送上锁与停止检测指令');
  setTimeout(refreshVibrationCalibrationStatus,300);
}
function downloadVibrationCalibrationCsv(){
  if(currentArmed){showToast('请先确认飞控已上锁');return;}
  const link=document.createElement('a');link.href='/vibration-calibration.csv';link.download='cf-drone-vibration-calibration.csv';link.click();
}
async function uploadRoute(){
  if(routeStarting||routePending)return;
  if(currentArmed||!connectionOk){routeMessage('上传前请连接飞控并保持上锁、电机停止。');return;}
  try{parseRouteText();}catch(error){routeMessage(error.message);return;}
  const text=document.getElementById('route-editor').value;
  routeUploadedText=null;routeUploadedRevision=0;
  routeStarting=true;updateRouteControls();routeMessage('正在上传并校验；不会解锁或启动。');
  try{
    const response=await controlFetch('/route/upload',{method:'POST',headers:{'Content-Type':'text/plain'},body:text});
    const result=await response.json();if(!response.ok||!result.ok)throw new Error(result.error||'序列上传失败');
    const readback=await fetch('/route/plan',{cache:'no-store'});
    if(!readback.ok||Number(readback.headers.get('X-Plan-Revision'))!==result.plan_revision||await readback.text()!==text)
      throw new Error('设备读回与上传内容不一致，请勿启动序列');
    routeUploadedText=text;routeUploadedRevision=result.plan_revision;
    try{localStorage.setItem('cfDroneOpenLoopSequence',text);localStorage.setItem('cfDroneOpenLoopSyncedText',text);}catch(_){}
    routeMessage('已上传校验。进入 AUTO 模式后由操作者解锁即可执行。');
    await refreshRouteStatus();
  }catch(error){routeMessage('上传未完成：'+error.message);}
  finally{routeStarting=false;updateRouteControls();}
}
async function setAutoModeForRoute(){
  if(routeRecording){routeMessage('请先停止录制。');return;}
  if(routeStarting||routePending)return;
  if(currentArmed||!connectionOk){routeMessage('进入 AUTO 前请连接飞控并保持上锁。');return;}
  if(!routeUploadedRevision||routeUploadedText!==document.getElementById('route-editor').value||routeServerState!=='ready'){routeMessage('请先在上锁状态上传并校验当前内容。');return;}
  routePending='auto';updateRouteControls();
  routeMessage('正在请求 AUTO 模式，并等待飞控状态确认…');
  try{
    sendButtonData(9,1);
    setTimeout(()=>sendButtonData(9,0),100);
    let confirmed=false;
    for(let i=0;i<8;i++){
      await new Promise(resolve=>setTimeout(resolve,250));
      const status=await refreshRouteStatus();
      if((status&&status.mode===4)||currentFlightMode===4){confirmed=true;break;}
    }
    routeMessage(confirmed?'AUTO 模式已由飞控状态确认；返回遥控器后解锁，飞控会自动执行已上传序列。':'尚未从飞控状态确认 AUTO 模式；请查看顶部飞行模式后再解锁。');
  }finally{
    routePending='';updateRouteControls();
  }
}
async function requestRouteAction(action){
  if(routePending)return;
  routePending=action;updateRouteControls();
  routeMessage('正在请求手动接管，等待飞控确认…');
  try{
    const options={method:'POST'};
    const response=await controlFetch('/route/'+action,options),result=await response.json();
    if(!response.ok||!result.ok)throw new Error(result.error||'飞控拒绝请求');
    startRouteMonitor();await refreshRouteStatus();
  }catch(error){routeMessage('请求未确认：'+error.message+'；请以飞控状态为准。');}
  finally{routePending='';updateRouteControls();}
}
function startRouteMonitor(){if(!routeTimer)routeTimer=setInterval(refreshRouteStatus,500);}
async function refreshRouteStatus(){
  if(routeStatusBusy)return null;
  routeStatusBusy=true;
  try{
    const response=await fetch('/route/status',{cache:'no-store'});if(!response.ok)throw new Error('状态不可用');
    const data=await response.json();routeServerState=data.state;
    if(data.arm!==undefined)setArmedState(data.arm);
    if(data.mode!==undefined){if(routeRecording&&routeRecordStartedArmed&&data.mode!==2)stopRouteRecording('飞行模式已切换，录制已安全停止。',true);currentFlightMode=data.mode;document.getElementById('flight-mode').textContent=['直控','特技','自稳','不支持','自动'][data.mode]||'未知';}
    flightRouteRunning=data.state==='running'||data.state==='start_pending';routeHold=flightRouteRunning||data.state==='landing';
    if(routeUploadedRevision&&data.plan_revision!==routeUploadedRevision){routeUploadedRevision=0;routeUploadedText=null;routeMessage('飞控中的序列已改变，请上锁后重新上传当前内容。');}
    const planKind=data.recorded?'录制输入':'手写输入';
    const messages={empty:'尚无已上传的动作序列。',ready:`已校验${planKind} ${data.count} 段，共 ${Number(data.duration_s).toFixed(1)} 秒；等待操作者启动。`,start_pending:'正在确认启动条件…',running:`飞控本机回放${planKind}：第 ${data.step}/${data.count} 段，共 ${Number(data.duration_s).toFixed(1)} 秒。`,landing:'已停止动作序列，正在保持定推力下降；无法检测触地，需操作者上锁。',complete:'序列已停止，飞控已上锁；这不代表传感器确认着陆。',aborted:'序列已退出，控制已交还当前手动模式。'};
    document.getElementById('route-status').textContent=(messages[data.state]||'状态未知')+(data.pending?' 正在处理请求…':'')+(data.reason?' 原因：'+routeReason(data.reason):'');
    if(!routeHold&&document.getElementById('route-page').getAttribute('aria-hidden')==='true'&&routeTimer){clearInterval(routeTimer);routeTimer=null;}
    updateRouteControls();return data;
  }catch(_){document.getElementById('route-status').textContent='无法确认飞控状态；已上传序列可能仍在本机执行。请恢复连接。';return null;}
  finally{routeStatusBusy=false;}
}
function routeReason(reason){return ({sequence_complete:'动作段已执行完毕',operator_landing:'操作者触发迫降',takeover_requested:'手动接管',manual_takeover:'切换到手动模式',disarmed:'已上锁',mode_changed:'切换模式',scheduler_gap:'执行周期中断，已转下降',multiple_expired_segments:'错过多个动作段，已转下降',landing_interrupted:'下降流程被接管',revision_mismatch:'上传批次已变化',requires_armed_stab:'启动条件不满足',web_rc_link_required:'启动时遥控连接已超时'})[reason]||reason;}

async function confirmArmButton(buttonIndex, warning){
  const expectedArmed=buttonIndex===0;
  let actual=null;
  for(let i=0;i<6;i++){
    const state=await refreshRouteStatus();
    if(state&&state.arm!==undefined){
      actual=!!state.arm;
      if(actual===expectedArmed)break;
    }
    if(i<5)await new Promise(resolve=>setTimeout(resolve,150));
  }
  if(warning)showToast('⚠️ '+warning);
  else if(actual===expectedArmed)showToast(expectedArmed?'✅ 已解锁':'🔒 已上锁');
  else showToast(expectedArmed?'❌ 飞控仍显示上锁，请查看自检状态':'⚠️ 未能确认上锁状态');
}
function takeManualControl(){if(flightRouteRunning||routeHold)requestRouteAction('takeover');}

function initKnobPositions() {
  // 根据初始 rawY 将旋鈕定位到正确位置（左摇杆油门在底部）
  const joystick = document.getElementById('joystick-left');
  const knob     = document.getElementById('knob-left');
  const rect     = joystick.getBoundingClientRect();
  if (rect.width === 0) { requestAnimationFrame(initKnobPositions); return; } // 布局未就绪则重试
  const radius = rect.width / 2 - 10;
  const dy = -leftStick.rawY / 100 * radius; // rawY=-100 → dy=radius（底部）
  knob.style.transform = `translate(calc(-50% + 0px), calc(-50% + ${dy}px))`;
}

function initButtons() {
  const container = document.getElementById('buttons-container');
  container.innerHTML = '';
  buttonConfigs.forEach((cfg, idx) => {
    const btn = document.createElement('button');
    btn.className = 'button';
    btn.id = idx === 5 ? 'route-record-main' : `btn-${idx}`;
    btn.title = cfg.desc;
    btn.innerHTML = `<div class="button-icon">${cfg.icon}</div><div class="button-label">${cfg.label}</div>`;
    btn.style.border = `2px solid ${cfg.color}55`;
    btn.addEventListener('pointerdown', e => {
      e.preventDefault();
      handleButton(idx);
      if (navigator.vibrate) navigator.vibrate(20);
    });
    container.appendChild(btn);
  });
}

function initNetwork() {
  updateConnectionStatus(true);
  setInterval(updateNetworkStatus, 2000);
}

function initPointerEvents() {
  [{ id:'joystick-left', side:'left' }, { id:'joystick-right', side:'right' }].forEach(js => {
    const el = document.getElementById(js.id);
    el.addEventListener('pointerdown', e => { e.preventDefault(); handlePointerStart(e, js.side); el.setPointerCapture(e.pointerId); });
    el.addEventListener('pointermove', e => { e.preventDefault(); handlePointerMove(e, js.side); });
    el.addEventListener('pointerup',   e => { e.preventDefault(); handlePointerEnd(e, js.side); });
    el.addEventListener('pointercancel', e => { e.preventDefault(); handlePointerEnd(e, js.side); });
    el.addEventListener('touchstart', e => e.preventDefault());
    el.addEventListener('touchmove',  e => e.preventDefault());
  });
}

/*======================== 动画循环 ========================*/
function animationLoop(timestamp) {
  if (timestamp - lastAnimationTime >= SEND_INTERVAL) {
    processJoystickInput();
    checkAndSendChanges();
    updateDisplayAll();
    lastAnimationTime = timestamp;
  }
  requestAnimationFrame(animationLoop);
}

/*======================== 摇杆曲线处理 ========================*/
// 仅做 expo 曲线，死区由后端 stickDeadzone 统一处理（对齐 SBUS 模式）
function applyCurve(value) {
  const absVal = Math.abs(value);
  const expoFactor = EXPO / 100;
  let curved = absVal * (1 - expoFactor) + Math.pow(absVal, 3) * expoFactor;
  return curved * (value >= 0 ? 1 : -1);
}

/*======================== 摇杆数据处理 ========================*/
function processJoystickInput() {
  if(flightRouteRunning||routeHold)return;
  // 发送原始值，后端统一完成映射（对齐SBUS/MAVLink模式，避免双重映射）
  // 油门：rawY∈[-100,+100]，后端 processThrottle: (raw+100)/(2*RAW_MAX)*100 → 0~100%
  // 姿态轴：归一化到[-1,1]做指数曲线后还原×100，后端除以RAW_MAX得[-1,1]
  currentValues.throttle = leftStick.rawY;
  currentValues.yaw   = applyCurve(leftStick.rawX  / 100) * 100;  // 右推=顺时针，与MAVLink一致
  currentValues.pitch = applyCurve(rightStick.rawY / 100) * 100;  // 上推=前进，与MAVLink一致
  currentValues.roll  = applyCurve(rightStick.rawX / 100) * 100;  // 右推=右滚，与MAVLink一致
  leftStick.x  = currentValues.yaw;
  leftStick.y  = leftStick.rawY;
  rightStick.x = currentValues.roll;
  rightStick.y = currentValues.pitch;
}

function hasSignificantChange(nv) {
  return Math.abs(nv.throttle - lastSentValues.throttle) > MIN_CHANGE_THRESHOLD ||
         Math.abs(nv.roll     - lastSentValues.roll)     > MIN_CHANGE_THRESHOLD ||
         Math.abs(nv.pitch    - lastSentValues.pitch)    > MIN_CHANGE_THRESHOLD ||
         Math.abs(nv.yaw      - lastSentValues.yaw)      > MIN_CHANGE_THRESHOLD;
}

function checkAndSendChanges() {
  if(flightRouteRunning||routeHold)return;
  const now = performance.now();
  // 有变化立即发；或超过强制重发间隔时也发一次（保持飞控侧数据新鲜，避免超时断连）
  if (hasSignificantChange(currentValues) || (now - lastForceSentTime >= FORCE_SEND_INTERVAL)) {
    sendJoystickData();
    lastForceSentTime = now;
  }
  for (let i = 0; i < buttonStates.length; i++) {
    if (buttonStates[i] !== lastButtonStates[i]) {
      sendButtonData(i, buttonStates[i]);
      lastButtonStates[i] = buttonStates[i];
    }
  }
}

/*======================== 数据发送函数 ========================*/
function sendJoystickData() {
  sendToESP('/web_rc', { t:1, th:Math.round(currentValues.throttle), r:Math.round(currentValues.roll),
    p:Math.round(currentValues.pitch), y:Math.round(currentValues.yaw), ts:performance.now() });
  lastSentValues = {...currentValues};
  packetStats.sent++;
}

function sendButtonData(buttonIndex, state) {
  if (state && (buttonIndex === 1 || buttonIndex === 2 || buttonIndex === 3) && !webRCStopToken) {
    showToast('本页面没有停机凭证；请使用当前遥控页面或实体急停。');
    return;
  }
  if (state && webRCStopToken && (buttonIndex === 1 || buttonIndex === 2 || buttonIndex === 3)) {
    const action = buttonIndex === 2 ? 'kill' : buttonIndex === 3 ? 'land' : 'lock';
    fetch(`${location.protocol}//${location.hostname}:82/${action}?s=${encodeURIComponent(webRCStopToken)}`,
      {method:'POST', mode:'no-cors', keepalive:true}).catch(() => {});
  }
  sendToESP('/web_rc', { t:2, b:buttonIndex, s:state ? 1 : 0, ts:performance.now() });
}

/*======================== 显示更新 ========================*/
function updateDisplayAll() {
  stickReadoutElements.throttle.textContent = Math.round((currentValues.throttle + 100) / 2);
  stickReadoutElements.yaw.textContent = Math.round(leftStick.x);
  stickReadoutElements.roll.textContent = Math.round(rightStick.x);
  stickReadoutElements.pitch.textContent = Math.round(rightStick.y);
}

/*======================== Pointer Events 处理 ========================*/
function handlePointerStart(e, side) {
  if(flightRouteRunning||routeHold){showToast('请先点击接管摇杆，再操作摇杆');return;}
  touches.set(e.pointerId, side);
  document.getElementById(`joystick-${side}`).classList.add('active');
  updateJoystickPosition(side, e.clientX, e.clientY);
}

function handlePointerMove(e, side) {
  if (touches.get(e.pointerId) === side) updateJoystickPosition(side, e.clientX, e.clientY);
}

function handlePointerEnd(e, side) {
  if (touches.get(e.pointerId) !== side) return;
  touches.delete(e.pointerId);
  const knob     = document.getElementById(`knob-${side}`);
  const joystick = document.getElementById(`joystick-${side}`);
  joystick.classList.remove('active');
  // 虚拟油门松手保持最后值；偏航和右侧姿态轴回中。
  const targetRawY = side === 'left' ? leftStick.rawY : 0;
  const radius = joystick.getBoundingClientRect().width / 2 - 10;
  const targetDy = -targetRawY / 100 * radius;
  knob.style.transition = 'transform 0.2s ease-out';
  knob.style.transform  = `translate(calc(-50% + 0px), calc(-50% + ${targetDy}px))`;
  setTimeout(() => { knob.style.transition = ''; }, 200);
  if (side === 'left')  leftStick  = {x:0, y:leftStick.y, rawX:0, rawY: targetRawY};
  else                  rightStick = {x:0, y:0, rawX:0, rawY:0};
  processJoystickInput();
  sendJoystickData();
}

function updateJoystickPosition(side, clientX, clientY) {
  const joystick = document.getElementById(`joystick-${side}`);
  const knob     = document.getElementById(`knob-${side}`);
  const rect     = joystick.getBoundingClientRect();
  const cx = rect.width / 2, cy = rect.height / 2;
  const radius = cx - 10;
  let dx = (clientX - rect.left) - cx;
  let dy = (clientY - rect.top)  - cy;
  const dist = Math.sqrt(dx*dx + dy*dy);
  if (dist > radius) { dx = dx/dist*radius; dy = dy/dist*radius; }
  knob.style.transform = `translate(calc(-50% + ${dx}px), calc(-50% + ${dy}px))`;
  if (side === 'left')  { leftStick.rawX  =  dx/radius*100; leftStick.rawY  = -dy/radius*100; }
  else                  { rightStick.rawX =  dx/radius*100; rightStick.rawY = -dy/radius*100; }
}

/*======================== 网络处理 ========================*/
function controlUrl(url) {
  if (!webRCLeaseToken) return url;
  return url + (url.includes('?') ? '&' : '?') + 'lease=' + encodeURIComponent(webRCLeaseToken);
}

function handleLeaseConflict(message) {
  const shouldNotify = !webRCLeaseBlocked;
  webRCLeaseToken = '';
  webRCLeaseBlocked = true;
  if(routeRecording&&routeRecordStartedArmed)stopRouteRecording('页面失去遥控控制权，录制已安全停止。',true);
  updateConnectionStatus(false);
  if (shouldNotify) showToast(message || '另一个遥控页面正在控制；关闭其他页面或等待 10 秒后重试');
}

async function acquireControlLease() {
  if (webRCLeaseToken) return true;
  if (webRCLeaseBlocked) return false;
  if (webRCLeasePromise) return webRCLeasePromise;
  const leaseUrl = '/web_rc/lease' + (webRCStopToken ? '?stop=' + encodeURIComponent(webRCStopToken) : '');
  webRCLeasePromise = fetch(leaseUrl, {method:'POST', cache:'no-store'})
    .then(async response => {
      const data = await response.json().catch(() => ({}));
      if (!response.ok || !data.lease) {
        const retry = Number(data.retry_ms || 0);
        handleLeaseConflict(retry > 0 ? `另一个遥控页面正在控制；约 ${Math.ceil(retry/1000)} 秒后刷新页面可接管` : '另一个遥控页面正在控制；刷新页面可重新申请');
        return false;
      }
      webRCLeaseToken = data.lease;
      webRCStopToken = data.stop || '';
      try { sessionStorage.setItem('cfDroneStopToken', webRCStopToken); } catch (_) {}
      webRCLeaseBlocked = false;
      updateRouteControls();
      return true;
    })
    .catch(() => {
      if (++consecutiveFails >= 3) updateConnectionStatus(false);
      return false;
    })
    .finally(() => { webRCLeasePromise = null; });
  return webRCLeasePromise;
}

async function controlFetch(url, options={}) {
  const ok = await acquireControlLease();
  if (!ok) throw new Error('web_rc_lease_required');
  const response = await fetch(controlUrl(url), options);
  if (response.status === 409) {
    let data = {};
    try { data = await response.clone().json(); } catch (_) {}
    if (data.error === 'web_rc_lease_required' || data.error === 'web_rc_lease_in_use') {
      handleLeaseConflict('遥控控制权已被其他页面占用');
    }
  }
  return response;
}

function isEmergencyButtonData(data) {
  return data && data.t === 2 && (data.b === 1 || data.b === 2 || data.b === 3);
}

function sendToESP(url, data) {
  const t0 = performance.now();
  const emergencyOverride = isEmergencyButtonData(data);
  (emergencyOverride ? Promise.resolve(true) : acquireControlLease()).then(ok => {
    if (!ok) return null;
    if (emergencyOverride) data.stop = webRCStopToken;
    else data.lease = webRCLeaseToken;
    return fetch(emergencyOverride ? url : controlUrl(url), { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(data) });
  })
    .then(r => {
      if (!r) return null;
      if (r.status === 409) {
        return r.json().catch(() => ({})).then(data => {
          if (data.error === 'web_rc_lease_required' || data.error === 'web_rc_lease_in_use') handleLeaseConflict('遥控控制权已被其他页面占用');
          throw new Error('lease');
        });
      }
      if (!r.ok) throw new Error();
      updateLatency(performance.now() - t0);
      return r.json();
    })
    .then(resp => {
      if (!resp) return;
      consecutiveFails = 0;
      updateConnectionStatus(true);
      const names = ['直控','特技','自稳','不支持','自动'];

      // 模式切换结果
      if (resp.m !== undefined && resp.rt !== 2) {
      if (routeRecording && routeRecordStartedArmed && resp.m !== 2) stopRouteRecording('飞行模式已切换，录制已安全停止。', true);
      if (resp.m !== currentFlightMode) {
        if (!resp.warn) showToast('✅ 已切换：' + (names[resp.m] || '未知'));
        const leftTouched = [...touches.values()].includes('left');
        if (!leftTouched && !flightRouteRunning && !routeHold) resetLeftStick(leftStick.rawY);
        }
        currentFlightMode = resp.m;
        document.getElementById('flight-mode').textContent = names[resp.m] || '自稳';
      }

      // ARM 状态更新（所有响应都同步显示）
      if (resp.arm !== undefined && resp.rt !== 2) {
        setArmedState(resp.arm);
        const el   = document.getElementById('armed-status');
        const item = document.getElementById('armed-status-item');
        el.textContent = resp.arm ? '已解锁' : '已上锁';
        item.style.background = resp.arm ? 'rgba(0,255,136,0.15)' : 'rgba(255,51,51,0.15)';
        el.style.color = resp.arm ? '#00ff88' : '#ff6666';
      }

      updateRouteControls();
      refreshDescentCalibrationSaveState();
      // 按钮松开确认 toast（rt=2, bs=0）
      if (resp.rt === 2 && resp.bs === 0) {
        if (resp.bi >= 0 && resp.bi <= 2) {
          showToast('正在确认飞控状态…');
          setTimeout(() => confirmArmButton(resp.bi, resp.warn), 120);
        } else if (resp.bi === 3) {
          showToast(resp.warn ? '⚠️ ' + resp.warn : '🛬 迫降流程已启动');
        }
      }
      // 心跳包携带的系统警告（低电自动上锁等），不与按钮 toast 冲突
      if (resp.rt === 4 && resp.warn) showToast('⚠️ ' + resp.warn);
    })
    .catch(() => {
      if (++consecutiveFails >= 3) updateConnectionStatus(false);
    });
}

function updateLatency(latency) {
  latencyHistory[latencyIndex] = latency;
  latencyIndex = (latencyIndex + 1) % latencyHistory.length;
  const avg = latencyHistory.reduce((a,b)=>a+b) / latencyHistory.length;
  document.getElementById('latency').textContent = Math.round(avg) + 'ms';
  const dot = document.querySelector('.status-dot');
  dot.className = avg > 200 ? 'status-dot warning' : avg > 100 ? 'status-dot warning' : 'status-dot connected';
}

function updateNetworkStatus() {
  const lr = packetStats.sent > 0 ? (packetStats.lost/packetStats.sent*100).toFixed(1) : '0';
  document.getElementById('packet-loss').textContent = lr + '%';
  loadSelfCheckStatus(false);
}

const diagnosticChecks = [
  {bit:1,   name:'IMU 初始化',   advice:'检查 IMU 供电、SPI 接线和传感器型号。'},
  {bit:2,   name:'IMU 数据超时', advice:'检查 IMU 通信、数据就绪信号和供电。'},
  {bit:4,   name:'IMU 数据有效性', advice:'检查传感器数据、安装方向和校准参数。'},
  {bit:8,   name:'电机输出初始化', advice:'拆下螺旋桨后检查电机引脚、PWM 配置和接线。'},
  {bit:16,  name:'遥控链路', advice:'检查接收机供电、协议、串口引脚和遥控链路。'},
  {bit:32,  name:'网页遥控链路', advice:'检查遥控页面连接、Wi-Fi 和摇杆数据是否持续发送。'},
  {bit:64,  name:'电池电压', advice:'检查电池电量、分压电阻和 ADC 引脚。'},
  {bit:128, name:'控制循环时序', advice:'检查循环负载、通信请求和日志输出是否过重。'},
  {bit:256, name:'参数有效性', advice:'检查参数值；修正后重启并重新查看自检状态。'},
  {bit:512, name:'AUTO 目标超时', advice:'检查外部 AUTO 目标流和模式切换状态。'},
  {bit:1024,name:'机体倒置', advice:'机体倒置告警；保持锁定并检查姿态。'}
];

function openSelfCheck() {
  selfCheckOpen = true;
  const page = document.getElementById('diagnostic-page');
  page.style.display = 'block';
  page.setAttribute('aria-hidden', 'false');
  refreshSelfCheck();
  refreshVibrationCalibrationStatus();
}

function closeSelfCheck() {
  selfCheckOpen = false;
  const page = document.getElementById('diagnostic-page');
  page.style.display = 'none';
  page.setAttribute('aria-hidden', 'true');
}

function refreshSelfCheck() {
  loadSelfCheckStatus(true);
}

function loadSelfCheckStatus(showLoading) {
  const requestId = ++selfCheckRequestSequence;
  const summary = document.getElementById('diagnostic-summary');
  if (showLoading && !selfCheckHasData) {
    summary.className = 'diagnostic-summary offline';
    summary.innerHTML = '<strong>正在读取诊断状态…</strong><small>数据来自飞控当前运行状态。</small>';
  }
  fetch('/web_rc/status', {cache:'no-store'}).then(r => {
    if (!r.ok) throw new Error('status unavailable');
    return r.json();
  }).then(data => {
    if (requestId !== selfCheckRequestSequence) return;
    if (typeof data.faults !== 'number') throw new Error('diagnostics unsupported');
    if (typeof data.armed === 'boolean') setArmedState(data.armed);
    selfCheckHasData = true;
    renderSelfCheckStatus(data);
    if (data.voltage !== undefined && data.voltage > 0.5)
      document.getElementById('battery').textContent = Number(data.voltage).toFixed(2) + 'V';
  }).catch(() => {
    if (requestId !== selfCheckRequestSequence) return;
    document.getElementById('battery').textContent = '-';
    const ledReason = document.getElementById('led-alert-reason');
    ledReason.className = 'diagnostic-summary offline';
    ledReason.innerHTML = '<strong>无法确认蓝灯快闪原因</strong><small>飞控状态读取失败；请检查 USB/Wi-Fi 链路后刷新。</small>';
    if (!selfCheckHasData) showSelfCheckUnavailable();
    else document.getElementById('diagnostic-updated').textContent = '读取失败，保留上次故障结果';
  });
}

function renderSelfCheckStatus(data) {
  const faults = data.faults;
  if (typeof faults !== 'number') return;
  const active = diagnosticChecks.filter(check => (faults & check.bit) !== 0);
  const armReadiness = document.getElementById('arm-readiness');
  if (typeof data.arm_ready === 'boolean' && typeof data.arm_reason === 'string') {
    armReadiness.className = 'diagnostic-summary ' + (data.arm_ready ? 'ok' : 'fault');
    armReadiness.innerHTML = data.armed === true
      ? '<strong>飞控已解锁</strong><small>飞控报告当前处于解锁状态。</small>'
      : data.arm_ready
      ? '<strong>当前可以解锁</strong><small>飞控报告所有解锁门槛均已满足；状态会随电池、油门和飞行模式变化。</small>'
      : `<strong>当前禁止解锁</strong><small>${data.arm_reason}</small>`;
  } else {
    armReadiness.className = 'diagnostic-summary offline';
    armReadiness.innerHTML = '<strong>当前固件未提供解锁条件</strong><small>刷新或更新飞控固件后，页面才能显示具体解锁阻止原因。</small>';
  }
  const ledReason = document.getElementById('led-alert-reason');
  const blockingFaults = diagnosticChecks.filter(check =>
    [1, 2, 4, 8, 256].includes(check.bit) && (faults & check.bit) !== 0);
  const voltage = Number(data.voltage);
  const lowBattery = data.armed === true
    ? (faults & 64) !== 0
    : Number.isFinite(voltage) && voltage > 0.5 && voltage < 3.5;
  const ledCauses = [];
  if (lowBattery) ledCauses.push(`电池低压：${voltage.toFixed(2)} V`);
  const nonBatteryCauses = data.armed === true
    ? active.filter(check => check.bit !== 64)
    : blockingFaults;
  nonBatteryCauses.forEach(check =>
    ledCauses.push(`${data.armed === true ? '活动告警' : '阻止解锁故障'}：${check.name}`));
  if (data.armed === true) {
    if (data.led_fast_blink === true && ledCauses.length) {
      ledReason.className = 'diagnostic-summary fault';
      ledReason.innerHTML = `<strong>蓝灯快闪原因（已解锁）</strong><small>${ledCauses.join('<br>')}</small>`;
    } else if (data.led_fast_blink === true) {
      ledReason.className = 'diagnostic-summary fault';
      ledReason.innerHTML = '<strong>飞控报告蓝灯快闪</strong><small>LED 判定与故障字段不一致，请检查固件版本。</small>';
    } else {
      ledReason.className = 'diagnostic-summary ok';
      ledReason.innerHTML = '<strong>飞控当前未报告蓝灯快闪</strong><small>已解锁但无活动告警；若实体灯仍快闪，请刷新状态并确认固件版本。</small>';
    }
  } else if (data.led_fast_blink === true && ledCauses.length) {
    ledReason.className = 'diagnostic-summary fault';
    ledReason.innerHTML = `<strong>蓝灯快闪原因（当前锁定状态）</strong><small>${ledCauses.join('<br>')}</small>`;
  } else if (data.led_fast_blink === true) {
    ledReason.className = 'diagnostic-summary fault';
    ledReason.innerHTML = '<strong>飞控报告蓝灯应快闪</strong><small>当前故障位和电压读数未能对应到具体触发项，请确认固件版本并刷新。</small>';
  } else if (!Number.isFinite(voltage) || voltage <= 0.5) {
    ledReason.className = 'diagnostic-summary offline';
    ledReason.innerHTML = '<strong>暂未找到蓝灯快闪触发项</strong><small>未检测到阻止解锁故障；电池电压无有效读数，因此无法排除电池告警。请检查电压采样并刷新。</small>';
  } else {
    ledReason.className = 'diagnostic-summary ok';
    ledReason.innerHTML = `<strong>当前数据未显示蓝灯快闪</strong><small>电池 ${voltage.toFixed(2)} V，且没有阻止解锁故障。若蓝灯仍快闪，请刷新或确认固件版本。</small>`;
  }
  const summary = document.getElementById('diagnostic-summary');
  const button = document.getElementById('self-check-button');
  button.classList.toggle('has-fault', active.length > 0);
  summary.className = 'diagnostic-summary ' + (active.length ? 'fault' : 'ok');
  summary.innerHTML = active.length
    ? `<strong>检测到 ${active.length} 项活动故障</strong><small>如故障涉及 IMU 或电机输出，请勿解锁。处理建议见下方。</small>`
    : '<strong>启动与运行自检正常：未报告活动故障</strong><small>解锁条件和四电机转动响应检测结果分别见下方；电机输出初始化正常不代表电机已经转动。</small>';
  const activePanel = document.getElementById('diagnostic-active');
  activePanel.style.display = active.length ? 'block' : 'none';
  activePanel.innerHTML = active.length
    ? `<strong>当前活动故障（位掩码 0x${(faults >>> 0).toString(16).toUpperCase().padStart(8, '0')}）</strong><ul>${active.map(check => `<li><b>${check.name}</b>：${check.advice}</li>`).join('')}</ul>`
    : '';
  document.getElementById('diagnostic-list').innerHTML = diagnosticChecks.map(check => {
    const isActive = (faults & check.bit) !== 0;
    return `<article class="diagnostic-item${isActive ? ' active' : ''}">
      <div class="diagnostic-item-title"><span>${check.name}</span><span class="diagnostic-item-state">${isActive ? '故障' : '未触发'}</span></div>
      <p>${isActive ? check.advice : '当前没有检测到此项故障。'}</p>
    </article>`;
  }).join('');
  document.getElementById('diagnostic-updated').textContent = '最近更新：' + new Date().toLocaleTimeString();
}

function showSelfCheckUnavailable() {
  if (!selfCheckOpen) return;
  const summary = document.getElementById('diagnostic-summary');
  summary.className = 'diagnostic-summary offline';
  summary.innerHTML = '<strong>暂时无法读取自检结果</strong><small>请检查与飞控的连接，或确认当前固件已提供诊断数据。</small>';
  document.getElementById('diagnostic-list').innerHTML = '';
  const activePanel = document.getElementById('diagnostic-active');
  activePanel.style.display = 'none';
  activePanel.innerHTML = '';
  document.getElementById('diagnostic-updated').textContent = '读取失败';
}

function updateConnectionStatus(connected) {
  connectionOk = connected;
  if (!connected) armedStatusKnown = false;
  if (!connected && routeRecording && routeRecordStartedArmed) stopRouteRecording('连接断开，录制已安全停止。', true);
  const dot  = document.getElementById('status-dot');
  const text = document.getElementById('connection-text');
  if (connected) { dot.className='status-dot connected'; text.textContent='已连接'; text.style.color='#0f8'; }
  else           { dot.className='status-dot disconnected'; text.textContent='连接断开'; text.style.color='#f33'; }
  updateRouteControls();
}
/*======================== 油门位置重置 ========================*/
function resetLeftStick(targetRawY) {
  const knob     = document.getElementById('knob-left');
  const joystick = document.getElementById('joystick-left');
  const rect     = joystick.getBoundingClientRect();
  if (rect.width === 0) return;
  const radius = rect.width / 2 - 10;
  const dy = -targetRawY / 100 * radius;
  knob.style.transition = 'transform 0.3s ease-out';
  knob.style.transform  = `translate(calc(-50% + 0px), calc(-50% + ${dy}px))`;
  setTimeout(() => { knob.style.transition = ''; }, 300);
  leftStick = {x:0, y:0, rawX:0, rawY: targetRawY};
  processJoystickInput();
  sendJoystickData();
}
/*======================== 按钮处理 ========================*/
function handleButton(idx) {
  if (idx === 5) { toggleRouteRecordingMain(); return; }
  if (idx === 4) {
    // 六轴模式循环：自稳(2) ↔ 特技(1)
    // 不在点击时弹 toast，结果完全依赖后端 resp.m 确认后触发
    let nextBit;
    if (currentFlightMode === 2)      nextBit = 7; // STAB→ACRO
    else                              nextBit = 6; // 其他→STAB
    sendButtonData(nextBit, 1);
    setTimeout(() => sendButtonData(nextBit, 0), 100);
    if (navigator.vibrate) navigator.vibrate(30);
    return;
  }
  // 解锁/上锁/急停：先发按下（state=1），100ms后发松开（state=0）
  // 后端响应中携带 rt/bi/bs，前端用这些字段判断 toast，无需 lastPressedButton
  if (idx === 3) {
    if (!currentArmed) { showToast('请确认飞控已解锁'); return; }
    showToast('🛬 迫降指令发送中…');
    sendButtonData(idx, 1);
    setTimeout(() => sendButtonData(idx, 0), 100);
    if (navigator.vibrate) navigator.vibrate([40, 40, 80]);
    return;
  }
  if (idx === 0 || idx === 1 || idx === 2) {
    if (idx === 0)      showToast('🔓 解锁中...');
    else if (idx === 1) showToast('🔒 上锁中...');
    else if (idx === 2) showToast('🛑 急停指令发送中...');
    sendButtonData(idx, 1);
    setTimeout(() => sendButtonData(idx, 0), 100);
    if (navigator.vibrate) navigator.vibrate(30);
  }
}

/*======================== Toast 提示 ========================*/
function showToast(msg) {
  let t = document.getElementById('toast-msg');
  if (!t) {
    t = document.createElement('div');
    t.id = 'toast-msg';
    t.style.cssText = 'position:fixed;top:50%;left:50%;transform:translate(-50%,-50%);background:rgba(0,0,0,.85);color:#fff;padding:12px 20px;border-radius:10px;font-size:14px;z-index:9999;pointer-events:none;text-align:center;max-width:80vw;border:1px solid rgba(255,255,255,.2);transition:opacity .3s';
    document.body.appendChild(t);
  }
  t.textContent = msg; t.style.opacity = '1';
  clearTimeout(t._timer);
  t._timer = setTimeout(() => { t.style.opacity = '0'; }, 2000);
}

/*======================== 调试控制台 ========================*/
function toggleConsole() {
  const panel = document.getElementById('console-window');
  const btn   = document.getElementById('console-open-button');
  const open  = !consolePanelOpen;
  if (open && (!armedStatusKnown || currentArmed)) { showToast('请确认飞控已上锁后打开调试控制台'); return; }
  consolePanelOpen = open;
  panel.setAttribute('aria-hidden',open?'false':'true');
  if (btn) { open ? btn.classList.add('active') : btn.classList.remove('active'); }
  if (open) {
    document.getElementById('console-close-button').focus();
    document.getElementById('console-output').innerHTML = '';
    consoleLastTotal = 0;
    controlFetch('/console/enable', {method:'POST'}).catch(()=>{});
    fetchConsoleLogs();
  } else {
    controlFetch('/console/disable', {method:'POST'}).catch(()=>{});
    clearTimeout(consolePollingTimer);
    consolePollingTimer = null;
    btn?.focus();
  }
}

document.getElementById('console-window').addEventListener('click',event=>{
  if(event.target.id==='console-window')toggleConsole();
});

document.addEventListener('keydown',event=>{
  if(event.key==='Escape'&&consolePanelOpen)toggleConsole();
});

function initConsoleTouchScrolling() {
  const output = document.getElementById('console-output');
  let lastTouchY = 0;
  output.addEventListener('touchstart', event => {
    if (event.touches.length) lastTouchY = event.touches[0].clientY;
  }, {passive:true});
  output.addEventListener('touchmove', event => {
    if (!consolePanelOpen || !event.touches.length) return;
    const currentY = event.touches[0].clientY;
    output.scrollTop -= currentY - lastTouchY;
    lastTouchY = currentY;
    event.preventDefault();
  }, {passive:false});
}

function downloadConsoleLogs() {
  const output = document.getElementById('console-output');
  const lines = Array.from(output.children, line => line.textContent);
  if (!lines.length) {
    showToast('当前没有可下载的调试日志');
    return;
  }
  const blob = new Blob([lines.join('\n') + '\n'], {type:'text/plain;charset=utf-8'});
  const link = document.createElement('a');
  link.href = URL.createObjectURL(blob);
  link.download = 'cf-drone-console-' + new Date().toISOString().replace(/[:.]/g, '-') + '.txt';
  link.click();
  setTimeout(() => URL.revokeObjectURL(link.href), 1000);
}

function scheduleConsolePoll(delayMs) {
  if (!consolePanelOpen) return;
  clearTimeout(consolePollingTimer);
  consolePollingTimer = setTimeout(() => {
    consolePollingTimer = null;
    fetchConsoleLogs();
  }, delayMs);
}

function fetchConsoleLogs() {
  if (!consolePanelOpen) return;
  if (consoleFetchInFlight) {
    scheduleConsolePoll(CONSOLE_CATCHUP_POLL_MS);
    return;
  }

  clearTimeout(consolePollingTimer);
  consolePollingTimer = null;
  consoleFetchInFlight = true;
  let nextPollDelay = CONSOLE_BASE_POLL_MS;
  const controller = new AbortController();
  const requestTimeout = setTimeout(() => controller.abort(), CONSOLE_REQUEST_TIMEOUT_MS);
  fetch('/console?since=' + consoleLastTotal + '&limit=' + CONSOLE_PAGE_LIMIT,
    {signal:controller.signal, cache:'no-store'}).then(r=>{
      if (!r.ok) throw new Error('console log request failed');
      return r.json();
    }).then(data => {
    const out = document.getElementById('console-output');
    if (data.lines && data.lines.length > 0) {
      const stickToBottom = out.scrollTop + out.clientHeight >= out.scrollHeight - 12;
      const frag = document.createDocumentFragment();
      data.lines.forEach(l => {
        const div = document.createElement('div');
        div.textContent = l;
        frag.appendChild(div);
      });
      out.appendChild(frag);
      if (stickToBottom) out.scrollTop = out.scrollHeight;
      // 限制 DOM 行数，避免长时间运行内存泄漏
      while (out.children.length > 200) out.removeChild(out.firstChild);
    }

    if (typeof data.next === 'number') consoleLastTotal = data.next;
    else if (typeof data.total === 'number') consoleLastTotal = data.total;

    nextPollDelay = data.has_more ? CONSOLE_CATCHUP_POLL_MS : CONSOLE_BASE_POLL_MS;
  }).catch(()=>{
    nextPollDelay = CONSOLE_BASE_POLL_MS;
  }).finally(() => {
    clearTimeout(requestTimeout);
    consoleFetchInFlight = false;
    if (consolePanelOpen) scheduleConsolePoll(nextPollDelay);
  });
}

function sendConsoleCmd() {
  const input = document.getElementById('console-input');
  const cmd = input.value.trim();
  if (!cmd) return;
  input.value = '';
  controlFetch('/console/cmd', {method:'POST', headers:{'Content-Type':'text/plain'}, body:cmd})
    .then(r => r.json())
    .then(resp => {
      if (!resp.ok) {
        if (resp.e === 'queue full') showToast('⚠️ 命令队列已满，请稍后重试');
        return;
      }
      fetchConsoleLogs();
    }).catch(()=>{});
}

// 回车发送 + ↑/↓ 命令历史
(function(){
  const cmdHistory = [];
  let historyIdx   = -1;
  document.getElementById('console-input').addEventListener('keydown', function(e) {
    if (e.key === 'Enter') {
      e.preventDefault();
      const v = this.value.trim();
      if (v) { cmdHistory.unshift(v); if (cmdHistory.length > 20) cmdHistory.pop(); }
      sendConsoleCmd();
      historyIdx = -1;
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      if (historyIdx < cmdHistory.length - 1) { historyIdx++; this.value = cmdHistory[historyIdx]; }
    } else if (e.key === 'ArrowDown') {
      e.preventDefault();
      if (historyIdx > 0) { historyIdx--; this.value = cmdHistory[historyIdx]; }
      else { historyIdx = -1; this.value = ''; }
    }
  });
})();

/*======================== 事件绑定 ========================*/
document.addEventListener('DOMContentLoaded', init);
document.addEventListener('contextmenu', e => e.preventDefault());

// 心跳：2000ms，连续3次失败才判定断连
setInterval(() => {
  if (connectionOk) sendToESP('/web_rc/heartbeat', {t:4, ts:performance.now()});
}, 2000);
</script>
</body>
</html>
)rawliteral";
#endif
