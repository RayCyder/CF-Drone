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
.descent-recording-banner{display:none;align-items:center;justify-content:center;gap:10px;padding:8px 12px;border:1px solid rgba(255,166,68,.7);border-radius:10px;background:rgba(137,66,0,.92);color:#fff3df;font-size:.8rem;font-weight:bold;box-shadow:0 5px 18px rgba(0,0,0,.35)}
.descent-recording-banner.active{display:flex}.descent-recording-banner button{border:1px solid rgba(255,255,255,.55);border-radius:7px;background:#fff;color:#7d3600;padding:6px 11px;font-weight:bold;touch-action:manipulation;cursor:pointer}
#console-open-button{border-color:rgba(110,190,255,.65);background:rgba(30,120,200,.16);color:#c4e6ff}
.route-page{position:fixed;inset:0;z-index:1001;display:none;background:#252525;overflow-y:auto;padding:clamp(14px,4vw,28px);touch-action:pan-y}
.descent-calibration-page{position:fixed;inset:0;z-index:1002;display:none;background:#252525;overflow-y:auto;padding:clamp(14px,4vw,28px);touch-action:pan-y}
.vibration-calibration-page{position:fixed;inset:0;z-index:1003;display:none;background:#252525;overflow-y:auto;padding:clamp(14px,4vw,28px);touch-action:pan-y}
.descent-calibration-shell{max-width:860px;margin:0 auto;display:flex;flex-direction:column;gap:12px}
.calibration-card{border:1px solid rgba(255,255,255,.14);border-radius:12px;padding:14px;background:rgba(0,0,0,.25)}
.calibration-card p,.calibration-card small{color:#c4cbd3;font-size:.85rem;line-height:1.5}
.calibration-actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
.calibration-actions button{border:0;border-radius:8px;padding:10px 14px;background:#444;color:#fff;font-size:.9rem;touch-action:manipulation}
.calibration-actions button.primary{background:#a85b00}.calibration-actions button:disabled{opacity:.45;cursor:not-allowed}
.calibration-step-heading{display:flex;align-items:center;gap:9px;margin-bottom:10px}.calibration-step-heading strong{font-size:.95rem}.calibration-step-number{display:grid;place-items:center;flex:0 0 26px;height:26px;border-radius:50%;background:#a85b00;color:#fff;font-size:.8rem;font-weight:bold}
.calibration-form-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px;align-items:start}
.calibration-field{display:flex;flex-direction:column;gap:5px;min-width:0;color:#e6edf3;font-size:.78rem;touch-action:pan-y}
.calibration-field>span{font-weight:bold}.calibration-field small{font-size:.68rem;color:#94a3af;line-height:1.35}
.calibration-field input{width:100%;min-height:44px;background:#17191c;color:#fff;border:1px solid #697781;border-radius:8px;padding:9px 10px;font-size:1rem;touch-action:auto;user-select:text;-webkit-user-select:text}
.calibration-field input:focus{outline:2px solid rgba(255,166,68,.55);border-color:#f0a24e}.calibration-field input:disabled{opacity:.55;background:#292929}
.calibration-form-actions{display:flex;align-items:flex-end;gap:8px;min-height:44px}.calibration-form-actions button{width:100%;min-height:44px;border:0;border-radius:8px;padding:9px 12px;background:#555;color:#fff;font-size:.88rem;touch-action:manipulation}.calibration-form-actions button.primary{background:#a85b00}.calibration-form-actions button:disabled{opacity:.45;cursor:not-allowed}
.calibration-actions button:focus-visible,.calibration-form-actions button:focus-visible,.calibration-limit summary:focus-visible{outline:2px solid #ffc477;outline-offset:2px}
.calibration-inline-status{margin-top:10px;padding:9px 10px;border-radius:8px;background:rgba(40,92,133,.18);border:1px solid rgba(100,180,255,.2);color:#acd4f5;font-size:.8rem;line-height:1.45}
.calibration-points{display:grid;gap:7px;font-size:.78rem;color:#d3d9e0;margin:9px 0}.calibration-points>div{padding:8px 10px;border:1px solid rgba(255,255,255,.1);border-radius:8px;background:rgba(0,0,0,.2);line-height:1.4}
.calibration-limit{border:1px solid rgba(255,255,255,.13);border-radius:10px;padding:11px 12px;background:rgba(0,0,0,.18)}.calibration-limit summary{cursor:pointer;color:#d4dbe2;font-weight:bold;touch-action:manipulation}.calibration-limit p{margin-top:8px}
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
.console-tools{display:flex;flex-direction:column;gap:7px;padding:8px;border:1px solid rgba(110,190,255,.25);border-radius:8px;background:rgba(30,90,140,.12)}
.console-tools-row{display:flex;flex-wrap:wrap;gap:6px;align-items:center}
.console-tools-row strong{font-size:.72rem;color:#c4e6ff;margin-right:2px}
.console-tools button{border:1px solid #526b7d;border-radius:6px;background:#263640;color:#e9f6ff;padding:5px 8px;font-size:.68rem;cursor:pointer;touch-action:manipulation}
.console-tools button.primary{background:#1c5e8d;border-color:#58a9df}
.console-tools-note{font-size:.64rem;line-height:1.35;color:#aebdca}
.console-status{min-height:1.1em;font-size:.66rem;color:#9ab0bf}
.console-status.ok{color:#62d895}.console-status.error{color:#ff8a8a}.console-status.busy{color:#ffd166}
.console-command-row{display:flex;gap:6px;touch-action:pan-y}

/*======== 内环 PID 调参 ========*/
.pid-window{position:fixed;inset:0;z-index:1020;display:none;align-items:center;justify-content:center;padding:16px;background:rgba(0,0,0,.76);touch-action:pan-y}
.pid-window[aria-hidden="false"]{display:flex}
.pid-dialog{width:min(760px,100%);max-height:92dvh;overflow:auto;padding:14px;background:#17191c;border:1px solid rgba(255,255,255,.2);border-radius:14px;box-shadow:0 16px 48px rgba(0,0,0,.65);touch-action:pan-y}
.pid-header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:10px}
.pid-header h2{font-size:1.05rem}.pid-header button,.pid-actions button{border:1px solid #66717a;border-radius:7px;background:#333;color:#fff;padding:7px 12px;cursor:pointer;touch-action:manipulation}
.pid-intro{color:#c8d2db;font-size:.78rem;line-height:1.45;margin-bottom:9px}
.pid-help{display:grid;grid-template-columns:repeat(3,1fr);gap:7px;margin-bottom:10px}
.pid-help div{padding:8px;border:1px solid rgba(100,180,255,.25);border-radius:8px;background:rgba(30,90,140,.12);font-size:.7rem;line-height:1.35;color:#c9dced}
.pid-help strong{color:#fff}.pid-sync{display:flex;align-items:center;gap:7px;margin:7px 0 9px;font-size:.75rem;color:#d7e5ef;touch-action:manipulation}
.pid-sync input{width:17px;height:17px;touch-action:manipulation}
.pid-grid{display:grid;grid-template-columns:minmax(74px,.8fr) repeat(3,minmax(92px,1fr));gap:7px;align-items:end}
.pid-grid-head{font-size:.7rem;color:#9fb0bd;text-align:center;padding-bottom:2px}
.pid-axis{font-size:.78rem;font-weight:bold;color:#e9f6ff;align-self:center}.pid-axis small{display:block;font-size:.62rem;font-weight:normal;color:#92a5b4;margin-top:2px}
.pid-field{display:flex;flex-direction:column;gap:3px;font-size:.61rem;color:#91a6b7;touch-action:pan-y}
.pid-field input{width:100%;border:1px solid #60717e;border-radius:7px;background:#101418;color:#fff;padding:8px;font: .78rem ui-monospace,SFMono-Regular,Consolas,monospace;touch-action:auto;user-select:text;-webkit-user-select:text}
.pid-field input:focus{outline:2px solid rgba(77,166,255,.55);border-color:#65aef0}.pid-field input.invalid{border-color:#ff6868;background:#351919}
.pid-status{min-height:1.3em;margin:10px 0 8px;font-size:.72rem;color:#aebdca}.pid-status.ok{color:#62d895}.pid-status.error{color:#ff8a8a}.pid-status.busy{color:#ffd166}
.pid-actions{display:flex;justify-content:flex-end;gap:7px;flex-wrap:wrap}.pid-actions button.primary{background:#176a9e;border-color:#65b9ed}.pid-actions button:disabled{opacity:.45;cursor:not-allowed}
.pid-footnote{margin-top:8px;color:#8fa0ad;font-size:.64rem;line-height:1.4}

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
  #route-page-button,#descent-calibration-button,#console-open-button{padding:5px 7px;font-size:.68rem}
  .status-bar{gap:5px;flex-wrap:wrap;justify-content:center}
  .status-item{font-size:clamp(0.6rem,2.5vw,0.7rem);padding:2px 5px}
}
@media (max-width:420px){
  .pid-window{padding:7px}.pid-dialog{padding:10px}.pid-help{gap:4px}.pid-help div{padding:5px;font-size:.62rem}
  .pid-grid{grid-template-columns:48px repeat(3,minmax(0,1fr));gap:4px}.pid-axis{font-size:.68rem}.pid-axis small{font-size:.55rem}
  .pid-field input{padding:7px 4px;font-size:.68rem}.pid-field span{font-size:.52rem}.pid-actions{justify-content:stretch}.pid-actions button{flex:1;padding:7px 4px}
  .calibration-form-grid{grid-template-columns:1fr}.calibration-actions button{flex:1}.calibration-form-actions{width:100%}
}

/*======== 小屏横屏自适应（高度≤480px）========*/
@media (max-height:480px) and (orientation:landscape){
  :root{--js-size:clamp(88px,min(36vw,40dvh),240px);--knob-size:calc(var(--js-size)*0.25);--pad:clamp(3px,.9vmin,7px);--gap:clamp(3px,.9vmin,7px)}
  .container{height:100dvh;min-height:0;overflow:hidden}
  .header{flex:0 0 auto;padding:3px 6px}
  .header h1{display:none}
  .header-tools{gap:4px;flex-wrap:nowrap;margin:0 0 3px}
  .header-tools .self-check-button{padding:3px 6px;font-size:.62rem}
  .descent-recording-banner{padding:4px 8px;font-size:.65rem}.descent-recording-banner button{padding:4px 8px;font-size:.65rem}
  .status-bar{margin-top:0;gap:3px;flex-wrap:nowrap;overflow:hidden}
  .status-item{gap:2px;font-size:0.55rem;padding:1px 3px}
  .content{min-height:0}
  .joystick-container{min-width:0;min-height:0;padding:4px;border-radius:12px}
  .joystick-title{font-size:0.62rem;white-space:nowrap}
  .stick-readouts{gap:4px;margin-top:2px}
  .stick-readout{padding:1px 4px;font-size:.58rem}
  .joystick-wrapper{min-height:0;margin-top:2px}
  .buttons-container{min-width:0;min-height:0;gap:4px;padding:5px;border-radius:12px}
  .buttons-grid{min-height:0;gap:4px}
  .button{min-height:0;font-size:0.62rem;padding:2px;border-radius:7px}
  .button-icon{font-size:0.8rem;margin-bottom:0}
  .footer{display:none}
  .console-window{padding:5px}
  .console-dialog{width:min(960px,98vw);height:96dvh;padding:7px;gap:5px}
  .console-panel{display:grid;grid-template-columns:minmax(220px,38%) minmax(0,1fr);grid-template-rows:minmax(0,1fr) auto;gap:6px;padding:6px}
  .console-tools{grid-column:1;grid-row:1/3;min-height:0;overflow-y:auto;padding:6px}
  .console-output{grid-column:2;grid-row:1;min-width:0;min-height:0}
  .console-command-row{grid-column:2;grid-row:2;min-width:0}
  .console-dialog-header h2{font-size:.85rem}
  .console-dialog-header button{padding:4px 9px}
  .pid-window{padding:5px}.pid-dialog{width:min(980px,99vw);max-height:97dvh;padding:8px}
  .pid-header{margin-bottom:5px}.pid-header h2{font-size:.88rem}.pid-header button{padding:4px 9px}
  .pid-intro{font-size:.65rem;margin-bottom:5px}.pid-help{grid-template-columns:repeat(3,1fr);gap:4px;margin-bottom:5px}.pid-help div{padding:4px;font-size:.58rem}
  .pid-sync{margin:4px 0;font-size:.64rem}.pid-grid{grid-template-columns:70px repeat(3,minmax(80px,1fr));gap:4px}.pid-field input{padding:5px;font-size:.7rem}
  .pid-status{margin:5px 0;font-size:.64rem}.pid-actions button{padding:4px 8px;font-size:.68rem}.pid-footnote{margin-top:4px;font-size:.56rem}
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
      <button id="pid-button" class="self-check-button" onclick="openPidPanel()">PID</button>
      <button id="console-open-button" class="self-check-button" onclick="toggleConsole()">调试</button>
      <button id="wifi-settings-button" class="self-check-button" onclick="openWifiSettings()">Wi-Fi 模式</button>
    </nav>
    <div class="status-bar">
      <div class="status-item"><span class="status-dot" id="status-dot"></span><span id="connection-text">连接中...</span></div>
      <div class="status-item" id="armed-status-item" style="background:rgba(255,51,51,0.15)"><span id="armed-status" style="color:#ff6666">已上锁</span></div>
      <div class="status-item"><span>飞行模式</span><span id="flight-mode">自稳</span></div>
      <button id="route-takeover-main" class="status-item" style="display:none;background:#167c3a;color:white;border:0" onclick="takeManualControl()">接管摇杆</button>
      <div class="status-item"><span>电池电压</span><span id="battery">-</span></div>
      <div class="status-item"><span>遥控延迟</span><span id="latency">-</span></div>
      <div class="status-item"><span>丢包率</span><span id="packet-loss">0%</span></div>
    </div>
  </div>
  <div id="descent-recording-banner" class="descent-recording-banner" role="status" aria-live="assertive" aria-hidden="true">
    <span id="descent-recording-banner-text">迫降标定已开启，等待起飞。</span>
    <button type="button" onclick="abortDescentCalibrationCapture()">取消标定</button>
  </div>

  <div class="content">
    <!-- 左摇杆 -->
    <div class="joystick-container">
      <div class="joystick-title">左摇杆 (油门/偏航；松手回悬停油门 <span id="hover-throttle-label">50</span>%)</div>
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
    <div class="console-tools">
      <div class="console-tools-row"><strong>调试工具</strong><button class="primary" onclick="openVibrationCalibrationFromConsole()">电机扰动检测</button><button class="primary" onclick="startAccelCalibrationFromConsole()">六面加速度计校准</button><button class="primary" onclick="openLevelCalibrationFromConsole()">机身水平校准</button></div>
      <div class="console-tools-row"><strong>常用命令</strong><button onclick="runConsoleCommand('diag brief')">快速预检</button><button onclick="runConsoleCommand('diag')">完整诊断</button><button onclick="runConsoleCommand('imu')">IMU</button><button onclick="runConsoleCommand('sensors')">扩展传感器</button><button onclick="runConsoleCommand('nav')">融合导航</button><button onclick="runConsoleCommand('ps')">姿态</button><button onclick="runConsoleCommand('p CTL_TRIM_ROLL')">横滚配平值</button><button onclick="runConsoleCommand('p CTL_TRIM_PITCH')">俯仰配平值</button><button onclick="runConsoleCommand('rc')">遥控输入</button><button onclick="runConsoleCommand('mot')">电机输出</button><button onclick="runConsoleCommand('wifi')">Wi-Fi</button><button onclick="runConsoleCommand('time')">循环时间</button><button onclick="runConsoleCommand('sys')">系统任务</button><button onclick="runConsoleCommand('log status')">日志状态</button><button onclick="runConsoleCommand('p')">参数列表</button><button onclick="runConsoleCommand('help')">命令帮助</button><button onclick="restartFromConsole()">重启</button></div>
	  <div class="console-tools-row"><strong>磁力计</strong><button onclick="runConsoleCommand('magcal status')">状态</button><button onclick="runConsoleCommand('magcal start')">开始采集</button><button onclick="runConsoleCommand('magcal stop')">停止采集</button><button onclick="runConsoleCommand('magcal save')">保存校准</button><button onclick="runConsoleCommand('magcal reset')">清除校准</button></div>
      <div class="console-tools-note">磁力计为可选传感器；未安装磁力计仍可进行六面加速度计校准和机身水平校准，仅磁航向与 magcal 不可用。机身静置水平但姿态不为 0°：使用“机身水平校准”修正 IMU 安装角。只有实际飞行松杆后持续漂移时，才调整 CTL_TRIM_ROLL / CTL_TRIM_PITCH。</div>
      <div class="console-tools-note"><strong>PID 调整：</strong>使用顶部“PID”按钮集中修改 Roll、Pitch、Yaw 的内环 P/I/D；控制台“参数列表”仍可用于核对全部参数。</div>
      <div id="console-status" class="console-status" role="status">打开后将主动确认飞控处于上锁状态。</div>
    </div>
    <div id="console-output" class="console-output"></div>
    <div class="console-command-row">
      <input id="console-input" placeholder="输入命令 (diag brief / imu / rc / help)..."
        style="flex:1;background:rgba(0,0,0,.6);border:1px solid rgba(100,100,100,.5);border-radius:6px;color:#0f8;padding:4px 8px;font-size:0.7rem;font-family:'Courier New',monospace;touch-action:auto">
      <button onclick="downloadConsoleLogs()" style="background:#444;border:1px solid #777;border-radius:6px;color:#fff;padding:4px 8px;font-size:0.7rem;cursor:pointer;touch-action:manipulation;white-space:nowrap">下载日志</button>
      <button onclick="sendConsoleCmd()" style="background:#1a73e8;border:none;border-radius:6px;color:#fff;padding:4px 10px;font-size:0.7rem;cursor:pointer;touch-action:auto">发送</button>
    </div>
      </div>
    </div>
  </section>
  <section id="pid-window" class="pid-window" role="dialog" aria-modal="true" aria-labelledby="pid-title" aria-hidden="true">
    <div class="pid-dialog">
      <div class="pid-header"><h2 id="pid-title">角速度内环 PID</h2><button type="button" onclick="closePidPanel()">关闭</button></div>
      <p class="pid-intro">调整 Roll、Pitch、Yaw 三个轴的角速度响应。保存只允许在飞控上锁且电机停止时进行；外环姿态参数和积分限幅 WU 保持固件当前值。</p>
      <div class="pid-help" aria-label="PID 参数简要说明">
        <div><strong>P 比例</strong><br>决定跟随力度。偏低响应软，偏高容易快速振荡。</div>
        <div><strong>I 积分</strong><br>消除持续偏差。偏高可能慢速摆动并积累过量修正。</div>
        <div><strong>D 微分</strong><br>抑制快速变化。偏高会放大噪声，并可能使电机发热。</div>
      </div>
      <label class="pid-sync"><input id="pid-sync-roll-pitch" type="checkbox" checked> Roll / Pitch 同步修改相同类型参数</label>
      <div class="pid-grid" id="pid-grid">
        <div></div><div class="pid-grid-head">P 比例</div><div class="pid-grid-head">I 积分</div><div class="pid-grid-head">D 微分</div>
        <div class="pid-axis">Roll<small>横滚</small></div>
        <label class="pid-field"><input id="pid-roll-p" type="number" inputmode="decimal" step="0.001"><span id="pid-roll-p-range"></span></label>
        <label class="pid-field"><input id="pid-roll-i" type="number" inputmode="decimal" step="0.001"><span id="pid-roll-i-range"></span></label>
        <label class="pid-field"><input id="pid-roll-d" type="number" inputmode="decimal" step="0.0001"><span id="pid-roll-d-range"></span></label>
        <div class="pid-axis">Pitch<small>俯仰</small></div>
        <label class="pid-field"><input id="pid-pitch-p" type="number" inputmode="decimal" step="0.001"><span id="pid-pitch-p-range"></span></label>
        <label class="pid-field"><input id="pid-pitch-i" type="number" inputmode="decimal" step="0.001"><span id="pid-pitch-i-range"></span></label>
        <label class="pid-field"><input id="pid-pitch-d" type="number" inputmode="decimal" step="0.0001"><span id="pid-pitch-d-range"></span></label>
        <div class="pid-axis">Yaw<small>偏航</small></div>
        <label class="pid-field"><input id="pid-yaw-p" type="number" inputmode="decimal" step="0.001"><span id="pid-yaw-p-range"></span></label>
        <label class="pid-field"><input id="pid-yaw-i" type="number" inputmode="decimal" step="0.001"><span id="pid-yaw-i-range"></span></label>
        <label class="pid-field"><input id="pid-yaw-d" type="number" inputmode="decimal" step="0.0001"><span id="pid-yaw-d-range"></span></label>
      </div>
      <div id="pid-status" class="pid-status" role="status">打开后读取飞控当前参数。</div>
      <div class="pid-actions"><button type="button" onclick="loadPidConfig()">重新读取</button><button type="button" onclick="restorePidEdits()">撤销编辑</button><button id="pid-save-button" class="primary" type="button" onclick="savePidConfig()">保存参数</button></div>
      <p class="pid-footnote">每次只做小幅调整，并在低高度、空旷环境逐轴验证。快速高频抖动通常先降低 P 或 D；持续缓慢偏差再少量调整 I。</p>
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
      <div id="calibration-readiness" class="diagnostic-summary offline">
        <strong>正在检查起飞前标定…</strong>
        <small>检查陀螺静止校准、六面加速度计校准和机身水平校准。</small>
      </div>
	  <div id="compass-status" class="diagnostic-summary offline">
		<strong>正在读取磁航向状态…</strong>
		<small>芯片已检测不等于航向可信。</small>
	  </div>
      <div id="expansion-probe-status" class="diagnostic-summary offline">等待光流与测距探测状态。</div>
      <div id="barometer-status" class="diagnostic-summary offline">
        <strong>正在读取气压高度保护…</strong>
        <small>气压计为可选传感器，不影响基础手动解锁。</small>
      </div>
      <div id="led-alert-reason" class="diagnostic-summary offline">
        <strong>正在读取蓝灯状态…</strong>
      </div>
      <div id="diagnostic-active" class="diagnostic-active" style="display:none"></div>
      <div id="diagnostic-list" class="diagnostic-list"></div>
      <section id="motor-self-check" class="diagnostic-summary offline">
        <strong>电机响应检测尚未执行</strong>
        <small>固定机体并确保周围安全后，可启动四路低功率自动检测。</small>
      </section>
      <div class="diagnostic-actions"><button id="motor-self-check-start" onclick="startVibrationCalibration()">启动四电机自动检测</button><button id="motor-self-check-stop" onclick="stopMotorSelfCheck()" disabled>停止检测</button></div>
      <div id="diagnostic-updated" class="diagnostic-updated">尚未获取</div>
    </div>
  </section>
  <section id="route-page" class="route-page" aria-hidden="true">
    <div class="route-shell">
      <div class="diagnostic-top"><h2>相对航线（遥控输出序列）</h2><div class="diagnostic-actions"><button onclick="closeRoutePage()">返回遥控器</button></div></div>
	      <p class="route-intro">V2 录制会把板端融合高度与相对航向写入关键段；回放时高度与相对航向闭环，横滚/俯仰仍按录制输入执行。光流水平位置当前只记录诊断，完成安装方向和尺度标定前不会直接控制水平航迹。手动接管会切回自稳。</p>
      <div class="route-state-card" aria-live="polite"><strong>当前飞控状态</strong><div class="route-status" id="route-status">正在读取飞控状态…</div><div class="route-status" id="route-message" role="status">当前内容尚未上传；上传不会解锁或启动。</div></div>

      <section class="route-step" aria-labelledby="route-step-edit-title">
        <div class="route-step-heading"><span class="route-step-number">1</span><h3 id="route-step-edit-title">录制或编辑序列</h3></div>
        <p class="route-step-note">可直接操作摇杆录制，也可在下方编辑每段遥控输入。</p>
        <div class="route-recorder"><div id="route-record-status" class="route-recorder-status">录制关闭；只在浏览器本地采样当前摇杆输出。</div><div class="route-recorder-actions"><button id="route-record-start" class="record" onclick="startRouteRecording()">开始录制</button><button id="route-record-stop" class="stop" onclick="stopRouteRecording('手动停止录制。')" disabled>停止录制</button></div></div>
        <textarea id="route-editor" class="route-editor" spellcheck="false" aria-label="开环控制序列"></textarea>
	        <details class="route-details"><summary>序列格式与录制规则</summary><p class="route-help">V1 每行 5 列：持续秒数、油门%、横滚、俯仰、偏航。V2 每行再增加融合相对高度 m、相对航向 °。持续时间 0.1–600 秒，油门 0–100%，姿态输入 -100–100；最多 128 段、总时长 30 分钟、正文 4096 字节。</p><p class="route-help">飞行中录制需要自稳模式和当前页面控制权。页面以 10 Hz 请求板端高度/航向快照，并在高度变化约 3 cm、航向变化约 1°或摇杆改变时生成关键段。若高度快照失效，该段会退回 V1 开环行为。</p></details>
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

	      <details class="route-details"><summary>回放行为与能力边界</summary><p class="route-help">V2 回放以当前起点重新对齐首个高度与航向，随后闭环跟踪相对变化。无健康高度估计时禁止启动或转入迫降；无磁力计时航向来自陀螺积分，只适合短时相对转向。横向仍不是坐标航点闭环。无桨测试只能验证估计、目标、调度、混控和安全接管，不能证明真实定高或落点精度。</p></details>
    </div>
  </section>
  <section id="descent-calibration-page" class="descent-calibration-page" aria-hidden="true">
    <div class="descent-calibration-shell">
      <div class="diagnostic-top"><h2>迫降推力标定</h2><div class="diagnostic-actions"><button onclick="closeDescentCalibrationPage()">返回遥控器</button></div></div>
      <section class="calibration-card" aria-labelledby="descent-step-start"><div class="calibration-step-heading"><span class="calibration-step-number">1</span><strong id="descent-step-start">起飞前开启</strong></div><p>保持上锁、电机停止并处于自稳模式。填写配置后开启标定；飞控只观察数据，不会解锁、起飞或改变推力。</p><div class="calibration-form-grid"><label class="calibration-field" for="descent-airframe-id"><span>机体标识</span><input id="descent-airframe-id" maxlength="32" autocomplete="off" placeholder="例如 frame-a" oninput="handleDescentCalibrationConfigInput()"><small>用于区分机架、载荷和电池配置。</small></label><label class="calibration-field" for="descent-prop-id"><span>桨叶配置</span><input id="descent-prop-id" maxlength="32" autocomplete="off" placeholder="例如 55mm-2blade" oninput="handleDescentCalibrationConfigInput()"><small>填写尺寸、叶数或型号；配置会绑定到本次记录。</small></label></div><div class="calibration-actions"><button id="descent-calibration-start" class="primary" onclick="startDescentCalibrationCapture()">起飞前开启标定</button><button id="descent-calibration-abort" onclick="abortDescentCalibrationCapture()" disabled>取消标定</button></div></section>
      <section class="calibration-card" aria-labelledby="descent-step-flight"><div class="calibration-step-heading"><span class="calibration-step-number">2</span><strong id="descent-step-flight">自动寻找稳定标记与下降候选</strong></div><p>起飞代理信号成立 3 秒后，飞控才检查连续 5 秒稳定状态。标记完成后蓝灯快闪 5 次、熄灭 1 秒并循环；看到提示后缓慢降低油门，进入下降候选段后灯态恢复正常。</p><div id="descent-calibration-status" class="calibration-inline-status" role="status" aria-live="polite">正在读取标定状态…</div><div id="descent-calibration-measurement" class="calibration-inline-status" role="status">尚无候选结果。</div></section>
      <section class="calibration-card" aria-labelledby="descent-step-save"><div class="calibration-step-heading"><span class="calibration-step-number">3</span><strong id="descent-step-save">落地上锁后确认保存</strong></div><p>系统保留最后一个连续稳定 3 秒的下降候选。请确认稳定标记时确实悬停、候选段确实下降，再保存到迫降推力参数。</p><div id="descent-calibration-recommendation" class="calibration-inline-status" role="status" aria-live="polite">落地上锁且候选通过质量检查后可保存。</div><div class="calibration-actions"><button id="descent-calibration-apply" class="primary" onclick="applyDescentCalibrationCandidate()" disabled>确认保存候选推力</button><button id="descent-calibration-download" onclick="downloadDescentCalibrationCsv()" disabled>下载记录</button><button id="descent-calibration-clear" onclick="clearDescentCalibrationCapture()">清除本次记录</button></div></section>
      <details class="calibration-limit"><summary>能力边界与飞行注意事项</summary><p>蓝灯只表示姿态、推力和杆量形成了稳定候选，不能证明真实悬停；匀速爬升也可能满足条件。此流程不使用高度或下降速度，得到的是飞手确认的经验推力。迫降仍无触地检测，电池、载荷、桨叶、风和地面效应变化后需重新验证。</p></details>
    </div>
  </section>
  <section id="vibration-calibration-page" class="vibration-calibration-page" aria-hidden="true">
    <div class="descent-calibration-shell">
      <div class="diagnostic-top"><h2>四电机低功率扰动检测</h2><div class="diagnostic-actions"><button onclick="closeVibrationCalibrationPage()">返回遥控器</button></div></div>
      <div class="calibration-card"><strong>开始前：固定机体并确保周围安全</strong><p>飞控保持上锁。系统先记录约 200 ms 静止基线，再按 FR、FL、RR、RL 顺序分别以 5% 输出约 500 ms，并采集同步 IMU 数据。每路结束后全部输出归零并等待 1 秒，第四路后也等待 1 秒再报告完成。该短时低功率测试可带桨执行，但机体仍须固定。关闭页面不会中断流程，可用“停止检测”立即上锁并取消后续试转。</p><small>每路约 500 帧，足够执行自动响应判定；请同时人工观察是否起转和有无异响，测试期间勿触碰机体或电机。</small></div>
      <div id="vibration-calibration-status" class="route-status" role="status">正在读取校准状态…</div>
      <div class="calibration-actions"><button id="vibration-calibration-start" class="primary" onclick="startVibrationCalibration()">开始四电机采集</button><button id="vibration-calibration-stop" onclick="stopMotorSelfCheck()" disabled>停止检测</button><button id="vibration-calibration-download" onclick="downloadVibrationCalibrationCsv()" disabled>下载行数表格 CSV</button></div>
      <div class="calibration-card"><strong>逐电机结果</strong><div id="vibration-calibration-results" class="calibration-points">尚无结果。</div><p id="vibration-calibration-analysis" class="route-status">比较四个电机的加速度振动 RMS；偏高只表示优先复核机械安装、紧固和电机，不直接判定损坏。</p></div>
      <div class="calibration-card"><strong>姿态算法评估边界</strong><p>该流程可筛查电机振动是否可能污染 IMU 输入。姿态算法的改进需另用静态、手动遥控飞行日志及可信姿态参考评估；仅凭单电机振动数据无法可靠地自动调节加速度计权重或滤波参数。</p></div>
    </div>
  </section>
  <section id="level-calibration-page" class="vibration-calibration-page" aria-hidden="true">
    <div class="descent-calibration-shell">
      <div class="diagnostic-top"><h2>机身水平校准</h2><div class="diagnostic-actions"><button onclick="closeLevelCalibrationPage()">返回遥控器</button></div></div>
      <div class="calibration-card"><strong>用机身基准面确认水平</strong><p>先上锁并停止全部电机，用水平仪将机身基准面放平、固定且保持静止。此功能只使用 IMU 的加速度计与陀螺仪，未安装磁力计也可正常完成。它采集约 1 秒 IMU 数据，检查重力模长与振动，再建议 IMU 安装横滚/俯仰角；不会修改六面加速度计偏置，也不能把空中悬停姿态当作水平基准。</p></div>
      <div id="level-calibration-live" class="route-status">正在读取估计姿态…</div>
      <div id="level-calibration-status" class="route-status" role="status">尚未采集。</div>
      <div class="calibration-actions"><button id="level-calibration-start" class="primary" onclick="startLevelCalibration()">采集水平基准</button><button id="level-calibration-apply" onclick="applyLevelCalibration()" disabled>确认保存安装角</button><button id="level-calibration-discard" onclick="discardLevelCalibration()">放弃建议</button></div>
      <div class="calibration-card"><small>保存后仍需保持机身水平，等待估计姿态收敛并复核 Roll/Pitch。航向角没有磁力计绝对参考，校准后不要求归零。</small></div>
    </div>
  </section>
  <!-- 版权页脚 -->
  <div class="footer"><a href="/telemetry">实时日志</a> · <a href="https://oshwhub.com/songge8/project_qqqyfdkm" target="_blank">琛光无人机开源项目</a></div>
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
const CONTROL_REQUEST_TIMEOUT_MS = 2500;
const LEASE_REQUEST_TIMEOUT_MS = 3500;
const STATUS_REQUEST_TIMEOUT_MS = 3500;
const stickReadoutElements = {
  throttle:document.getElementById('left-y'),
  yaw:document.getElementById('left-x'),
  roll:document.getElementById('right-x'),
  pitch:document.getElementById('right-y'),
};

const touches = new Map();
let leftStick  = {x:0, y:0, rawX:0, rawY:-100};
let rightStick = {x:0, y:0, rawX:0, rawY:0};
let hoverThrottleRaw = 0; // 后端按悬停推力和 MOT_THR_MIN/MAX 反算；断线时回中为 50%
let hoverThrottleReachable = true;
let hoverThrottleWarningShown = false;
let stickInputActivated = false;
let leftThrottleReturnGeneration = 0;

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
  '/descent-calibration/start', '/descent-calibration/abort', '/descent-calibration/status',
  '/console/disable'
]);
const nativeFetch = window.fetch.bind(window);
window.fetch = (input, options) => {
  let url = null;
  try {
    const raw = typeof input === 'string' ? input : input.url;
    url = new URL(raw, location.href);
  } catch (_) {}
  const fastStopRequest = url && url.hostname === location.hostname && url.port === '82' &&
    ['/kill','/land','/lock'].includes(url.pathname);
  const flightRequest = url && url.origin === location.origin && flightRequestPaths.has(url.pathname);
  if ((!armedStatusKnown || currentArmed) && !flightRequest && !fastStopRequest)
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
try {
  webRCStopToken = localStorage.getItem('cfDroneStopToken') || '';
} catch (_) {}
if (!webRCStopToken) {
  try { webRCStopToken = sessionStorage.getItem('cfDroneStopToken') || ''; } catch (_) {}
}
let webRCLeasePromise = null;
let webRCLeaseBlocked = false;
let joystickRequestPromise = null;
let joystickSendPending = false;
let heartbeatRequestPromise = null;
let currentWifiMode = 'unknown';

function updateWifiModeButton(mode) {
  if (!['ap','sta','offline'].includes(mode)) return;
  currentWifiMode = mode;
  const button = document.getElementById('wifi-settings-button');
  if (!button) return;
  button.textContent = mode === 'ap' ? '连接 Wi-Fi' :
    mode === 'sta' ? '切换 Drone_WiFi' : 'Wi-Fi 配置';
}

async function openWifiSettings() {
  if (currentArmed) {
    showToast('请先上锁，再切换 Wi-Fi 模式');
    return;
  }
  if (currentWifiMode !== 'sta') {
    location.href = '/wifi';
    return;
  }
  if (!window.confirm('确认切换到 Drone_WiFi 热点模式？当前 Wi-Fi 连接会断开；重启后请连接 Drone_WiFi，并访问 192.168.4.1。已保存的 Wi-Fi 网络会保留。')) return;
  const button = document.getElementById('wifi-settings-button');
  button.disabled = true;
  try {
    const response = await fetch('/wifi/mode', {
      method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:new URLSearchParams({mode:'ap'})
    });
    const reply = await response.json().catch(()=>({}));
    if (!response.ok) throw new Error(reply.message || '切换失败');
    showToast(reply.message || '正在切换到 Drone_WiFi…');
    button.textContent = '正在切换…';
  } catch (error) {
    button.disabled = false;
    showToast(error.message || 'Wi-Fi 模式切换失败');
  }
}

let buttonStates     = new Array(16).fill(false);
let lastButtonStates = new Array(16).fill(false);

let consolePollingTimer = null;
let consoleLastTotal    = 0;   // 增量拉取游标：已展示到第 N 行
let consoleFetchInFlight = false; // 防并发：上次 fetch 未返回时跳过本次
let consolePanelOpen = false;
let consoleCommandPending = false;
let consoleReadFailureCount = 0;
function setArmedState(armed) {
  const wasArmed = currentArmed;
  currentArmed = !!armed;
  armedStatusKnown = true;
  if (currentArmed && !wasArmed && consolePanelOpen) toggleConsole();
  if (currentArmed && !wasArmed && pidPanelOpen) closePidPanel();
  if (routeRecording && routeRecordStartedArmed && !currentArmed && wasArmed) stopRouteRecording('飞控已上锁，录制已安全停止。', true);
  if (routeRecording && !routeRecordStartedArmed && currentArmed && !wasArmed) stopRouteRecording('飞控已解锁，本地录制已停止。', true);
  updateRouteControls();
}
let selfCheckOpen = false;
let selfCheckRequestSequence = 0;
let selfCheckHasData = false;
let selfCheckRequestInFlight = false;
let routeTimer = null;
let descentCalibrationTimer = null;
let vibrationCalibrationTimer = null;
let descentCalibrationLatestStatus = null;
let currentDeviceId='',currentFirmwareBuild='';
const DESCENT_CALIBRATION_CONFIG_KEY = 'cfDroneDescentCalibrationConfigV1';
let flightRouteRunning = false;
let routeStarting = false;
let routeHold = false;
let routePending='',routeUploadedText=null,routeUploadedRevision=0,routeServerState='empty',routeStatusBusy=false;
let routeRecording=false,routeRecordStartedArmed=false,routeRecordTimer=null,routeRecordStartMs=0,routeRecordSegmentStartMs=0,routeRecordLast=null,routeRecordSegments=[];
let routeRecordTrimmedMs=0;
let routeNavigationSample={valid:false,altitude:0,heading:0,flowX:0,flowY:0,receivedAt:0};
const ROUTE_RECORD_SAMPLE_MS=100;
const ROUTE_RECORD_IDLE_THROTTLE_PCT=6; // 与飞控 throttleDeadzone 保持一致
const ROUTE_RECORD_HEADER_V1='# WEB_RC_RECORDED_V1';
const ROUTE_RECORD_HEADER_V2='# WEB_RC_RECORDED_V2';
const ROUTE_RECORD_MAX_SEGMENTS=128;
const ROUTE_RECORD_MAX_BYTES=4096;
const ROUTE_RECORD_MAX_DURATION_MS=1800000;
const CONSOLE_BASE_POLL_MS = 500;
const CONSOLE_CATCHUP_POLL_MS = 80;
const CONSOLE_PAGE_LIMIT = 20;
const CONSOLE_REQUEST_TIMEOUT_MS = 8000;

/*======================== 按钮配置（2×3 六宫格）========================*/
const buttonConfigs = [
  {icon:"🔓",label:"解锁",   color:"#00ff88",desc:"解锁电机"},
  {icon:"🔒",label:"上锁",   color:"#ff3333",desc:"锁定电机"},
  {icon:"🛑",label:"急停",   color:"#ff0055",desc:"紧急停止"},
  {icon:"🛬",label:"迫降",   color:"#ff8c00",desc:"保持水平并进入受控下降；气压仅限制过快下降"},
  {icon:"🔄",label:"切换模式", color:"#00cfff",desc:"自稳、定高与特技循环切换"},
  {icon:"⏺",label:"录制序列",color:"#ff5555",desc:"录制当前摇杆输出序列；再次点击停止"}
];

/*======================== 初始化 ========================*/
function init() {
  // Opening a page immediately makes it the active controller. The device
  // replaces the prior lease token, so stale tabs can no longer send input.
  acquireControlLease();
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
  const points=[];const lines=text.split(/\r?\n/);let schema=0,expectedColumns=0,metadataSeen=false,contentSeen=false;
  for(let i=0;i<lines.length;i++){
    const line=lines[i].trim();if(!line)continue;
    if(line.startsWith('#')){
      const known=line==='# WEB_RC_RECORDED_V1'||line==='# WEB_RC_RECORDED_V2'||line==='# CF_ROUTE_META schema=2 source=authored policy=slew';
      const metadata=line.startsWith('# WEB_RC_RECORDED_')||line.startsWith('# CF_ROUTE_META');
      if(known){
        if(i!==0||metadataSeen||contentSeen)throw new Error(`第 ${i+1} 行元数据必须唯一且位于正文第一行`);
        metadataSeen=true;schema=line==='# WEB_RC_RECORDED_V1'?1:2;expectedColumns=schema===1?5:7;
      }else if(metadata)throw new Error(`第 ${i+1} 行包含不支持的航线元数据`);
      continue;
    }
    contentSeen=true;
    const fields=line.split(/[\s,]+/);
    if(![5,7].includes(fields.length)||fields.some(v=>v===''||!Number.isFinite(Number(v))))throw new Error(`第 ${i+1} 行需包含 5 个或 7 个有限数字`);
    if(!metadataSeen){schema=1;expectedColumns=5;}
    if(fields.length!==expectedColumns)throw new Error(`第 ${i+1} 行与航线 schema 不匹配`);
    const [duration,throttle,roll,pitch,yaw,altitude,heading]=fields.map(Number);
    if(duration<0.1||duration>600||throttle<0||throttle>100||Math.abs(roll)>100||Math.abs(pitch)>100||Math.abs(yaw)>100)throw new Error(`第 ${i+1} 行参数超出范围`);
	if(fields.length===7&&(altitude<-20||altitude>20||heading<-360||heading>360))throw new Error(`第 ${i+1} 行高度或航向超出范围`);
    points.push({duration,throttle,roll,pitch,yaw,altitude,heading});
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
	const navigationFresh=routeNavigationSample.valid&&performance.now()-routeNavigationSample.receivedAt<=350;
  return {
    throttle:clampRouteValue(Math.round((currentValues.throttle+100)/2),0,100),
    roll:clampRouteValue(Math.round(currentValues.roll),-100,100),
    pitch:clampRouteValue(Math.round(currentValues.pitch),-100,100),
    yaw:clampRouteValue(Math.round(currentValues.yaw),-100,100),
	altitude:navigationFresh?routeNavigationSample.altitude:null,
	heading:navigationFresh?routeNavigationSample.heading:null
  };
}
function angleDifferenceDegrees(a,b){let d=a-b;while(d>180)d-=360;while(d<-180)d+=360;return d;}
function sameRouteRecordValue(a,b){return a&&b&&a.throttle===b.throttle&&a.roll===b.roll&&a.pitch===b.pitch&&a.yaw===b.yaw&&
	((a.altitude===null&&b.altitude===null)||(a.altitude!==null&&b.altitude!==null&&Math.abs(a.altitude-b.altitude)<0.03))&&
	((a.heading===null&&b.heading===null)||(a.heading!==null&&b.heading!==null&&Math.abs(angleDifferenceDegrees(a.heading,b.heading))<1.0));}
function formatRouteRecordLine(segment){
	const base=(segment.durationMs/1000).toFixed(1)+' '+segment.throttle+' '+segment.roll+' '+segment.pitch+' '+segment.yaw;
	return segment.altitude===null||segment.heading===null?base:base+' '+segment.altitude.toFixed(3)+' '+segment.heading.toFixed(2);
}
function routeRecordText(segments){
  if(!segments.length)return '';
  const navigation=segments.every(segment=>segment.altitude!==null&&segment.heading!==null);
  return (navigation?ROUTE_RECORD_HEADER_V2:ROUTE_RECORD_HEADER_V1)+'\n'+segments.map(formatRouteRecordLine).join('\n');
}
function appendRouteRecordSegment(durationMs,value){
  const roundedMs=Math.max(ROUTE_RECORD_SAMPLE_MS,Math.round(durationMs/ROUTE_RECORD_SAMPLE_MS)*ROUTE_RECORD_SAMPLE_MS);
  if(!routeRecordSegments.length&&value.throttle<ROUTE_RECORD_IDLE_THROTTLE_PCT){
    routeRecordTrimmedMs+=roundedMs;
    return '';
  }
  const segment={durationMs:roundedMs,throttle:value.throttle,roll:value.roll,pitch:value.pitch,yaw:value.yaw,altitude:value.altitude,heading:value.heading};
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
  const initial=routeRecordSnapshot();
  if(routeRecordStartedArmed&&(initial.altitude===null||initial.heading===null)){
    routeRecordStatus('板端高度或航向样本尚未就绪，不能开始 V2 录制。');return;
  }
  routeRecording=true;
  if(routeTimer){clearInterval(routeTimer);routeTimer=null;}
  routeRecordSegments=[];
  routeRecordTrimmedMs=0;
  routeRecordLast=initial;
  routeRecordStartMs=performance.now();
  routeRecordSegmentStartMs=routeRecordStartMs;
  routeRecordTimer=setInterval(sampleRouteRecording,ROUTE_RECORD_SAMPLE_MS);
  routeRecordStatus((routeRecordStartedArmed?'正在录制手动操作与板端高度/航向：':'正在本地录制：')+'0.0 秒，0 段。');
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
  if(routeRecordStartedArmed&&(value.altitude===null||value.heading===null)){
    stopRouteRecording('板端高度或航向样本过期，录制已停止并保留完整 V2 片段。',true,true);return;
  }
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
  openDescentCalibrationPage();
}
function openDescentCalibrationPage(){
  const page=document.getElementById('descent-calibration-page');page.style.display='block';page.setAttribute('aria-hidden','false');
  loadDescentCalibrationConfig();
  refreshDescentCalibrationStatus();
}
function closeDescentCalibrationPage(){
  const page=document.getElementById('descent-calibration-page');page.style.display='none';page.setAttribute('aria-hidden','true');
}
function setDescentCalibrationActive(active){
  if(active&&!descentCalibrationTimer)descentCalibrationTimer=setInterval(refreshDescentCalibrationStatus,1000);
  if(!active&&descentCalibrationTimer){clearInterval(descentCalibrationTimer);descentCalibrationTimer=null;}
  const button=document.getElementById('descent-calibration-button');
  button.textContent=active?'标定状态':'迫降标定';button.classList.toggle('has-fault',active);
  const banner=document.getElementById('descent-recording-banner');
  banner.classList.toggle('active',active);banner.setAttribute('aria-hidden',active?'false':'true');
  updateDescentCalibrationControls();
}
function updateDescentCalibrationControls(){
  const status=descentCalibrationLatestStatus;
  const active=!!(status&&['waiting_takeoff','takeoff_delay','hover_candidate','hover_ready','descent_tracking'].includes(status.state));
  document.getElementById('descent-calibration-start').disabled=active||!connectionOk||currentArmed||currentFlightMode!==2;
  document.getElementById('descent-calibration-abort').disabled=!active;
  document.getElementById('descent-airframe-id').disabled=active;
  document.getElementById('descent-prop-id').disabled=active;
  document.getElementById('descent-calibration-download').disabled=currentArmed||!status||!['complete','aborted'].includes(status.state)||Number(status.samples)===0;
  document.getElementById('descent-calibration-clear').disabled=currentArmed||active;
  document.getElementById('descent-calibration-apply').disabled=!connectionOk||currentArmed||!status||status.state!=='complete'||status.candidate_ready!==true||status.save_pending===true||status.binding_saved===true;
}
async function refreshDescentCalibrationStatus(){
  try{
    const response=await fetch('/descent-calibration/status',{cache:'no-store'});if(!response.ok)throw new Error('状态读取失败');
    const data=await response.json();descentCalibrationLatestStatus=data;
    if(typeof data.armed==='boolean')setArmedState(data.armed);
    if(Number.isInteger(data.mode)){currentFlightMode=data.mode;document.getElementById('flight-mode').textContent=['直控','特技','自稳','定高','自动'][data.mode]||'未知';}
    const active=['waiting_takeoff','takeoff_delay','hover_candidate','hover_ready','descent_tracking'].includes(data.state);
    setDescentCalibrationActive(active);
    const status=document.getElementById('descent-calibration-status');
    const progress=Math.min(100,Math.max(0,Number(data.stable_progress_ms)||0)/(data.state==='hover_candidate'?5000:3000)*100);
    const stateText={empty:'尚未开启标定',waiting_takeoff:'标定已开启，等待起飞代理信号',takeoff_delay:`已检测到起飞代理信号，等待 3 秒（${Math.min(3,Number(data.takeoff_elapsed_ms)/1000).toFixed(1)}/3.0 秒）`,hover_candidate:`正在寻找连续 5 秒稳定标记（${progress.toFixed(0)}%）`,hover_ready:'悬停候选／稳定标记完成，可以缓慢下降；蓝灯正在循环提示',descent_tracking:`已检测到降低油门，正在滚动检查稳定下降候选（${progress.toFixed(0)}%）`,complete:'已上锁，候选记录已冻结',aborted:'本次标定已中止'};
    status.textContent=(stateText[data.state]||'标定状态未知')+(data.reason?`；状态：${data.reason}`:'');
    const bannerText=document.getElementById('descent-recording-banner-text');
    if(bannerText)bannerText.textContent=data.state==='hover_ready'?'蓝灯提示已开始：悬停候选完成，可以缓慢下降':data.state==='descent_tracking'?'正在采集稳定下降候选；落地后请明确上锁':stateText[data.state]||'迫降标定进行中';
    document.getElementById('descent-calibration-measurement').textContent=data.candidate_ready
      ? `稳定标记平均推力 ${Number(data.hover_mean_thrust).toFixed(3)}；下降候选 ${Number(data.candidate_thrust).toFixed(3)}；差值 ${Number(data.thrust_delta).toFixed(3)}；电池 ${Number(data.candidate_battery_v).toFixed(2)} V；最大倾角 ${Number(data.max_tilt_deg).toFixed(1)}°。`
      : `尚未形成完整的 3 秒稳定下降候选${data.reason&&active?'；'+data.reason:''}。`;
    document.getElementById('descent-calibration-recommendation').textContent=data.binding_saved
      ? `候选已保存并绑定到本次记录。当前 SF_DESCEND_THRUST=${Number(data.candidate_thrust).toFixed(3)}。`
      : data.state==='complete'&&data.candidate_ready?'请确认实际飞行中稳定标记对应悬停、候选段对应下降，再保存。':'落地上锁且候选通过质量检查后可保存。';
    updateDescentCalibrationControls();
  }catch(error){document.getElementById('descent-calibration-status').textContent=error.message||'无法读取标定状态';}
}
async function startDescentCalibrationCapture(){
  if(!connectionOk||currentArmed||currentFlightMode!==2){showToast('请连接飞控并在自稳模式、上锁状态下开启标定');return;}
  try{
    const config=readDescentCalibrationConfig();
    const response=await controlFetch('/descent-calibration/start',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(config)});const result=await response.json();
    if(!response.ok||!result.ok)throw new Error(result.error||'无法开始记录');
    await refreshDescentCalibrationStatus();closeDescentCalibrationPage();showToast('标定已开启；起飞后将自动寻找稳定标记');
  }catch(error){document.getElementById('descent-calibration-status').textContent=error.message;}
}
async function abortDescentCalibrationCapture(){
  try{
    const response=await controlFetch('/descent-calibration/abort',{method:'POST'});const result=await response.json();
    if(!response.ok||!result.ok)throw new Error(result.error||'当前没有进行中的标定');
    await refreshDescentCalibrationStatus();openDescentCalibrationPage();showToast('本次标定已取消');
  }catch(error){showToast(error.message||'取消标定失败');}
}
function loadDescentCalibrationConfig(){
  try{const value=JSON.parse(localStorage.getItem(DESCENT_CALIBRATION_CONFIG_KEY)||'{}');document.getElementById('descent-airframe-id').value=value.airframe||'';document.getElementById('descent-prop-id').value=value.prop||'';}catch(_){}
}
function handleDescentCalibrationConfigInput(){
  const airframe=document.getElementById('descent-airframe-id').value;
  const prop=document.getElementById('descent-prop-id').value;
  try{localStorage.setItem(DESCENT_CALIBRATION_CONFIG_KEY,JSON.stringify({airframe,prop}));}catch(_){}
}
function readDescentCalibrationConfig(){
  const airframe=document.getElementById('descent-airframe-id').value.trim();
  const prop=document.getElementById('descent-prop-id').value.trim();
  if(!airframe||!prop)throw new Error('请填写机体标识和桨叶配置');
  if(airframe.length>32||prop.length>32||/["\\<>\x00-\x1f]/.test(airframe)||/["\\<>\x00-\x1f]/.test(prop))throw new Error('机体和桨叶标识不能超过 32 个字符，也不能包含引号、反斜杠、控制字符或尖括号');
  try{localStorage.setItem(DESCENT_CALIBRATION_CONFIG_KEY,JSON.stringify({airframe,prop}));}catch(_){throw new Error('浏览器无法保存机体配置');}
  return {airframe,prop};
}
async function refreshDescentCalibrationSaveState(){
  updateDescentCalibrationControls();
}
async function applyDescentCalibrationCandidate(){
  const status=descentCalibrationLatestStatus;
  if(!status||status.state!=='complete'||!status.candidate_ready||currentArmed||!connectionOk){showToast('保存前请确认候选有效、飞控已连接并上锁');return;}
  try{
    const config=readDescentCalibrationConfig();
    if(config.airframe!==status.airframe||config.prop!==status.prop||!status.session)throw new Error('机体或桨叶配置与本次记录不一致');
    const binding={session:status.session,device_id:status.device_id,firmware_build:status.firmware_build,airframe:config.airframe,prop:config.prop};
    const response=await controlFetch('/descent-calibration/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(binding)});
    const result=await response.json();if(!response.ok||!result.ok)throw new Error(result.error||'参数保存未排队');
    document.getElementById('descent-calibration-recommendation').textContent='已提交保存；等待飞控写入并确认…';
    for(let i=0;i<12;i++){
      await new Promise(resolve=>setTimeout(resolve,1000));
      const saved=await fetch('/descent-calibration/save-status?'+new URLSearchParams(binding),{cache:'no-store'}).then(r=>r.json());
      if(saved.saved){document.getElementById('descent-calibration-recommendation').textContent=`参数已写入并读回确认：SF_DESCEND_THRUST=${Number(saved.value).toFixed(3)}。`;await refreshDescentCalibrationStatus();return;}
    }
    throw new Error('参数仍未确认写入；请检查 NVS 状态，勿重复飞行验证');
  }catch(error){document.getElementById('descent-calibration-recommendation').textContent=error.message||'参数保存失败';}
}
async function clearDescentCalibrationCapture(){
  if(currentArmed){showToast('请先上锁再清除记录');return;}
  try{const response=await controlFetch('/descent-calibration/clear',{method:'POST'});const result=await response.json();if(!response.ok||!result.ok)throw new Error(result.error||'清除失败');await refreshDescentCalibrationStatus();}
  catch(error){showToast(error.message||'清除标定记录失败');}
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
    if(data.state==='rejected'){
      const reasonNames={gravity_invalid:'重力数据无效',gravity_norm_out_of_range:'加速度模长偏离 1g，请先完成六面加速度计校准',accel_noise_too_high:'加速度波动过大，请固定机身后重试',gyro_noise_too_high:'陀螺仪波动过大，请固定机身后重试',gyro_rate_too_high:'检测到机身仍在转动，请完全静止后重试',not_stationary_or_gravity_invalid:'机体未保持静止，或重力模长不合理',mounting_offset_or_geometry_invalid:'安装偏角超过 15°，或校正几何检查失败',capture_timeout:'采集超时',capture_start_failed:'采集资源不可用',preflight_failed:'飞控状态不允许校准'};
      detail+=`；${reasonNames[data.reason]||data.reason}；加速度波动 ${Number(data.acc_sd||0).toFixed(4)} m/s²，陀螺波动 ${Number(data.gyro_sd||0).toFixed(5)} rad/s，平均角速度 ${Number(data.gyro_mean_norm||0).toFixed(5)} rad/s`;
    }
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
  const active=data.state==='boot_wait'||data.state==='queued'||data.state==='baseline'||data.state==='running'||data.state==='settling';
  button.disabled=active||currentArmed;
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
    const phase=data.state==='boot_wait'?'上电自动检测等待期':data.state==='baseline'?'采集静止基线':data.state==='settling'?'四路完成，等待最后一路完全停转':`已完成 ${data.step}/4 路`;
    card.innerHTML=`<strong>电机检测进行中</strong><small>${phase}；结束前保持机体固定并勿解锁。</small>`;
  }else{
    card.className='diagnostic-summary offline';
    card.innerHTML='<strong>电机响应检测尚未执行</strong><small>固定机体并确保周围安全后，点击下方按钮自动测试四路。</small>';
  }
}
async function refreshVibrationCalibrationStatus(){
  try{
    const response=await fetch('/vibration-calibration/status',{cache:'no-store'});if(!response.ok)throw new Error('状态读取失败');
    const data=await response.json();
    const names=['FR','FL','RR','RL'];
    const stateText=({empty:'尚无记录',boot_wait:'上电自动检测等待中',queued:'已排队，准备启动',baseline:'正在采集静止基线',running:'正在采集',settling:'全部输出已归零，等待电机停转',complete:'四路采集完成',aborted:'采集已中止'})[data.state]||'状态未知';
    const step=Math.min(Number(data.step)||0,4);
    document.getElementById('vibration-calibration-status').textContent=`${stateText}；已完成 ${step}/4 路${data.state==='running'?`，当前 ${names[Math.min(step,3)]} 电机测试中`:''}${data.state==='aborted'?'；原因：'+data.reason:''}`;
    document.getElementById('vibration-calibration-start').disabled=data.state==='boot_wait'||data.state==='queued'||data.state==='baseline'||data.state==='running'||data.state==='settling'||currentArmed;
    document.getElementById('vibration-calibration-stop').disabled=!(data.state==='boot_wait'||data.state==='queued'||data.state==='baseline'||data.state==='running'||data.state==='settling');
    document.getElementById('vibration-calibration-download').disabled=data.state!=='complete'||currentArmed;
    renderVibrationCalibrationResults(data.motors);
    renderMotorSelfCheck(data);
    if(data.state==='boot_wait'||data.state==='queued'||data.state==='baseline'||data.state==='running'||data.state==='settling')setVibrationCalibrationPolling(true);else setVibrationCalibrationPolling(false);
  }catch(error){
    document.getElementById('vibration-calibration-status').textContent=error.message||'无法读取校准状态';
    if(selfCheckOpen)document.getElementById('motor-self-check').innerHTML='<strong>电机检测状态暂不可读</strong><small>请检查连接后刷新。</small>';
  }
}
async function startVibrationCalibration(){
  if(currentArmed){showToast('请先上锁并停止电机');return;}
  const status=document.getElementById('vibration-calibration-status');
  status.textContent='正在提交四电机 5% / 500 ms 测试请求…';
  try{
    const response=await controlFetch('/vibration-calibration/start',{method:'POST'});
    const result=await response.json();if(!response.ok||!result.ok)throw new Error(result.error||'无法开始采集');
    status.textContent='测试请求已接受，等待飞控主循环开始…';
    setVibrationCalibrationPolling(true);refreshVibrationCalibrationStatus();
  }catch(error){
    status.textContent=error.message||'启动失败';
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
    if(data.mode!==undefined){if(routeRecording&&routeRecordStartedArmed&&data.mode!==2)stopRouteRecording('飞行模式已切换，录制已安全停止。',true);currentFlightMode=data.mode;document.getElementById('flight-mode').textContent=['直控','特技','自稳','定高','自动'][data.mode]||'未知';}
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
  if (stickInputActivated && (hasSignificantChange(currentValues) ||
	  (now - lastForceSentTime >= (routeRecording&&routeRecordStartedArmed?ROUTE_RECORD_SAMPLE_MS:FORCE_SEND_INTERVAL)))) {
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
  if (!stickInputActivated) return Promise.resolve(false);
  joystickSendPending = true;
  if (joystickRequestPromise) return joystickRequestPromise;
  joystickRequestPromise = (async () => {
    let result = false;
    while (joystickSendPending) {
      joystickSendPending = false;
      const values = {...currentValues};
      lastSentValues = values;
      packetStats.sent++;
      result = await sendToESP('/web_rc', {t:1, th:Math.round(values.throttle), r:Math.round(values.roll),
        p:Math.round(values.pitch), y:Math.round(values.yaw), ts:performance.now()});
    }
    return result;
  })().finally(() => { joystickRequestPromise = null; });
  return joystickRequestPromise;
}

function sendButtonData(buttonIndex, state) {
  if (state && (buttonIndex === 1 || buttonIndex === 2 || buttonIndex === 3) && !webRCStopToken) {
    showToast('本页面没有停机凭证；请使用当前遥控页面或实体急停。');
    return;
  }
  if (state && webRCStopToken && (buttonIndex === 1 || buttonIndex === 2 || buttonIndex === 3)) {
    const action = buttonIndex === 2 ? 'kill' : buttonIndex === 3 ? 'land' : 'lock';
    nativeFetch(`${location.protocol}//${location.hostname}:82/${action}?s=${encodeURIComponent(webRCStopToken)}`,
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
  stickInputActivated=true;
  if(side==='left')leftThrottleReturnGeneration++;
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
  const joystick = document.getElementById(`joystick-${side}`);
  joystick.classList.remove('active');
  if(side==='left'){
    returnLeftStickToHover();
  }else{
    const knob=document.getElementById('knob-right');
    knob.style.transition='transform 0.2s ease-out';
    knob.style.transform='translate(-50%,-50%)';
    setTimeout(()=>{knob.style.transition='';},200);
    rightStick={x:0,y:0,rawX:0,rawY:0};
    processJoystickInput();
    sendJoystickData();
  }
}

function releaseControlsForPageExit() {
  if (!stickInputActivated) return;
  touches.clear();
  leftThrottleReturnGeneration++;
  rightStick={x:0,y:0,rawX:0,rawY:0};
  leftStick.rawX=0;
  leftStick.rawY=hoverThrottleRaw;
  processJoystickInput();
  sendJoystickData();
  stickInputActivated=false;
}

function returnLeftStickToHover(){
  const generation=++leftThrottleReturnGeneration;
  const startRawY=leftStick.rawY;
  const started=performance.now();
  const duration=300;
  leftStick.rawX=0;
  if(!hoverThrottleReachable&&!hoverThrottleWarningShown){
    hoverThrottleWarningShown=true;
    showToast('油门缩放不足，悬停推力不可达；已回到允许的最高油门');
  }
  function step(now){
    if(generation!==leftThrottleReturnGeneration)return;
    const progress=Math.min(1,(now-started)/duration);
    const eased=1-Math.pow(1-progress,3);
    leftStick.rawY=startRawY+(hoverThrottleRaw-startRawY)*eased;
    leftStick.y=leftStick.rawY;
    const joystick=document.getElementById('joystick-left');
    const radius=joystick.getBoundingClientRect().width/2-10;
    const dy=-leftStick.rawY/100*radius;
    document.getElementById('knob-left').style.transform=`translate(calc(-50% + 0px), calc(-50% + ${dy}px))`;
    processJoystickInput();
    if(progress<1)requestAnimationFrame(step);
    else sendJoystickData();
  }
  requestAnimationFrame(step);
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
function controlUrl(url, leaseToken=webRCLeaseToken) {
  if (!leaseToken) return url;
  return url + (url.includes('?') ? '&' : '?') + 'lease=' + encodeURIComponent(leaseToken);
}

async function fetchWithTimeout(input, options={}, timeoutMs=CONTROL_REQUEST_TIMEOUT_MS) {
  const controller = new AbortController();
  const sourceSignal = options.signal;
  let sourceAbort = null;
  if (sourceSignal) {
    sourceAbort = () => controller.abort();
    if (sourceSignal.aborted) controller.abort();
    else sourceSignal.addEventListener('abort', sourceAbort, {once:true});
  }
  const timeout = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetch(input, {...options, signal:controller.signal});
    // Keep the same deadline active until the complete response body arrives.
    // Callers may parse the original response after this buffered clone finishes.
    await response.clone().arrayBuffer();
    return response;
  } finally {
    clearTimeout(timeout);
    if (sourceSignal && sourceAbort) sourceSignal.removeEventListener('abort', sourceAbort);
  }
}

function recoverExpiredLease(expectedToken='') {
  if (expectedToken && webRCLeaseToken !== expectedToken) return;
  webRCLeaseToken = '';
  webRCLeaseBlocked = false;
}

function handleLeaseConflict(message, expectedToken='') {
  if (expectedToken && webRCLeaseToken !== expectedToken) return;
  const shouldNotify = !webRCLeaseBlocked;
  webRCLeaseToken = '';
  webRCLeaseBlocked = true;
  if(routeRecording&&routeRecordStartedArmed)stopRouteRecording('页面失去遥控控制权，录制已安全停止。',true);
  updateConnectionStatus(false);
  if (shouldNotify) showToast(message || '此页面已被更新打开的遥控页面立即取代');
}

async function acquireControlLease() {
  if (webRCLeaseToken) return true;
  if (webRCLeaseBlocked) return false;
  if (webRCLeasePromise) return webRCLeasePromise;
  const leaseUrl = '/web_rc/lease' + (webRCStopToken ? '?stop=' + encodeURIComponent(webRCStopToken) : '');
  webRCLeasePromise = fetchWithTimeout(leaseUrl, {method:'POST', cache:'no-store'}, LEASE_REQUEST_TIMEOUT_MS)
    .then(async response => {
      const data = await response.json().catch(() => ({}));
      if (!response.ok || !data.lease) {
        if (data.error === 'web_rc_flight_takeover_forbidden')
          handleLeaseConflict('飞行中禁止其他页面抢占；请返回原控制浏览器');
        else if (data.error === 'web_rc_lease_in_use')
          handleLeaseConflict('遥控控制权已被其他页面占用');
        else if (++consecutiveFails >= 3) updateConnectionStatus(false);
        return false;
      }
      webRCLeaseToken = data.lease;
      webRCStopToken = data.stop || '';
      try { localStorage.setItem('cfDroneStopToken', webRCStopToken); } catch (_) {}
      try { sessionStorage.setItem('cfDroneStopToken', webRCStopToken); } catch (_) {}
      webRCLeaseBlocked = false;
      consecutiveFails = 0;
      updateConnectionStatus(true);
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
  for (let attempt = 0; attempt < 2; attempt++) {
    const ok = await acquireControlLease();
    if (!ok) throw new Error('web_rc_lease_required');
    const requestLeaseToken = webRCLeaseToken;
    const response = await fetchWithTimeout(controlUrl(url, requestLeaseToken), options);
    if (response.status === 409) {
      let data = {};
      try { data = await response.clone().json(); } catch (_) {}
      if ((data.error === 'web_rc_lease_required' || data.error === 'web_rc_lease_expired') && attempt === 0) {
        recoverExpiredLease(requestLeaseToken);
        continue;
      }
      if (data.error === 'web_rc_lease_in_use')
        handleLeaseConflict('遥控控制权已被其他页面占用', requestLeaseToken);
    }
    return response;
  }
  throw new Error('web_rc_lease_required');
}

function isEmergencyButtonData(data) {
  return data && data.t === 2 && (data.b === 1 || data.b === 2 || data.b === 3);
}

function sendToESP(url, data, leaseRetry=false) {
  const t0 = performance.now();
  const emergencyOverride = isEmergencyButtonData(data);
  let requestLeaseToken = '';
  return (emergencyOverride ? Promise.resolve(true) : acquireControlLease()).then(ok => {
    if (!ok) return null;
    if (emergencyOverride) data.stop = webRCStopToken;
    else {
      requestLeaseToken = webRCLeaseToken;
      data.lease = requestLeaseToken;
    }
    return fetchWithTimeout(emergencyOverride ? url : controlUrl(url, requestLeaseToken),
      {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(data)});
  })
    .then(r => {
      if (!r) return null;
      if (r.status === 409) {
        return r.json().catch(() => ({})).then(data => {
          if (data.error === 'web_rc_lease_required' || data.error === 'web_rc_lease_expired') {
            recoverExpiredLease(requestLeaseToken);
            throw new Error('lease_retry');
          }
          if (data.error === 'web_rc_lease_in_use')
            handleLeaseConflict('遥控控制权已被其他页面占用', requestLeaseToken);
          throw new Error('lease');
        });
      }
      if (!r.ok) throw new Error();
      updateLatency(performance.now() - t0);
      return r.json();
    })
    .then(resp => {
      if (!resp) return;
	  if(resp.rt===1&&resp.nav===true&&Number.isFinite(Number(resp.alt))&&Number.isFinite(Number(resp.hdg))){
		routeNavigationSample={valid:true,altitude:Number(resp.alt),heading:Number(resp.hdg),flowX:Number(resp.fx)||0,flowY:Number(resp.fy)||0,receivedAt:performance.now()};
	  }
      consecutiveFails = 0;
      updateConnectionStatus(true);
      const names = ['直控','特技','自稳','定高','自动'];

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
      return true;
    })
    .catch(error => {
      if (error.message === 'lease_retry' && !leaseRetry) return sendToESP(url, data, true);
      if (++consecutiveFails >= 3) updateConnectionStatus(false);
      return false;
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
  if (selfCheckRequestInFlight) return;
  selfCheckRequestInFlight = true;
  const requestId = ++selfCheckRequestSequence;
  const summary = document.getElementById('diagnostic-summary');
  if (showLoading && !selfCheckHasData) {
    summary.className = 'diagnostic-summary offline';
    summary.innerHTML = '<strong>正在读取诊断状态…</strong><small>数据来自飞控当前运行状态。</small>';
  }
  fetchWithTimeout('/web_rc/status', {cache:'no-store'}, STATUS_REQUEST_TIMEOUT_MS).then(r => {
    if (!r.ok) throw new Error('status unavailable');
    return r.json();
  }).then(data => {
    if (requestId !== selfCheckRequestSequence) return;
    if (typeof data.faults !== 'number') throw new Error('diagnostics unsupported');
    const hoverPercent=Number(data.hover_throttle_pct);
    if(Number.isFinite(hoverPercent)){
      const bounded=Math.max(0,Math.min(100,hoverPercent));
      hoverThrottleRaw=bounded*2-100;
      hoverThrottleReachable=data.hover_throttle_reachable!==false;
      if(hoverThrottleReachable)hoverThrottleWarningShown=false;
      document.getElementById('hover-throttle-label').textContent=Math.round(bounded);
    }
    if (typeof data.armed === 'boolean') setArmedState(data.armed);
    if (data.device_id) currentDeviceId=String(data.device_id);
    if (data.firmware_build) currentFirmwareBuild=String(data.firmware_build);
    if (typeof data.wifi_mode === 'string') updateWifiModeButton(data.wifi_mode);
    consecutiveFails = 0;
    updateConnectionStatus(true);
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
  }).finally(() => { selfCheckRequestInFlight = false; });
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
  const calibrationReadiness = document.getElementById('calibration-readiness');
  const calibrationChecks = [
    {ok:data.gyro_bias_ready===true,name:'陀螺仪静止校准',action:'将机身保持静止约 3 秒'},
    {ok:data.accel_calibration_stored===true,name:'六面加速度计校准',action:'在调试页面运行六面加速度计校准'},
    {ok:data.level_calibration_stored===true,name:'机身水平校准',action:'在调试页面采集并保存水平基准'}
  ];
  const calibrationPending=calibrationChecks.filter(item=>!item.ok);
  calibrationReadiness.className='diagnostic-summary '+(calibrationPending.length?'fault':'ok');
  calibrationReadiness.innerHTML=calibrationPending.length
    ? `<strong>起飞前标定未完成（${calibrationPending.length} 项）</strong><small>${calibrationChecks.map(item=>`${item.ok?'✓':'✗'} ${item.name}${item.ok?'':'：'+item.action}`).join('<br>')}</small>`
    : `<strong>起飞前标定已完成</strong><small>${calibrationChecks.map(item=>`✓ ${item.name}`).join('<br>')}</small>`;
	const compassStatus=document.getElementById('compass-status');
	const compassAge=Number(data.compass_age_ms), magneticHeading=Number(data.magnetic_heading_deg);
	const navigationHeading=Number(data.navigation_heading_deg), magneticInnovation=Number(data.magnetic_innovation_deg);
	const compassReasons=Number(data.compass_reject_reasons)||0;
	compassStatus.className='diagnostic-summary '+(data.compass_trusted===true?'ok':data.compass_detected===true?'offline':'fault');
	compassStatus.innerHTML=data.compass_trusted===true
	  ? `<strong>磁航向可信</strong><small>磁航向 ${magneticHeading.toFixed(1)}°，融合航向 ${navigationHeading.toFixed(1)}°，创新 ${magneticInnovation.toFixed(1)}°，样本年龄 ${compassAge} ms。</small>`
	  : data.compass_detected===true
	  ? `<strong>磁力计已检测但尚不可信</strong><small>ready=${data.compass_ready===true?'是':'否'}，calibrated=${data.compass_calibrated===true?'是':'否'}，fresh=${data.compass_fresh===true?'是':'否'}，拒绝位 0x${compassReasons.toString(16).toUpperCase().padStart(4,'0')}。在控制台运行 magcal status 查看校准覆盖。</small>`
	  : '<strong>未检测到磁力计</strong><small>基础 STAB/ALTHOLD 仍可工作；要求 REQ_MAG_TRUSTED 的航线不能启动。</small>';
  const barometerStatus = document.getElementById('barometer-status');
  const detectedText = value => value===true?'已检测':value===false?'未检测':'未知';
  const expansionStatus = document.getElementById('expansion-probe-status');
  const flowAge = Number(data.optical_flow_age_ms), rangeAge = Number(data.downward_range_age_ms);
  const flowQuality = Number(data.optical_flow_quality), rangeMeters = Number(data.downward_range_m);
  const flowDetail = data.optical_flow_ready===true
    ? `初始化成功；${data.optical_flow_state==='fresh_zero'?`新鲜零位移，质量 ${flowQuality}，${flowAge} ms 前`:data.optical_flow_state==='fresh_motion'?`有效 dx ${Number(data.optical_flow_dx)} / dy ${Number(data.optical_flow_dy)}，质量 ${flowQuality}，${flowAge} ms 前`:data.optical_flow_state==='low_quality'?`新鲜但质量不足（${flowQuality}）`:data.optical_flow_state==='stale'?`样本已过期（${flowAge} ms）`:`当前样本无效（质量 ${flowQuality}，年龄 ${flowAge} ms）`}`
    : `${detectedText(data.optical_flow_detected)}，未就绪`;
  const rangeDetail = data.downward_range_ready===true
    ? `初始化成功；${data.downward_range_usable===true?`有效距离 ${rangeMeters.toFixed(3)} m，${rangeAge} ms 前`:`当前样本无效或过期（状态 ${Number(data.downward_range_status)} / 原始 ${Number(data.downward_range_raw_status)}，年龄 ${rangeAge} ms）`}`
    : `${detectedText(data.downward_range_detected)}，未就绪`;
  expansionStatus.className='diagnostic-summary '+
    ((data.optical_flow_usable===true||data.downward_range_usable===true)?'ok':'offline');
  expansionStatus.innerHTML=`<strong>扩展运动传感器</strong><small>PMW3901：${flowDetail}<br>VL53L1X：${rangeDetail}<br>测距参与高度融合，光流提供局部位移影子状态；位置悬停仍未启用。</small>`;
  const barometerReasons = {
    barometer_unavailable:'未检测到气压计',
    waiting_for_sample:'气压计已检测，正在等待首个样本',
    sample_stale_or_invalid:'气压计样本过期或无效',
    relative_altitude_below_1m:'样本有效；相对高度低于 1 米，快速下降保护暂不介入',
    ready:'高度样本有效，快速下降保护可用'
  };
  const barometerReason = barometerReasons[data.barometer_reason] || '状态未知';
  const barometerAge = Number(data.barometer_age_ms);
  const relativeAltitude = Number(data.relative_altitude_m);
  const verticalSpeed = Number(data.vertical_speed_mps);
  barometerStatus.className='diagnostic-summary '+(data.barometer_guard_ready===true?'ok':'offline');
  barometerStatus.innerHTML=data.barometer_guard_ready===true
    ? `<strong>气压快速下降保护可用</strong><small>相对高度 ${relativeAltitude.toFixed(2)} m，垂直速度 ${verticalSpeed.toFixed(2)} m/s，样本年龄 ${barometerAge} ms。此保护只会有限增加迫降推力。</small>`
    : `<strong>气压高度保护当前降级</strong><small>${barometerReason}。气压计为可选传感器，不影响基础手动解锁；迫降会退回定推力下降。</small>`;
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
  const barometerStatus = document.getElementById('barometer-status');
  barometerStatus.className = 'diagnostic-summary offline';
  barometerStatus.innerHTML = '<strong>无法读取气压高度保护状态</strong><small>连接恢复后刷新；气压计为可选传感器，不影响基础手动解锁。</small>';
	const compassStatus=document.getElementById('compass-status');
	compassStatus.className='diagnostic-summary offline';
	compassStatus.innerHTML='<strong>无法读取磁航向状态</strong><small>连接恢复后刷新。</small>';
  document.getElementById('expansion-probe-status').textContent='光流与测距状态读取失败；连接恢复后刷新。';
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
async function handleButton(idx) {
  if (idx === 5) { toggleRouteRecordingMain(); return; }
  if (idx === 4) {
    // 模式循环：自稳(2) → 定高(3) → 特技(1) → 自稳(2)
    // 不在点击时弹 toast，结果完全依赖后端 resp.m 确认后触发
    let nextBit;
    if (currentFlightMode === 2)      nextBit = 8; // STAB→ALTHOLD
    else if (currentFlightMode === 3) nextBit = 7; // ALTHOLD→ACRO
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
    if(idx===0&&!currentArmed){
      leftThrottleReturnGeneration++;
      stickInputActivated=true;
      leftStick.rawX=0;
      leftStick.rawY=-100;
      const joystick=document.getElementById('joystick-left');
      const radius=joystick.getBoundingClientRect().width/2-10;
      document.getElementById('knob-left').style.transform=
        `translate(calc(-50% + 0px), calc(-50% + ${radius}px))`;
      processJoystickInput();
      updateDisplayAll();
      if(!await sendJoystickData()){
        showToast('零油门确认失败，未发送解锁');
        return;
      }
    }
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

/*======================== 内环 PID 调参 ========================*/
const pidFields=[
  {key:'roll_p',id:'pid-roll-p',pair:'pitch_p',fallback:[0,.20]},
  {key:'roll_i',id:'pid-roll-i',pair:'pitch_i',fallback:[0,.50]},
  {key:'roll_d',id:'pid-roll-d',pair:'pitch_d',fallback:[0,.01]},
  {key:'pitch_p',id:'pid-pitch-p',pair:'roll_p',fallback:[0,.20]},
  {key:'pitch_i',id:'pid-pitch-i',pair:'roll_i',fallback:[0,.50]},
  {key:'pitch_d',id:'pid-pitch-d',pair:'roll_d',fallback:[0,.01]},
  {key:'yaw_p',id:'pid-yaw-p',fallback:[0,1.00]},
  {key:'yaw_i',id:'pid-yaw-i',fallback:[0,.20]},
  {key:'yaw_d',id:'pid-yaw-d',fallback:[0,.05]}
];
let pidPanelOpen=false;
let pidSnapshot=null;
let pidRanges={};
let pidRequestPending=false;

function setPidStatus(message,state=''){
  const status=document.getElementById('pid-status');
  status.textContent=message;
  status.className='pid-status'+(state?' '+state:'');
}

function formatPidValue(value){
  return Number(value).toFixed(6).replace(/0+$/,'').replace(/\.$/,'');
}

function applyPidConfig(data,updateRanges=true){
  if(!data||!data.values)throw new Error('飞控未返回 PID 参数');
  if(updateRanges&&data.ranges)pidRanges=data.ranges;
  const values={};
  pidFields.forEach(field=>{
    const value=Number(data.values[field.key]);
    if(!Number.isFinite(value))throw new Error('PID 参数格式无效');
    const range=pidRanges[field.key]||field.fallback;
    const input=document.getElementById(field.id);
    input.min=range[0];input.max=range[1];input.value=formatPidValue(value);input.classList.remove('invalid');
    document.getElementById(field.id+'-range').textContent=`范围 ${formatPidValue(range[0])}–${formatPidValue(range[1])}`;
    values[field.key]=value;
  });
  pidSnapshot=values;
  document.getElementById('pid-save-button').disabled=data.editable===false||currentArmed;
  return values;
}

async function openPidPanel(){
  if(pidPanelOpen)return;
  try{
    const response=await fetchWithTimeout('/web_rc/status',{cache:'no-store'},STATUS_REQUEST_TIMEOUT_MS);
    const status=await response.json().catch(()=>({}));
    if(!response.ok||typeof status.armed!=='boolean')throw new Error('无法确认飞控上锁状态');
    setArmedState(status.armed);
    if(status.armed)throw new Error('请先上锁并停止电机，再调整 PID');
    pidPanelOpen=true;
    document.getElementById('pid-window').setAttribute('aria-hidden','false');
    document.getElementById('pid-button')?.classList.add('active');
    if(!await loadPidConfig())throw new Error('PID 参数读取失败');
  }catch(error){
    if(pidPanelOpen)closePidPanel();
    showToast(error.message||'PID 页面打开失败');
  }
}

function closePidPanel(){
  if(!pidPanelOpen)return;
  pidPanelOpen=false;
  document.getElementById('pid-window').setAttribute('aria-hidden','true');
  const button=document.getElementById('pid-button');
  button?.classList.remove('active');button?.focus();
}

async function loadPidConfig(){
  if(pidRequestPending)return;
  pidRequestPending=true;
  document.getElementById('pid-save-button').disabled=true;
  setPidStatus('正在读取飞控当前参数…','busy');
  try{
    const response=await fetchWithTimeout('/pid/config',{cache:'no-store'});
    const data=await response.json().catch(()=>({}));
    if(!response.ok||!data.ok)throw new Error(data.error||'PID 参数读取失败');
    applyPidConfig(data,true);
    setPidStatus(data.pending?'参数存储正在处理上一批写入，请稍后再保存。':'已读取当前内环 PID。',data.pending?'busy':'ok');
    document.getElementById('pid-save-button').disabled=!!data.pending||data.editable===false||currentArmed;
    return true;
  }catch(error){
    setPidStatus(error.message||'PID 参数读取失败','error');
    return false;
  }finally{pidRequestPending=false;}
}

function restorePidEdits(){
  if(!pidSnapshot){setPidStatus('尚未读取到可恢复的参数。','error');return;}
  pidFields.forEach(field=>{
    const input=document.getElementById(field.id);
    input.value=formatPidValue(pidSnapshot[field.key]);input.classList.remove('invalid');
  });
  setPidStatus('已恢复为本次读取或保存后的值。');
}

function collectPidValues(){
  const values={};let invalid='';
  pidFields.forEach(field=>{
    const input=document.getElementById(field.id),value=Number(input.value),range=pidRanges[field.key]||field.fallback;
    const valid=input.value.trim()!==''&&Number.isFinite(value)&&value>=Number(range[0])&&value<=Number(range[1]);
    input.classList.toggle('invalid',!valid);
    if(!valid&&!invalid)invalid=field.key;
    values[field.key]=value;
  });
  if(invalid)throw new Error('存在空值或超出范围的 PID 参数');
  return values;
}

async function waitForPidPersistence(expected){
  for(let attempt=0;attempt<8&&pidPanelOpen;attempt++){
    await new Promise(resolve=>setTimeout(resolve,350));
    try{
      const response=await fetchWithTimeout('/pid/config',{cache:'no-store'});
      const data=await response.json().catch(()=>({}));
      if(!response.ok||!data.ok)continue;
      const matches=pidFields.every(field=>Math.abs(Number(data.values[field.key])-expected[field.key])<1e-6);
      if(matches&&!data.pending){
        applyPidConfig(data,false);
        setPidStatus('PID 已写入并由飞控读回确认。','ok');
        return;
      }
    }catch(_){}
  }
  if(pidPanelOpen)setPidStatus('参数已生效并进入保存队列；稍后可点“重新读取”确认。','busy');
}

async function savePidConfig(){
  if(pidRequestPending)return;
  if(currentArmed){setPidStatus('飞控已解锁，禁止保存 PID。','error');return;}
  let values;
  try{values=collectPidValues();}catch(error){setPidStatus(error.message,'error');return;}
  pidRequestPending=true;
  const button=document.getElementById('pid-save-button');button.disabled=true;
  let keepDisabled=false;
  setPidStatus('正在校验并提交全部 9 个参数…','busy');
  try{
    const response=await controlFetch('/pid/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(values)});
    const data=await response.json().catch(()=>({}));
    const messages={requires_disarmed_motors_stopped:'请先上锁并停止全部电机',parameter_storage_busy:'参数存储正忙，请稍后重试',flight_controller_busy:'飞控正在执行校准、测试或序列任务',invalid_pid_value:'参数为空或超出允许范围',pid_apply_failed:'参数应用失败，原值已恢复',parameter_save_not_queued:'参数保存未能排队，原值已恢复'};
    if(!response.ok||!data.ok)throw new Error(messages[data.error]||data.error||'PID 保存失败');
    applyPidConfig(data,false);
    keepDisabled=!!data.pending;
    setPidStatus(data.pending?'参数已生效，正在写入持久存储…':'PID 已保存。',data.pending?'busy':'ok');
    if(data.pending)waitForPidPersistence(values);
  }catch(error){
    setPidStatus(error.message||'PID 保存失败','error');
  }finally{
    pidRequestPending=false;
    button.disabled=currentArmed||keepDisabled;
  }
}

pidFields.forEach(field=>document.getElementById(field.id).addEventListener('input',event=>{
  event.target.classList.remove('invalid');
  if(!field.pair||!document.getElementById('pid-sync-roll-pitch').checked)return;
  const pair=pidFields.find(candidate=>candidate.key===field.pair);
  if(pair)document.getElementById(pair.id).value=event.target.value;
}));
document.getElementById('pid-window').addEventListener('click',event=>{if(event.target.id==='pid-window')closePidPanel();});

/*======================== 调试控制台 ========================*/
function setConsoleStatus(message,state=''){
  const status=document.getElementById('console-status');
  if(!status)return;
  status.textContent=message;
  status.className='console-status'+(state?' '+state:'');
}

async function openConsole() {
  if(consolePanelOpen)return;
  const panel = document.getElementById('console-window');
  const btn   = document.getElementById('console-open-button');
  if(btn)btn.disabled=true;
  try{
    const statusResponse=await fetch('/web_rc/status',{cache:'no-store'});
    const flightStatus=await statusResponse.json().catch(()=>({}));
    if(!statusResponse.ok||typeof flightStatus.armed!=='boolean')throw new Error('无法确认飞控上锁状态');
    setArmedState(flightStatus.armed);
    if(flightStatus.armed)throw new Error('请先上锁再打开调试控制台');
    const enableResponse=await controlFetch('/console/enable',{method:'POST'});
    const enabled=await enableResponse.json().catch(()=>({}));
    if(!enableResponse.ok||!enabled.ok)throw new Error(enabled.error||enabled.e||'控制台启用失败');
    consolePanelOpen=true;
    panel.setAttribute('aria-hidden','false');
    if(btn)btn.classList.add('active');
    document.getElementById('console-close-button').focus();
    document.getElementById('console-output').innerHTML = '';
    consoleLastTotal = 0;
    consoleReadFailureCount = 0;
    setConsoleStatus('控制台已连接，可输入命令或使用快捷按钮。','ok');
    fetchConsoleLogs();
  }catch(error){
    const message=error.message||'调试控制台打开失败';
    setConsoleStatus(message,'error');
    showToast(message);
  }finally{
    if(btn)btn.disabled=false;
  }
}

function closeConsole(){
  if(!consolePanelOpen)return;
  consolePanelOpen=false;
  document.getElementById('console-window').setAttribute('aria-hidden','true');
  const btn=document.getElementById('console-open-button');
  btn?.classList.remove('active');
  clearTimeout(consolePollingTimer);consolePollingTimer=null;
  controlFetch('/console/disable',{method:'POST'}).catch(error=>showToast(error.message||'控制台关闭请求失败'));
  btn?.focus();
}

function toggleConsole(){consolePanelOpen?closeConsole():openConsole();}

function openVibrationCalibrationFromConsole(){
  closeConsole();
  openVibrationCalibrationPage();
}

function openLevelCalibrationFromConsole(){
  closeConsole();
  openLevelCalibrationPage();
}

function startAccelCalibrationFromConsole(){
  if(!window.confirm('六面校准只使用 IMU，不需要磁力计。请依次按提示放稳机体的六个面，每面等待约 8 秒；成功后会自动保存加速度计偏置与比例参数。现在开始？'))return;
  runConsoleCommand('ca');
}

function restartFromConsole(){
  if(currentArmed){showToast('请先上锁并停止电机，再重启飞控');return;}
  if(!window.confirm('确认重启飞控？重启会中断当前网页连接，并清空尚未导出的 RAM 飞行日志。'))return;
  runConsoleCommand('reboot');
  setTimeout(()=>setConsoleStatus('重启命令已提交，等待飞控重新上线…','busy'),250);
}

document.getElementById('console-window').addEventListener('click',event=>{
  if(event.target.id==='console-window')toggleConsole();
});

document.addEventListener('keydown',event=>{
  if(event.key==='Escape'&&pidPanelOpen)closePidPanel();
  else if(event.key==='Escape'&&consolePanelOpen)toggleConsole();
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

    if(consoleReadFailureCount>0)setConsoleStatus('控制台连接已恢复。','ok');
    consoleReadFailureCount=0;
    nextPollDelay = data.has_more ? CONSOLE_CATCHUP_POLL_MS : CONSOLE_BASE_POLL_MS;
  }).catch(error=>{
    consoleReadFailureCount++;
    const timedOut=error.name==='AbortError';
    const message=consoleReadFailureCount===1
      ? (timedOut?'控制台响应较慢，正在重试…':'控制台输出暂时不可用，正在重试…')
      : (timedOut?'控制台连续读取超时，正在重试…':'控制台输出连续读取失败，正在重试…');
    setConsoleStatus(message,consoleReadFailureCount===1?'busy':'error');
    nextPollDelay = CONSOLE_BASE_POLL_MS;
  }).finally(() => {
    clearTimeout(requestTimeout);
    consoleFetchInFlight = false;
    if (consolePanelOpen) scheduleConsolePoll(nextPollDelay);
  });
}

async function sendConsoleCmd() {
  const input = document.getElementById('console-input');
  const cmd = input.value.trim();
  if (!cmd||consoleCommandPending) return;
  consoleCommandPending=true;
  input.disabled=true;
  setConsoleStatus(`正在发送：${cmd}`,'busy');
  try{
    const response=await controlFetch('/console/cmd',{method:'POST',headers:{'Content-Type':'text/plain'},body:cmd});
    const resp=await response.json().catch(()=>({}));
    if(!response.ok||!resp.ok){
      const reason=resp.error||resp.e||(response.status===423?'解锁或电机输出期间暂停调试命令':'命令发送失败');
      throw new Error(reason);
    }
    if(input.value.trim()===cmd)input.value='';
    setConsoleStatus(`已提交：${cmd}`,'ok');
    fetchConsoleLogs();
  }catch(error){
    const message=error.message||'命令发送失败';
    setConsoleStatus(message,'error');
    showToast(message);
  }finally{
    consoleCommandPending=false;
    input.disabled=false;
    input.focus();
  }
}

function runConsoleCommand(command){
  const input=document.getElementById('console-input');
  input.value=command;
  sendConsoleCmd();
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
document.addEventListener('visibilitychange', () => { if (document.hidden) releaseControlsForPageExit(); });
window.addEventListener('pagehide', releaseControlsForPageExit);
window.addEventListener('blur', releaseControlsForPageExit);

// 心跳：2000ms，连续3次失败才判定断连
setInterval(() => {
  if (heartbeatRequestPromise) return;
  heartbeatRequestPromise = sendToESP('/web_rc/heartbeat', {t:4, ts:performance.now()})
    .finally(() => { heartbeatRequestPromise = null; });
}, 2000);
</script>
</body>
</html>
)rawliteral";
#endif
