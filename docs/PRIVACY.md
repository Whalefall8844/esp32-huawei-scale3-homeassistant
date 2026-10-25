# 隐私与本地配置

公开源码/通用二进制不包含家庭 Wi-Fi、设备 MAC、CAK/C1/C2、API PSK、个人生日/身高、体重历史、日志或第三方 Webhook。示例资料均是明确标注的虚构值。

运行时仍需注意：

- CAK/C1/C2 在 ESP32 Preferences 中保存；密码输入样式只隐藏界面，不等于闪存加密。
- 文本配置实体可能被 HA API、备份和 Recorder 保存。按实际实体 ID 将密钥文本实体从 Recorder 排除；这不会删除旧备份/历史，也不会阻止有权限的 HA 用户读取当前状态。
- 原始完整测量和按人记录属于健康隐私；不要在 Issue 上传未脱敏的日志、数据库或 HA 备份。
- ESPHome INFO 日志含测量数值；需要排错分享日志时先去掉值、地址和身份信息。
- 首次配对使用可信局域网/USB；设备不要端口转发到公网。本项目不管理你的网络或 HA 账户权限。
- `home-assistant` 示例里的个人资料填在你自己的 HA 配置中，不回传给作者。

在 HA 中使用以下配置前，先去设备页核对实际 entity_id（设备名可能带后缀）。不要重复创建已有 `recorder:` 根项：

```yaml
recorder:
  exclude:
    entities:
      - text.huawei_scale3_scale_cak
      - text.huawei_scale3_scale_c1
      - text.huawei_scale3_scale_c2
```

排除上述密钥实体，不要误排除你希望保存的测量实体。HA 归档组件自身只保存测量和活动系数，不读取这些密钥文本实体。
