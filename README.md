# ESP32 华为体脂秤 3 → Home Assistant

通过 **ESP32-C3 SuperMini + ESPHome** 把华为体脂秤 3 的体重、阻抗和心率接入 Home Assistant。另提供可选的身体成分估算、多用户归档与趋势卡片。

> 社区项目，非华为官方产品。仅以华为体脂秤 3 的 Hagrid WSP 实现为目标；不能据此声称支持 Pro、其他型号或所有固件版本。模型值不等于秤屏幕体脂率，不用于诊断。

## 数据链路

```text
华为体脂秤 3
  └─ BLE 广播发现 → GATT 连接/订阅 → WSP 认证与解密
      └─ ESP32-C3 / ESPHome
          └─ Wi-Fi 局域网 / ESPHome Native API
              └─ Home Assistant 原始实体
                  └─ [可选] huawei_scale_archive
                      ├─ 按人归档 / 待认领 / 同轮数据合并
                      ├─ 身体成分模型估算
                      └─ 独立身体指标与纠正后趋势
```

**不使用 MQTT，不需要云端转发。** 首次下载工具和依赖需要互联网；运行时数据在局域网内流转。本项目不包含灯具、投影仪、AI 菜谱或群消息功能。

## 功能

| 内容 | 实现与边界 |
|---|---|
| 原始测量 | 体重 kg、低频阻抗 Ω、心率 bpm；无效或缺失值不编造、不借用上一轮值 |
| 完整快照 | 同一轮随机 ID + 体重/阻抗/心率 JSON；晚到数据更新同一档案记录 |
| 蓝牙 | UUID 发现特征、串行 CCCD 订阅及写入、CRC 校验、会话认证/解密 |
| 测量结束 | 每轮最多一次最终补读，避免空闲→补读→活动的无限反馈；提供独立连接开关 |
| 原有绑定 | 读取已有 HUID 并原样透传；读不到有效 HUID 就停止，不清空、不重绑 |
| 可选 HA | 配置使用者资料、保守自动归档、模糊记录待认领、活动系数、近七天归档趋势 |

**不是完全只读协议**：认证后仍会同步一份协议用户资料，并定期查询状态。HUID 不改写不等于绝不写入任何特征；单次连接也可能亮屏，不能承诺开启连接时绝不唤醒秤。

## 快速开始

需要：ESP32-C3 SuperMini（4MB Flash）、USB 数据线、2.4GHz Wi-Fi、Home Assistant，以及自己设备所需的 MAC 与 CAK/C1/C2 参数。

1. 从 [Releases](https://github.com/Whalefall8844/esp32-huawei-scale3-homeassistant/releases) 下载对应版本 **factory.bin**；或按 [编译与刷机](docs/FIRMWARE.md) 自行编译。
2. 用 USB 刷入，地址 **0x0**。这会替换板上现有程序；不要直接覆盖仍承担其他业务的网关。
3. 使用 [ESPHome Web](https://web.esphome.io/) 的串口配网功能填写自己的 Wi-Fi；通用固件不带公共 AP 密码，也不带家庭凭据。
4. 在 HA 中添加 **ESPHome** 集成，选择设备或填入设备 IP。通用版本启用 ESPHome 首次连接密钥配置；首次配对请在可信局域网完成。具体兼容性见 [说明](docs/FIRMWARE.md)。
5. 进入设备的配置实体，填写 `Scale Bluetooth MAC`、`Scale CAK`、`Scale C1`、`Scale C2`。密钥是 **32 个十六进制字符**，不是 Base64。
6. 按需调整协议资料 `Scale Protocol Gender / Age / Height`，点击 **Apply Scale Configuration**。
7. 打开 **Huawei Scale Connection**，赤脚站上秤，等待测量完成并检查原始实体。
8. 需要按人存档及身体成分估算时，再安装 [可选 HA 组件](home-assistant/README.md)。

**协议密钥不在发行包中。** 必须从你有权使用、与目标型号兼容的客户端实现/配置中获取；参见固定版本的 [openScale 源码](https://github.com/oliexdev/openScale/tree/8ef93b2092745ea84ddd677eac1871452c27b3d5)。如果没有合法可用的参数，请先解决该前提，不能靠填零完成认证。本项目不提供提取他人账户凭据的方法。

## 可选 HA 指标

每位使用者独立显示：体重、BMI、体脂率、水分、肌肉、基础代谢、每日总能量消耗、内脏脂肪等级、骨矿物估算、去脂体重、蛋白质、心率、阻抗和测量时间。活动系数可调整。

- **实测**：体重、阻抗、心率。
- **计算**：BMI 来自体重/身高；其他身体成分来自 openScale 成人四电极模型。
- **非实时身份认证**：归档基于体重与可用阻抗参考范围；重叠、访客或首次使用先待认领。两个人体重接近时不能保证自动识别人。
- HA 时间为首次收到该轮数据的时间，不是秤内部离线历史时间；不导入秤的历史记录。

## 目录

```text
firmware/             独立 ESPHome 配置与 C++/Python 外部组件
home-assistant/       可选 HA 组件、示例配置、趋势卡与仪表盘
docs/                 协议、编译、使用与限制
requirements.txt      固定 ESPHome 构建版本
LICENSE / NOTICE      GPL-3.0-or-later 与上游署名
```

## 验证状态

原有项目在 ESP32-C3 + ESPHome 2026.7.4 / ESP-IDF 5.5.5 与 HA 2026.9.3 上使用过实际秤测量。这里的**通用配置界面和多人可配置版本是新整理的版本**，编译与功能核对结果见 [发行说明](docs/RELEASE.md)；不能把原有部署的实测结果当成这个通用二进制已经实机验证。开源整理不触发任何现有设备 OTA，也不重启现有 HA。

## 隐私与许可

未附带使用者资料、真实地址、密钥、测量记录或家庭配置。运行时填写的参数仍会保存在 ESP32/HA 中；密码样式输入框不代表加密存储，见 [隐私说明](docs/PRIVACY.md)。

GPL-3.0-or-later。身体成分与 Hagrid 协议参考固定提交的 openScale，详见 [NOTICE](NOTICE) 和 [第三方说明](docs/THIRD_PARTY.md)。再分发修改版/固件时请遵守相应源码提供和署名义务。
