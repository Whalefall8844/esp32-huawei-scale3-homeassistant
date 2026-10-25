# 编译、刷机与首次设置

## 支持基准

- ESP32-C3 SuperMini，4MB Flash；ESPHome board id `esp32-c3-devkitm-1`。
- ESPHome **2026.7.4**，ESP-IDF **5.5.5**；C3 是 BLE，不是经典蓝牙 SPP。
- Wi-Fi 功率保持 **8.5dB**，软件共存启用。其他板型未验证，不直接刷此二进制。
- USB 供电、开发板板载天线即可，无额外 GPIO 接线。

## 从源码编译

Python 版本须满足 ESPHome 2026.7.4 的要求。以下在仓库根目录执行：

```sh
python -m venv .venv
# Linux/macOS: . .venv/bin/activate
# PowerShell: .\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python -m esphome compile firmware/huawei-scale3.yaml
```

Windows 建议将编译缓存放在短路径；下面是示例路径，不是必须路径：

```powershell
$env:ESPHOME_ESP_IDF_PREFIX = 'C:\esp-idf-tools'
$env:ESPHOME_BUILD_PATH = 'C:\esphome-build'
python -m esphome compile firmware/huawei-scale3.yaml
```

初次构建会下载编译工具与依赖；不要把编译目录、`secrets.yaml`、`.esphome` 或 ELF 打包为公开源码。

## 刷写

Release 中的 `huawei-scale3-c3.factory.bin` 是合并后的首次 USB 刷机镜像，**写入偏移 0x0**。先核对发行页 SHA-256，再用 ESPHome Web 或：

```sh
python -m pip install esptool
python -m esptool --chip esp32c3 --port PORT write-flash 0x0 huawei-scale3-c3.factory.bin
```

`PORT` 替换为自己的串口。若无法进入下载模式，可按住 BOOT 再接 USB/按 RESET，具体取决于板子。不要把 app-only 镜像写到 0x0；不要将 ESP32-C3 镜像刷到 ESP32/S3。

通用版**没有网络 OTA、Web 控制页面或开放 AP**；升级同样使用 USB。高级用户若要 OTA，应在自己的本地配置中启用并设置独有密码，自行编译。不要把私人版本放到公开 Release。

## 配网与 HA

通用版不预置 Wi-Fi。刷写后用 Chromium 浏览器打开 ESPHome Web，串口连接后使用 Improv Wi-Fi 设置网络；串口日志同时占用会导致连接失败。

HA 添加 ESPHome 集成，设备名默认带 MAC 后缀避免多台重名。没有填写编译期 API key，配置为 `api: encryption: {}`：ESPHome 在首次配对时允许控制端设置独有 Noise PSK，之后持久化并使用加密连接。

首次未设置 PSK 的阶段不是已鉴权状态，请使用可信网络。需要支持该首次密钥配置流程的 HA / aioesphomeapi；本次整理参照 HA 2026.9.3 接口，但未用新二进制完成实际首次配对验证。如果旧版 HA 无法配对，可升级，或复制 YAML 为被 Git 忽略的 `huawei-scale3.local.yaml`，在本地给 API 配置独有密钥：

```yaml
api:
  encryption:
    key: !secret api_key
  reboot_timeout: 0s
```

同目录私有 `secrets.yaml` 中的 `api_key` 应是随机生成的 32 字节 Base64。不要使用网上的示例共享密钥。

## 秤参数与连接

1. HA 设备配置实体填入自己的蓝牙 MAC 和 CAK/C1/C2（每项 16 字节，32 位 hex）。
2. 协议性别：`1` 男、`0` 女；年龄/身高默认是**示例协议资料**，使用前修改。它不是 HA 多人识别的配置，也不代表当前称重的人。
3. 点 Apply 后客户端先断开，空闲时再应用参数。格式错误或仍未断开就保持关闭并提示状态。
4. 首次默认连接关闭；准备称重时打开连接开关。后续重启会恢复用户保存的开关意图，应用参数成功后才启用连接。
5. 手动关闭连接可停止网关主动和秤通信；它不等于让秤硬件强制休眠。

已有绑定 HUID 会原样透传；未绑定设备或无法读到有效 HUID 时本驱动停止该次会话。请先使用合法客户端完成正常初始化，不执行工厂重置或“清空绑定”来排障。

## 常见问题

- **无实体**：先确认 ESP32 已联网、HA ESPHome 集成已连接；无需安装额外 HA 组件也应有原始传感器。
- **连不上秤**：核对型号/地址/密钥，关闭手机上占用秤连接的应用，打开连接开关后触发称重；不同秤端固件可能不兼容。
- **只有体重**：阻抗测量通常需要赤脚、良好电极接触与完整测量；未提供有效阻抗或心率就保持缺失，不生成替代值。
- **空秤亮屏**：每轮补读限制消除了已知无限读取路径，但状态查询/连接本身仍可能亮屏；暂时关闭连接并观察。不要把该开关写进周期性自动化反复开关。
- **HA 重启期间测量**：没有离线持久队列；可在恢复时收到 ESPHome 当前保留的快照，但不保证补齐断线期间多次测量。
- **Wi-Fi 不稳定**：先检查电源、RSSI、位置及共存；不靠提高发射功率掩盖问题。
