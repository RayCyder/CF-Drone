# CF Drone Web API

本文档描述 CF Drone 固件当前实现的 HTTP 接口，供网页、移动端和桌面客户端开发使用。接口以固件源码为唯一权威来源；当前协议没有独立版本号，也不提供 OpenAPI 描述文件。

## 1. 服务概览

### 1.1 支持状态

| 芯片 | Wi-Fi | Web API |
| --- | --- | --- |
| ESP32 | 默认启用 | 默认启用 |
| ESP32-S3 | 默认启用 | 默认启用 |
| ESP32-C3 | 默认关闭 | 默认关闭 |

ESP32-C3 在 `board_config.h` 中关闭了 `BOARD_WIFI_ENABLED` 和 `BOARD_WEB_RC_ENABLED`，因此默认固件不会启动本文档中的服务。

### 1.2 默认网络配置

| 项目 | 默认值 |
| --- | --- |
| 工作模式 | AP 自建热点 |
| SSID | `Drone_WiFi` |
| 密码 | 默认开放热点；若 NVS 中保存过 AP 密码则使用保存值 |
| HTTP 地址 | `http://192.168.4.1` |
| HTTP 端口 | `80` |
| 旧版兼容端口 | `8080`，重定向至 80；ESP32-C3 不启用 |

SSID 和密码可以通过控制台命令修改，重启后生效：

```text
ap <ssid> <password>
sta <ssid> <password>
```

### 1.3 传输与安全

- 协议为明文 HTTP，不支持 HTTPS。
- 接口没有用户身份认证、账户会话、TLS 或权限角色。飞行控制接口另有运行时控制凭证：短期 `lease` 标识当前控制页面，持久化于浏览器的 `stop` 连续性凭证用于刷新恢复与应急动作。它们不是用户认证令牌。
- 固件没有配置 CORS 响应头；建议客户端直接连接无人机热点并访问设备地址，不要依赖跨域浏览器调用。
- Web 遥控属于飞行控制通道。客户端测试时必须拆除螺旋桨，并实现断线和后台切换处理。
- HTTP 服务运行在独立任务中，控制输入通过队列交给飞控主循环；客户端应避免并发洪泛和长时间占用连接。

## 2. 通用约定

### 2.1 Base URL

AP 模式默认使用：

```text
http://192.168.4.1
```

STA 模式下应使用路由器分配给飞控的实际 IP 地址。

### 2.2 内容类型

| 场景 | Content-Type |
| --- | --- |
| Web RC 请求 | `application/json` |
| 本地航线计划上传 | `text/plain` |
| JSON 响应 | `application/json` |
| 控制台命令 | `text/plain` |
| 遥控页面 | `text/html` |

当前服务端实际读取 HTTP 原始正文，不严格校验请求的 `Content-Type`。客户端仍应发送表中规定的类型。

### 2.3 状态码

| 状态码 | 含义 |
| --- | --- |
| `200` | 请求已处理或命令已入队 |
| `301` | 访问旧版 8080 端口时重定向至 80 端口 |
| `400` | 缺少正文、协议解析失败或命令为空 |
| `409` | 序列未上传、序列正运行、日志忙，或当前飞行状态条件不满足 |
| `503` | 控制台命令队列已满或序列缓冲区内存不足 |

固件没有注册统一的 JSON 404 处理器。未知路径的响应格式由 ESP32 `WebServer` 默认行为决定，客户端不应假定其为 JSON。

### 2.4 飞行模式

| 值 | 名称 | 说明 |
| ---: | --- | --- |
| `0` | `RAW` | 直控 |
| `1` | `ACRO` | 特技模式 |
| `2` | `STAB` | 自稳模式 |
| `3` | `ALTHOLD` | 实验性定高模式；仅在已解锁、有飞行推力且垂直估计健康时接受 |
| `4` | `AUTO` | 自动模式 |

### 2.5 Web RC 通用响应

`POST /web_rc` 和 `POST /web_rc/heartbeat` 成功时返回以下结构：

| 字段 | 类型 | 必有 | 说明 |
| --- | --- | --- | --- |
| `s` | string | 是 | 固定为 `"ok"` |
| `m` | integer | 是 | 当前飞行模式 |
| `arm` | integer | 是 | `1` 已解锁，`0` 已上锁 |
| `rt` | integer | 是 | 本次请求的消息类型，与请求字段 `t` 对应 |
| `bi` | integer | 否 | 按钮编号，仅有效按钮事件返回 |
| `bs` | integer | 否 | 按钮状态，仅有效按钮事件返回 |
| `warn` | string | 否 | 待展示的警告消息 |

示例：

```json
{
  "s": "ok",
  "m": 2,
  "arm": 0,
  "rt": 2,
  "bi": 0,
  "bs": 0,
  "warn": "油门过高，无法解锁"
}
```

`warn` 是一次性消息，只会被按钮事件或心跳响应取出并清除。摇杆响应不会消费该消息。因此客户端必须处理按钮和心跳响应，不能只处理摇杆响应。

## 3. 接口清单

| 方法 | 路径 | 用途 |
| --- | --- | --- |
| `GET` | `/` | 获取内嵌网页遥控器 |
| `POST` | `/web_rc/lease` | 获取或恢复当前页面的控制租约 |
| `POST` | `/web_rc` | 提交摇杆或按钮消息 |
| `POST` | `/web_rc/heartbeat` | 提交连接心跳 |
| `GET` | `/web_rc/status` | 查询 Web RC 和电池状态 |
| `POST` | `/route/upload` | 上传并校验本地航线计划到飞控 RAM |
| `GET` | `/route/plan` | 上锁时读回已上传序列和批次号 |
| `POST` | `/route/takeover` | 明确退出序列并切回自稳手动控制 |
| `GET` | `/route/status` | 查询飞控端序列状态 |
| `GET` | `/console` | 增量读取调试日志 |
| `POST` | `/console/cmd` | 提交调试命令 |
| `POST` | `/console/enable` | 开启 Web 日志收集 |
| `POST` | `/console/disable` | 关闭 Web 日志收集 |
| `GET` | `/telemetry` | 获取实时遥测与机载日志快照页面 |
| `GET` | `/logs/status` | 查询机载飞行日志状态 |
| `GET` | `/logs.csv` | 冻结并下载机载飞行日志 CSV |
| `POST` | `/logs/resume` | 在上锁且电机停止时恢复机载日志滚动记录 |

## 4. 遥控页面

### `GET /`

上锁时返回完整遥控器页面。解锁或电机正在输出时返回小型恢复页，只保留双摇杆、状态、心跳、迫降、上锁和急停，避免 100 KiB 级完整页面下载占用控制服务；重新上锁后可加载完整页面。

遥控页顶部的“航线录制”按钮会打开独立编辑页面。V1 每行包含五个字段；V2 每行追加融合相对高度和相对陀螺航向。浏览器录制 V2 使用 `# WEB_RC_RECORDED_V2`，手写 V2 使用 `# CF_ROUTE_META schema=2 source=authored policy=slew`。V2 必须全为七列，不能混入五列段。三个姿态轴仍是 `-100..100` 的遥控输入。最多 128 段、总时长 30 分钟、正文 4096 字节。先在上锁且电机停止时上传校验并读回，再切换 AUTO；操作者解锁后主循环再次检查批次、模式、垂直估计和链路并自动启动。飞控用两组定长缓冲保存动作，不在控制循环分配内存。编辑内容可存入浏览器 `localStorage`；飞控 RAM 内序列在重启后清除。完整操作、手工格式和定高算法见[航线录制、定制、回放与定高算法](../ROUTE_RECORDING_REPLAY_GUIDE.md)。

V2 回放从当前起点重新对齐记录的高度和航向，高度由垂直控制器闭环，航向由短程相对陀螺角闭环。横滚和俯仰仍按录制杆量回放；PMW3901 水平位置只作诊断影子估计，所以该功能仍不能保证实际水平位移、半径或落点。浏览器断开、控制循环停顿超过 100 ms、序列完成、垂直估计失效或操作者停止都会进入安全降级。迫降不能识别近地或触地，操作者须确认情况后上锁。执行期间摇杆不会隐式接管；点击“接管摇杆”或切换到 STAB/ACRO 才会明确停止序列并恢复手动控制。急停/上锁按钮仍立即停机。

返回编译进固件的完整 HTML、CSS 和 JavaScript 遥控页面。

成功响应：

```http
HTTP/1.1 200 OK
Content-Type: text/html
```

该页面也是接口调用的参考客户端，包含摇杆发送、心跳、状态查询及控制台逻辑。

## 5. Web RC 控制接口

### 5.1 本地航线接口

`POST /route/upload` 接收原始文本正文。兼容规则固定为：无头且全五列是 V1 authored/slew；`# WEB_RC_RECORDED_V1` 是 V1 recorded/direct；`# WEB_RC_RECORDED_V2` 是 V2 recorded/direct；`# CF_ROUTE_META schema=2 source=authored policy=slew` 是按时间推进的 V2 authored/slew；追加 `advance=arrival heading=relative` 的精确元数据开启到达后计时和短时相对陀螺航向。到达模式中，高度目标变化段的持续时间是最大等待时间，误差不超过 0.10 m且垂速绝对值不超过 0.15 m/s连续 500 ms 后才进入下一段；高度目标不变段仍完整定时执行。超时或垂直估计失效会进入受控迫降。元数据必须唯一且精确位于正文第一行。V2 必须全部为七列；缺 schema 的七列、混合列数、未知 schema/source/policy/advance/heading 均返回 `schema_mismatch`。字段范围、段数、时长和正文容量限制保持不变。只有上锁且电机停止才接受；上传成功返回 `plan_revision`，不会解锁或启动。失败的解析不会替换当前有效序列。

`GET /route/plan` 仅在上锁时读回当前文本，并通过 `X-Plan-Revision` 返回批次号。页面确认读回内容和批次后请求 AUTO 模式；操作者随后解锁，主循环在当前批次有效、Web RC 在线且模式仍为 AUTO 时自动启动。固件没有 `/route/start` 或 `/route/stop`。`GET /route/status` 返回 `state`、段数、当前段、总时长、`plan_revision`、`pending`、原因、解锁状态、模式以及结构化 `schema/source/policy`。`POST /route/takeover` 明确切回 STAB 手动控制；页面迫降按钮使用 Web RC 按钮消息。切换到 STAB/ACRO 也会取消序列。

V1 保持原有开环杆量语义。V2 增加高度和相对航向闭环，但水平位置仍未闭环。Web RC 的“急停”仍会立即 disarm；停止/完成/断连进入现有受控下降流程，不能确认着陆。

### 5.2 提交摇杆数据

```http
POST /web_rc
Content-Type: application/json
```

请求正文：

```json
{
  "t": 1,
  "th": -100,
  "r": 0,
  "p": 0,
  "y": 0,
  "ts": 12345.67
}
```

| 字段 | 类型 | 建议范围 | 服务端缺省值 | 说明 |
| --- | --- | ---: | ---: | --- |
| `t` | integer | `1` | 无 | 消息类型，摇杆消息必须为 `1` |
| `th` | number | `-100..100` | `0` | 油门原始值，`-100` 映射至 0%，`100` 映射至 100% |
| `r` | number | `-100..100` | `0` | 横滚，正值表示右滚 |
| `p` | number | `-100..100` | `0` | 俯仰，正值表示前进方向 |
| `y` | number | `-100..100` | `0` | 偏航，正值表示顺时针 |
| `ts` | number | 非负 | `0` | 客户端时间戳；当前仅解析，不参与控制或防重放 |

处理规则：

- 姿态轴会被约束在 `-100..100`，应用 6% 后端死区，再映射到 `-30..30` 度。
- 横滚与俯仰最终乘以 `0.85` 灵敏度系数；偏航乘以 `0.68`。
- 油门应用 6% 低端死区，最终写入 `0..1` 的控制量。
- 非有限值或绝对值超过 1000 的值会回退到该轴上一次有效值。
- 请求中缺少某个控制字段时，该字段会使用 0，而不是保持上一次值。客户端必须每次发送完整的四轴数据。

成功响应：Web RC 通用响应，`rt` 为 `1`。

错误响应：

```json
{"e":"no data"}
```

或：

```json
{"e":"parse failed"}
```

命令行示例：

```bash
curl -X POST http://192.168.4.1/web_rc \
  -H 'Content-Type: application/json' \
  -d '{"t":1,"th":-100,"r":0,"p":0,"y":0,"ts":12345}'
```

### 5.3 提交按钮事件

```http
POST /web_rc
Content-Type: application/json
```

请求正文：

```json
{
  "t": 2,
  "b": 0,
  "s": 1,
  "ts": 12345.67
}
```

| 字段 | 类型 | 范围 | 说明 |
| --- | --- | --- | --- |
| `t` | integer | `2` | 消息类型 |
| `b` | integer | `0..15` | 按钮位编号 |
| `s` | integer | `0` 或非零 | `0` 松开，非零按下 |
| `ts` | number | 非负 | 当前仅解析，不参与控制 |

已定义的按钮编号：

| `b` | 动作 | 触发方式 | 备注 |
| ---: | --- | --- | --- |
| `0` | 解锁 | 上升沿 | IMU 故障、低电或油门过高时拒绝并返回 `warn` |
| `1` | 上锁 | 上升沿 | 设置为上锁状态 |
| `2` | 急停 | 上升沿 | 上锁并将目标推力清零 |
| `6` | STAB | 上升沿 | 切换到自稳模式 |
| `7` | ACRO | 上升沿 | 切换到特技模式 |
| `8` | ALTHOLD 请求 | 上升沿 | 满足已解锁、推力高于怠速且垂直估计健康时切换；否则返回警告 |
| 其他 | 保留 | 无 | 只更新按钮位掩码，没有已定义动作 |

客户端应发送一次按下和一次松开。固件内嵌页面使用 100 ms 脉冲：

```text
POST {"t":2,"b":0,"s":1,...}
等待约 100 ms
POST {"t":2,"b":0,"s":0,...}
```

成功响应：Web RC 通用响应，`rt` 为 `2`，有效按钮还会返回 `bi` 和 `bs`。

### 5.4 提交心跳

```http
POST /web_rc/heartbeat
Content-Type: application/json
```

请求正文：

```json
{
  "t": 4,
  "ts": 12345.67
}
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `t` | integer | 心跳固定为 `4` |
| `ts` | number | 客户端时间戳，当前不参与服务端判断 |

固件内嵌页面每 2 秒发送一次心跳。成功响应为 Web RC 通用响应，`rt` 为 `4`，可能携带一次性 `warn`。

连接时序：

- 任意有效的摇杆、按钮或心跳消息都会刷新最后收包时间。
- 最后收包后 10 秒以内，`enabled` 为 `true`。
- 飞行且 Web RC 正在控制时，超过 8 秒未收到消息会启动自动下降。
- 内嵌页面在摇杆静止时每 200 ms 强制重发完整摇杆数据；活动摇杆最多约 20 Hz 检测并发送。

客户端建议：

- 前台控制期间至少每 2 秒发送心跳。
- 即使摇杆没有变化，也应周期性重发完整四轴状态；建议沿用 200 ms。
- 应串行化控制请求或限制并发，避免旧响应覆盖新 UI 状态。
- 页面进入后台、网络切换或应用暂停时，应立即将姿态轴回中、油门回当前悬停前馈，尝试发送一次后停止续发旧杆量。

### 5.5 查询状态

```http
GET /web_rc/status
```

成功响应：

```json
{
  "enabled": true,
  "active": true,
  "voltage": 4.08,
  "throttle": 0.0,
  "roll": 0.0,
  "pitch": 0.0,
  "yaw": 0.0,
  "faults": 64,
  "uptime_ms": 123456,
  "wifi_connected": true,
  "wifi_disconnects": 1,
  "wifi_last_disconnect_ms": 85000,
  "device_id": "20500D335840",
  "firmware_build": "Oct  5 2026 12:34:56",
  "stick_age_ms": 75,
  "packet_age_ms": 75,
  "http_idle_drops": 2,
  "control_source": 2,
  "thrust_target": 0.25,
	"compass_detected": true,
	"compass_ready": true,
	"compass_calibrated": true,
	"compass_fresh": true,
	"compass_trusted": true,
	"compass_age_ms": 12,
	"compass_reject_reasons": 0,
	"magnetic_heading_deg": 83.2,
	"navigation_heading_deg": 82.9,
	"magnetic_innovation_deg": 0.3,
	"magnetic_field_norm": 1632.0,
  "barometer_available": true,
  "barometer_usable": true,
  "barometer_guard_ready": true,
  "barometer_reason": "ready",
  "barometer_age_ms": 42,
  "relative_altitude_m": 1.42,
  "vertical_speed_mps": -0.31
}
```

| 字段 | 类型 | 单位 | 说明 |
| --- | --- | --- | --- |
| `enabled` | boolean | - | 10 秒有效窗口内收到过摇杆包和 Web RC 消息；只有心跳不构成有效遥控 |
| `active` | boolean | - | Web RC 已被标记为当前控制输入且仍在有效窗口内 |
| `voltage` | number | V | 当前电池电压；无有效读数时为 `0.0` |
| `throttle` | number | % | 后端处理后的油门，约 `0..100` |
| `roll` | number | 度 | 后端死区处理后的横滚，约 `-30..30` |
| `pitch` | number | 度 | 后端死区处理后的俯仰，约 `-30..30` |
| `yaw` | number | 度 | 后端死区处理后的偏航，约 `-30..30` |
| `faults` | integer | 位掩码 | 当前活动诊断故障；位定义见 `diagnostics.h`，每次查询前刷新 |
| `uptime_ms` | integer | ms | 本次启动后的时间；下降到较小值表示设备曾重启 |
| `wifi_connected` | boolean | - | 查询时 STA 是否已关联 |
| `wifi_disconnects` | integer | 次 | 本次启动后 STA 从已连接转为断开的次数 |
| `device_id` | string | - | 芯片 eFuse MAC 派生的 12 位设备标识 |
| `firmware_build` | string | - | 当前镜像的编译日期和时间；用于将标定记录绑定到具体构建 |
| `wifi_last_disconnect_ms` | integer | ms | 最近一次 STA 断开的启动时间；`0` 表示尚未记录 |
| `stick_age_ms` | integer | ms | 距最近摇杆包的时间；`-1` 表示从未收到 |
| `packet_age_ms` | integer | ms | 距最近 Web RC 消息或心跳的时间；`-1` 表示从未收到 |
| `http_idle_drops` | integer | 次 | 本次启动后因连接建立但未发送请求而主动关闭的 TCP 连接数 |
| `control_source` | integer | 枚举 | 实际控制来源；`2` 为 Web RC，`6` 为受控下降 |
| `thrust_target` | number | 0..1 | 当前控制器目标推力；可能与摇杆油门不同 |
| `compass_detected` | boolean | - | QMC5883P 身份探测成功 |
| `compass_ready` | boolean | - | 驱动已连续取得有效样本；不代表已校准或可信 |
| `compass_calibrated` | boolean | - | 已加载通过范围校验的磁力计校准 |
| `compass_fresh` | boolean | - | 最近磁样本年龄不超过 100 ms |
| `compass_trusted` | boolean | - | 校准、新鲜度、场强、倾角和航向创新门控均通过，可供导航使用 |
| `compass_age_ms` | integer | ms | 最近磁样本年龄；无样本为 `-1` |
| `compass_reject_reasons` | integer | 位掩码 | 不可信原因：bit0 未探测、bit1 未就绪、bit2 未校准、bit3 陈旧、bit4 总线、bit5 场强、bit6 创新、bit7 倾角 |
| `magnetic_heading_deg` | number | ° | 校准和倾斜补偿后的磁航向 |
| `navigation_heading_deg` | number | ° | 陀螺短时连续、磁航向长期修正后的导航航向 |
| `magnetic_innovation_deg` | number | ° | 磁航向与融合航向的环绕误差 |
| `magnetic_field_norm` | number | 原始标度 | 校准后三轴磁场模长，用于干扰门控 |
| `mag_cal_active` | boolean | - | 磁力计校准是否正在采集 |
| `mag_cal_attempt_started` | boolean | - | 本次启动后是否开始过一轮采集 |
| `mag_cal_candidate_ready` | boolean | - | 当前样本数、三轴跨度和比例约束是否已满足保存条件 |
| `mag_cal_samples` | integer | 个 | 当前一轮采集的有效磁力计样本数 |
| `mag_cal_elapsed_ms` | integer | ms | 当前或最近一轮采集持续时间 |
| `mag_cal_last_sample_age_ms` | integer | ms | 采集中最近样本年龄；尚无样本为 `-1` |
| `mag_cal_progress_pct` | integer | % | 样本与三轴覆盖的最小进度；候选有效时为 100 |
| `mag_cal_sample_progress_pct` | integer | % | 相对最低 300 个样本的采集进度 |
| `mag_cal_axis_x_pct` / `y` / `z` | integer | % | 相对每轴最低 500 原始计数跨度的覆盖进度 |
| `mag_cal_span_x` / `y` / `z` | integer | 原始计数 | 本轮各轴最大值减最小值 |
| `mag_cal_quality` | number | 0..1 | 候选三轴覆盖均衡度及样本量质量 |
| `barometer_available` | boolean | - | 运行时是否实际检测到 BMP388；构建启用不等于实体传感器存在 |
| `barometer_usable` | boolean | - | 当前估计是否有效、有限且样本年龄不超过 250 ms |
| `barometer_guard_ready` | boolean | - | 样本可用且相对高度至少 1 m，迫降快速下降保护具备介入条件；这不是定高或触地能力 |
| `barometer_reason` | string | - | `ready`、`barometer_unavailable`、`waiting_for_sample`、`sample_stale_or_invalid` 或 `relative_altitude_below_1m` |
| `barometer_age_ms` | integer | ms | 当前气压样本年龄；无样本时为 `-1` |
| `relative_altitude_m` | number | m | 相对本次传感器基准的气压高度；无样本时为 `0.0`，不能作为离地高度 |
| `vertical_speed_mps` | number | m/s | 气压估计垂直速度，向上为正；无样本时为 `0.0` |
| `vertical_estimator_ready` | boolean | - | IMU 预测和至少一种高度观测已形成健康的垂直估计 |
| `vertical_estimator_degraded` | boolean | - | 垂直估计可用但未同时融合气压与低空测距 |
| `fused_altitude_m` | number | m | IMU、气压和可用下视测距融合后的相对高度，向上为正 |
| `fused_vertical_speed_mps` | number | m/s | 融合垂直速度，向上为正 |
| `vertical_acceleration_mps2` | number | m/s² | 去除重力后的世界坐标垂直加速度 |
| `height_source` | integer | 枚举 | `0` 无高度观测、`1` 气压、`2` 测距、`3` 气压与测距融合 |
| `altitude_control_active` | boolean | - | ALTHOLD 或 V2 AUTO 的垂直控制器当前是否生效 |
| `altitude_target_m` | number | m | 当前融合坐标系中的高度目标 |
| `vertical_speed_target_mps` | number | m/s | 高度外环或油门杆生成的目标升降速度 |
| `altitude_thrust_command` | number | 0..1 | 垂直控制器输出的归一化推力命令 |
| `flow_position_x_m` / `flow_position_y_m` | number | m | 光流与有效下视距离形成的诊断影子位置，不进入水平闭环 |
| `flow_velocity_x_mps` / `flow_velocity_y_mps` | number | m/s | 诊断影子水平速度 |
| `attitude_yaw_deg` | number | 度 | 当前陀螺积分相对航向，不是磁北绝对航向 |
| `arm_ready` | boolean | - | 按飞控当前完整解锁门槛计算的即时结果；仅表示查询时状态 |
| `arm_reason` | string | - | `arm_ready=false` 时的首个阻止原因；条件可能在下一次查询前变化 |

固件内嵌页面每 2 秒查询一次该接口，使用 `voltage`、`faults` 和解锁就绪字段更新界面。诊断故障为零不代表一定可解锁；请以 `arm_ready` 和 `arm_reason` 为准。

## 6. 实时遥测与机载日志快照

### 6.1 实时遥测页

```http
GET /telemetry
```

返回内嵌实时遥测页面。页面通过 `http://<host>:81/stream` 订阅 Server-Sent Events，接收 `schema`、`sample` 和 `system-log` 事件。实时 `sample` 事件以 2 Hz 推送，用于观察当前状态；网页端“下载当前页面 CSV”只导出浏览器已捕获的低频样本。

需要定位故障时，应使用同一页面上的“下载故障快照”按钮。该按钮请求 `/logs.csv`，下载飞控内存中的冻结 100 Hz 日志快照，而不是 2 Hz 页面样本。

### 6.2 查询机载日志状态

```http
GET /logs/status
```

该接口只读取状态，不会触发冻结或恢复。

成功响应：

```json
{
  "state": "FROZEN",
  "generation": 3,
  "rowCount": 400,
  "reasonMask": 64,
  "missedSamples": 0,
  "triggerUs": 123456789
}
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `state` | string | `ROLLING`、`POST_TRIGGER`、`FROZEN` 或 `UNAVAILABLE` |
| `generation` | integer | 冻结快照代号；下载期间如果变化，服务端会中止输出 |
| `rowCount` | integer | 当前冻结快照可导出的行数 |
| `reasonMask` | integer | 冻结原因位掩码：低 16 位为诊断故障，最高位表示解锁后上锁，最高位存在时第 24–30 位为上锁原因编号 |
| `missedSamples` | integer | 记录期间丢失的样本数 |
| `triggerUs` | integer | 触发冻结的微秒时间戳 |

上锁原因编号：`0` 未分类，`1` Web 上锁，`2` Web 急停，`3` 遥控摇杆手势，`4` 串口命令，`5` MAVLink 命令，`6` 关键硬件故障，`7` 倒置保护，`8` 低电压怠速保护。按 `(reasonMask >> 24) & 0x7f` 提取；只有最高位存在时才有意义。整板掉电或重启不会经过软件上锁流程，因此不能凭缺少该编号判断重启原因。

### 6.3 下载机载日志 CSV

```http
GET /logs.csv
```

该接口在 HTTP 任务中低优先级流式输出 CSV。请求开始时必须已上锁且电机停止；服务端会调用日志模块冻结当前滚动日志。如果日志模块正在 `POST_TRIGGER` 后触发采样阶段，冻结会被拒绝并返回 `409`。下载期间每一行都会再次检查上锁状态、电机停止状态和 `generation` 是否仍匹配；任一条件变化时，连接会提前结束。

成功响应：

```http
HTTP/1.1 200 OK
Content-Type: text/csv; charset=utf-8
Content-Disposition: attachment; filename="cf-drone-flight-log.csv"
```

错误响应：

| HTTP 状态 | 条件 |
| --- | --- |
| `409` | 已解锁、电机仍在输出、日志处于后触发采样阶段，或日志未冻结 |
| `500` | 当前日志列数超过 HTTP 导出容量 |
| `503` | 固件未集成机载日志快照接口 |

CSV 每行最多按 40 个浮点列导出，HTTP 行缓冲区为 1024 字节。列名与固件日志模块当前 `getLogColumnName()` 返回值一致。

### 6.4 恢复机载日志滚动记录

```http
POST /logs/resume
```

该接口只有在已上锁且电机停止时接受。成功后调用日志模块恢复滚动记录，不读取或下载当前冻结快照。

成功响应：

```json
{
  "ok": 1,
  "state": "ROLLING",
  "generation": 4,
  "rowCount": 0
}
```

错误响应：

| HTTP 状态 | 响应 | 条件 |
| --- | --- | --- |
| `409` | `{"ok":0,"error":"motors active"}` | 未上锁或电机仍在输出 |
| `503` | `{"ok":0,"error":"flight log unavailable"}` | 固件未集成机载日志快照接口 |

## 7. Web 控制台接口

Web 控制台将固件日志保存在固定大小的环形缓冲区：ESP32 和 ESP32-S3 默认 50 行，每行最多 239 个字符；ESP32-C3 配置为 20 行、每行最多 159 个字符，但 C3 默认不开启 Web 服务。

控制台接口没有独立认证，并可执行包括解锁、电机测试、参数修改、恢复参数和重启在内的命令。面向最终用户的客户端不应默认暴露控制台。

### 7.1 开启日志收集

```http
POST /console/enable
```

成功响应：

```json
{"ok":1}
```

开启后，固件 `print()` 输出会复制到 Web 环形缓冲区。此前没有被收集的历史串口日志无法补取。

### 7.2 关闭日志收集

```http
POST /console/disable
```

成功响应：

```json
{"ok":1}
```

关闭只停止新增 Web 日志，不清空现有缓冲区。

### 7.3 增量读取日志

```http
GET /console?since=0&limit=20
```

查询参数：

| 参数 | 类型 | 默认值 | 说明 |
| --- | --- | ---: | --- |
| `since` | integer | 当前缓冲区最早可用位置 | 客户端已消费到的全局行号 |
| `limit` | integer | `20` | 最大返回行数；不超过固件环形缓冲区容量 |

成功响应：

```json
{
  "total": 42,
  "next": 40,
  "has_more": true,
  "lines": [
    "Setup Motors",
    "Motors initialized"
  ]
}
```

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `total` | integer | 固件启动以来写入 Web 缓冲区的总行数，单调递增 |
| `next` | integer | 下一次请求应传入的 `since` |
| `has_more` | boolean | `next < total` 时为 `true` |
| `lines` | string[] | 本次返回的日志行 |

如果 `since` 已早于环形缓冲区当前保留范围，服务端会自动从最早仍可用的位置返回，客户端无法恢复已经被覆盖的行。

推荐拉取流程：

1. 打开控制台时调用 `/console/enable`。
2. 以 `since=0` 开始读取。
3. 将响应的 `next` 保存为下一次 `since`。
4. `has_more=true` 时快速继续读取；否则降低轮询频率。
5. 关闭控制台时调用 `/console/disable`。

内嵌页面在追赶日志时约每 80 ms 拉取一次，正常状态约每 500 ms 拉取一次。

### 7.4 提交控制台命令

```http
POST /console/cmd
Content-Type: text/plain
```

正文是单行命令，例如：

```text
ps
```

成功响应仅表示命令已入队，并不表示命令执行成功：

```json
{"ok":1,"queued":1}
```

命令执行结果通过 `/console` 日志读取。

错误响应：

```http
HTTP/1.1 400 Bad Request
Content-Type: application/json

{"ok":0,"e":"empty command"}
```

```http
HTTP/1.1 503 Service Unavailable
Content-Type: application/json

{"ok":0,"e":"queue full"}
```

限制：

- 队列容量为 4 条。
- 每条命令缓冲区为 64 字节，实际最多保留 63 个字符。
- 主循环每次最多执行一条排队命令。
- HTTP 成功响应不包含命令输出。

已实现命令：

| 命令 | 作用 | 风险 |
| --- | --- | --- |
| `help` / `motd` | 显示帮助 | 低 |
| `p` | 列出参数 | 低 |
| `p <name>` | 读取参数 | 低 |
| `p <name> <value>` | 修改参数 | 高 |
| `preset` | 恢复参数 | 高 |
| `time` | 显示运行时间和循环频率 | 低 |
| `ps` | 显示欧拉角姿态 | 低 |
| `psq` | 显示四元数姿态 | 低 |
| `imu` | 显示 IMU 信息 | 低 |
| `arm` | 解锁 | 极高 |
| `disarm` | 上锁 | 高 |
| `raw` | 切换 RAW 模式 | 高 |
| `stab` | 切换 STAB 模式 | 高 |
| `acro` | 切换 ACRO 模式 | 高 |
| `auto` | 切换 AUTO 模式 | 高 |
| `rc` | 显示 RC 状态 | 低 |
| `wifi` | 显示 Wi-Fi 状态 | 低 |
| `ap <ssid> <password>` | 保存 AP 配置 | 高，重启生效 |
| `sta <ssid> <password>` | 保存 STA 配置 | 高，重启生效 |
| `mot` | 显示电机输出 | 低 |
| `log [dump]` | 显示日志头或转储日志 | 低 |
| `cr` | 校准遥控器 | 高，长时间运行 |
| `ca` | 校准加速度计 | 高，长时间运行 |
| `mfr` | 测试右前电机 | 极高 |
| `mfl` | 测试左前电机 | 极高 |
| `mrr` | 测试右后电机 | 极高 |
| `mrl` | 测试左后电机 | 极高 |
| `sys` | 显示系统任务和资源 | 低 |
| `reset` | 重置姿态和陀螺偏置滤波器 | 高 |
| `reboot` | 重启设备 | 高 |

### Wi-Fi 网页配置

Wi-Fi 使用 `WIFI_MODE=0` 关闭、`1` 连接现有路由器（STA）、`2` 启动无人机配置热点（AP）。首次启动或 STA 连接超时后会启动无需密码的 `Drone_WiFi` 配置热点，手机连接热点后访问 `http://192.168.4.1/wifi`，填写路由器 SSID 和密码。保存后飞控自动重启并加入路由器；手机随后也连接同一路由器，通过路由器分配给飞控的 IP 打开网页遥控器。`wifi` 命令可查看当前模式和 IP。

连接到路由器后，可在遥控页面底部点“Wi-Fi 设置”进入配置页更换网络。路由器凭据保存在设备 NVS 中；密码不会打印到日志。若已连接网络失效，飞控会在 20 秒后重新开放配置热点。浏览器端输入需要 1–32 字节 SSID；密码可以留空用于无密码网络，否则长度需为 8–63 字节。

## 8. 错误处理

### 8.1 Web RC 错误结构

| HTTP 状态 | 响应 | 条件 |
| --- | --- | --- |
| `400` | `{"e":"no data"}` | 请求没有可读取的正文 |
| `400` | `{"e":"parse failed"}` | 正文为空、缺少 `{` 或缺少精确的 `"t":` 标记 |

### 8.2 控制台错误结构

| HTTP 状态 | 响应 | 条件 |
| --- | --- | --- |
| `400` | `{"ok":0,"e":"empty command"}` | 去除首尾空白后命令为空 |
| `503` | `{"ok":0,"e":"queue full"}` | 四条命令队列已满 |

### 8.3 客户端处理要求

- 始终先检查 HTTP 状态，再解析 JSON。
- 对超时、连接拒绝、非 JSON 响应和字段缺失做容错。
- 不要把单次 `200` 解释成持续连接；应基于最近一次成功通信时间维护客户端连接状态。
- 不要根据请求时间戳判断服务端是否接受了数据；当前服务端不回显或校验 `ts`。
- `warn` 为可选字段，客户端应按 UTF-8 文本直接展示，不应依赖固定中文内容做业务判断。

## 9. 客户端状态机建议

推荐客户端至少维护以下状态：

```text
DISCONNECTED
  -> HTTP 请求成功 -> CONNECTED

CONNECTED
  -> 周期发送完整摇杆数据和心跳
  -> 连续请求失败或应用进入后台 -> DEGRADED

DEGRADED
  -> 尝试发送最低油门和中立姿态
  -> 恢复通信 -> CONNECTED
  -> 超过客户端超时 -> DISCONNECTED
```

建议行为：

- 初次连接后先请求 `/web_rc/status`，再开始发送控制数据。
- 初始油门使用 `th=-100`，其余轴使用 0。
- 控制消息始终包含 `th`、`r`、`p`、`y` 四个字段。
- 按钮采用按下和松开成对发送，并处理两次响应中的 `warn`。
- 本地显示的解锁和模式状态以服务端响应的 `arm`、`m` 为准，不做乐观确认。
- 至少保存最近一次成功响应时间、最近一次完整摇杆值和当前按钮状态。
- 页面打开时会请求 `POST /web_rc/lease`。上锁时新页面立即取得租约并使旧页面失效；飞行中只有携带现有 `stop` 连续性凭证的原浏览器刷新页可恢复，其他页面收到 `web_rc_flight_takeover_forbidden`。租约缺失、过期和仍由其他页面持有分别返回 `web_rc_lease_required`、`web_rc_lease_expired`、`web_rc_lease_in_use`。
- `lease` 只保存在页面内存并随租约更新；`stop` 同时保存在当前 origin 的 `localStorage` 和 `sessionStorage`。AP 与 STA 地址属于不同 origin，存储不共享；设备重启后旧凭证失效。

## 10. 当前协议限制

以下内容是当前实现的明确限制，客户端设计应预留兼容层：

1. 没有 API 版本号或能力协商接口。
2. 没有认证、授权、TLS 或请求签名。
3. 控制权由当前页面的短期 `lease` 标识；上锁时新页面可替换，飞行中只允许持有当前 `stop` 连续性凭证的刷新恢复，不提供多客户端并行控制或用户身份认证。
4. JSON 使用字符串查找解析，不是完整 JSON 解析器。
5. `ts` 不参与时序校验，无法拒绝延迟包或重放包。
6. 成功响应没有请求序列号，异步并发时难以把响应与请求严格关联。
7. 没有统一错误对象或稳定的错误代码枚举。
8. 没有 CORS 配置。
9. ALTHOLD 依赖健康垂直估计，只能从已有飞行推力的已解锁状态进入；当前参数仍需带桨实飞调校。
10. Wi-Fi 配置使用未加密 HTTP，且没有认证；不要把无人机配置热点或飞控控制页暴露到不可信网络。

## 11. 源码对应关系

| 内容 | 源码位置 |
| --- | --- |
| HTTP 路由与协议处理 | `web_rc.ino` |
| 官方内嵌前端调用示例 | `web_rc_html.h` |
| Wi-Fi 初始化与默认凭据 | `wifi.ino` |
| 芯片功能开关 | `board_config.h` |
| 按钮动作和模式处理 | `control.ino` |
| Web RC 失联保护 | `safety.ino` |
| 控制台命令实现 | `cli.ino` |
| 参数注册 | `parameters.ino` |

本文档对应当前仓库实现，不代表未来版本的兼容性承诺。修改上述接口实现时，应同步更新本文档。
