# 主循环轻量化与网页快速停机

本页的镜像、刷写和端口 82 验证记录对应当时的历史版本。第六轮复核后，当前源码增加凭证校验和快速迫降 `/land`，并删除路线页的手动启动及“停止并下降”入口；见 [第六轮复核](docs/FEATURE_REVIEW_ROUND6.md)。以下尺寸与板端结果不代表当前源码镜像。

目标设备：ESP32-D，MAC `20:50:0d:33:58:40`。本版本默认关闭逐阶段计时和高频网页输入事件日志，保留故障诊断、主循环超时计数、飞行日志、遥控失联保护及电机输出。

| 编译宏 | 默认 | 作用 |
| --- | ---: | --- |
| `CF_DRONE_ENABLE_LOOP_STAGE_MONITOR` | 0 | 每周期各阶段 `micros()` 计时、统计和详细超时追踪 |
| `CF_DRONE_ENABLE_WEB_INPUT_EVENT_LOG` | 0 | 油门变化时格式化并记录 `WEB_RC_INPUT` 事件 |
| `CF_DRONE_ENABLE_FLIGHT_LOG` | 1 | 飞行日志采集；关闭后下降校准采样仍继续 |
| `CF_DRONE_ENABLE_FAST_STOP_SERVER` | 1 | 独立 82 端口的上锁和急停入口 |

可在构建时覆盖，例如：

```sh
CF_DRONE_ENABLE_LOOP_STAGE_MONITOR=1 CF_DRONE_ENABLE_WEB_INPUT_EVENT_LOG=1 \
  tools/build_esp32d.sh /tmp/cf-drone-diagnostic-build
```

网页按下上锁或急停时，除了原有 80 端口请求，还会向独立 82 端口发送 `POST /lock` 或 `POST /kill`。通信核的单独任务只解析请求首行，向主循环投递停机原因；主循环开始处立即调用 `disarm()` 清零电机输出。停机后 2 秒内拒绝重新解锁，避免排队的旧请求重新解锁。82 端口仅接受这两种停机动作。

局限：82 端口能避开 80 端口的长请求队列，无法突破 Wi-Fi 断连、浏览器完全冻结或主循环自身卡死；真实电机停机延迟尚未在有桨条件下测量。飞行日志默认仍开启，以保留后续台架分析证据。

## 编译与板端核对

默认配置 ESP32-D `min_spiffs` 编译成功：程序 1,352,323 B（68%），静态 RAM 124,548 B（38%）。镜像 [cf-drone-light-loop-fast-stop-20261002.bin](deliverables/cf-drone-light-loop-fast-stop-20261002.bin) SHA-256 为 `72e16d42fdc4fabdea3d14667439439f869debb36e13a9bc73fd8bfd68027682`。

刷写前确认 USB MAC 为 `20:50:0d:33:58:40`；写入 app0 `0x10000` 后，独立 `verify-flash` 校验一致。复位后向 82 端口发 `POST /lock` 得到 204，状态显示已上锁、油门 0、目标推力 0、故障 0，短时禁止解锁提示生效；之后状态恢复 `arm_ready=true`。未启动电机。
