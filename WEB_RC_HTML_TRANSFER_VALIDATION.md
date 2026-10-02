# 网页截断与 `openRoutePage` 未定义修复（2026-10-02）

## 复现与原因

目标设备 IP `192.168.31.189`、MAC `20:50:0d:33:58:40`。旧固件主页声明 `Content-Length: 108435`，实际一次仅收到 8616 字节，截在 CSS 附近；另两次收到完整内容。`openRoutePage()` 定义位于后面的 JavaScript，故该次部分页面中的按钮会抛出 `ReferenceError`。设备所用 ESP32 `WebServer::send_P` 对整页调用一次 `write_P`，且不检查返回的写入字节数；短写时浏览器得到长度不符的页面。

## 修复与板端结果

主页改为每次最多写入 1024 字节，按实际写入字节数推进，短写继续发送，并在无写入进展超过 5 秒时退出。仅修改主页发送路径，页面文本和遥控逻辑未改变。ESP32-D `min_spiffs` 编译成功，程序 `1,353,827 B`，静态 RAM `124,532 B`。镜像 [cf-drone-html-stream-20261002.bin](deliverables/cf-drone-html-stream-20261002.bin) 大小 `1,353,968 B`、SHA-256 `9ba284a35162f2088f75c0aec2c9d6d0b152980940344fb45bbe528891e0f3f6`。

刷写前 USB 串口读出 MAC `20:50:0d:33:58:40`，板端旧应用与 [cf-drone-web-throttle-hold-20261002.bin](deliverables/cf-drone-web-throttle-hold-20261002.bin) 独立校验一致。新镜像写入 app0 `0x10000` 后，esptool 写入校验和独立 `verify-flash` 均通过。新固件主页连续 5 次返回 `108435/108435` 字节，均含 `openRoutePage()` 与完整结束标签。真实浏览器点击“开环序列”成功，页面显示序列状态；控制台没有该 `ReferenceError` 或 `ERR_CONTENT_LENGTH_MISMATCH`。

刷写复位清空了内存序列；已从先前保存的设备原文重新上传并读回逐字节核对。最终状态：上锁、零油门、故障数 0；序列 `ready`、19 段、3.5 秒、`recorded=false`，模式 STAB；保存的下降推力仍为 `0.400`。未启动电机或回放。

浏览器验证期间曾有一次 `/web_rc/status` 请求 `ERR_CONNECTION_RESET`，板端随后显示一次 Wi-Fi 断连计数。分块发送解决已复现的主页短写路径，但 Wi-Fi 连接波动仍可中断其他请求，需要另行观察。
