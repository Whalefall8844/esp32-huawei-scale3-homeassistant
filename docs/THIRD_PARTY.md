# 第三方与复现依赖

项目自身：GPL-3.0-or-later。上游署名/修改说明见根目录 NOTICE 和 HA 组件 NOTICE；不能把衍生模型重新标成 MIT。

| 上游 | 版本 / 来源 | 许可与用途 |
|---|---|---|
| openScale | `8ef93b2092745ea84ddd677eac1871452c27b3d5` | GPL-3.0-or-later；Hagrid WSP、Scale3 字段和成人四电极模型 |
| ESPHome | `2026.7.4` | MIT 等随附许可；组件框架和 Native API |
| ESP-IDF | `5.5.5` | Apache-2.0 及其组件各自许可；系统、BLE/Wi-Fi、构建工具 |
| mbedTLS | ESP-IDF 自带版本 | 随上游许可；HMAC/AES/SHA |
| esphome/noise-c | `0.1.11`（由固定 ESPHome 依赖解析） | 上游各文件许可；API 加密 |
| esphome/libsodium | `1.10021.0` | MIT/ISC 等随附许可；Noise 加密依赖 |
| Improv SDK | `1.2.4` | Apache-2.0；USB 配网 |
| espressif/mdns | `1.11.0` | 上游随附许可；局域网发现 |
| Home Assistant Core | 参考 `2026.9.3` | Apache-2.0；用户自行安装，不包含在固件内 |

固定上游源代码入口：

- [openScale](https://github.com/oliexdev/openScale/tree/8ef93b2092745ea84ddd677eac1871452c27b3d5)
- [ESPHome 2026.7.4](https://github.com/esphome/esphome/tree/2026.7.4)
- [ESP-IDF v5.5.5](https://github.com/espressif/esp-idf/tree/v5.5.5)
- [noise-c](https://github.com/esphome/noise-c)

发行版同时提供与 tag 对应的项目源码。依赖由固定的 ESPHome/ESP-IDF 构建流程下载；准确构建配置与解析后的组件锁文件见发行说明/附件。再分发完整二进制时，应保留依赖自带的许可与通知，并履行 GPL 对应源码义务，不能仅附一条模糊的“参考 openScale”说明。
