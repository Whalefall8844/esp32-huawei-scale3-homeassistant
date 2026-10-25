# Huawei Scale Archive（可选 Home Assistant 组件）

SPDX-License-Identifier: GPL-3.0-or-later

把 ESP32 提供的**完整单次测量 JSON**保存到本地 SQLite，为一个或多个 YAML 档案计算指标、认领归属并绘制可纠正的近七天趋势。本目录不包含数据库、日志、凭据、真实个人资料，也不包含其他集成。它是固件的可选扩展；不安装也可查看固件原始传感器。

## 安装

1. 把本目录 `custom_components/huawei_scale_archive` 整个目录复制到 **HA 配置目录下**同名的 `custom_components/huawei_scale_archive`；保留 LICENSE 和 NOTICE。
2. 把 `www/scale-archive-trend.js` 复制到 **HA 配置目录下** `www/scale-archive-trend.js`。
3. 将下方配置合并到 HA 的 `configuration.yaml`，先在自己的 HA 中替换虚构资料。一个档案即可运行，也可增加更多稳定的英文小写/数字/下划线档案 ID；不能省略或留空 `profiles`。
4. 重启 HA。在开发者工具→状态确认完整测量实体，再修改 `source_entity`。这里预期 `sensor.huawei_scale3_huawei_scale_measurement`，对应 ESPHome 设备名 `huawei_scale3` 与文本传感器 `Huawei Scale Measurement`；实际实体可能因命名规则、已有注册项或手动重命名而不同。**必须选择状态为完整 JSON 的实体，不是单独体重、阻抗或心率实体。**示例 ID 未经你的 HA 实例核验。
5. 在设置→仪表盘→资源中添加 `/local/scale-archive-trend.js?v=2`，类型为 JavaScript 模块；若使用 YAML 资源模式，则在现有 `lovelace.resources` 下增加下方资源。不要同时重复配置两种资源模式。
6. 新建空白仪表盘，在原始配置编辑器中粘贴 `scale-dashboard.yaml`。该文件是完整的仪表盘示例，而非单卡配置。若修改档案 ID，同步修改示例中实体 ID；实体注册表发生重名时以 HA 实际 ID 为准。

## 完整配置（全部个人资料与参考数值均为虚构示例）

```yaml
# SPDX-License-Identifier: GPL-3.0-or-later
# 全部为虚构示例，不是真实个人资料。请在自己的HA中替换；不要提交个人资料。
# 合并到 configuration.yaml，不要重复创建 huawei_scale_archive 顶级键。
huawei_scale_archive:
  # 预期默认实体ID；请在开发者工具→状态确认实际ID（设备名/重命名会改变ID）。
  source_entity: sensor.huawei_scale3_huawei_scale_measurement
  profiles:
    person_1:
      name: 示例人物一（虚构）
      sex: male
      birth_date: "1990-01-01"
      height_cm: 170
    person_2:
      name: 示例人物二（虚构）
      sex: female
      birth_date: "1992-01-01"
      height_cm: 160
  # 不填time_zone时沿用 hass.config.time_zone；需要覆盖时取消下一行注释。
  # time_zone: UTC
  # 相对于HA配置目录，运行时自动创建；仓库不包含数据库。
  storage_path: huawei_scale_archive.sqlite3
  weight_tolerance_kg: 3.0
  impedance_tolerance_ohm: 100.0
  # 可选：关联HA person实体，不参与识别；键必须存在于profiles。
  person_entities: {}
  # 默认不提供初始参考：分别手动认领以学习真实参考；下方数值纯属虚构演示。
  initial_references: {}
  # initial_references:
  #   person_1: {weight: 65.0, impedance: 500.0}  # 虚构，非实测
  #   person_2: {weight: 55.0, impedance: 600.0}  # 虚构，非实测
```

YAML 资源模式的资源片段（合并到已有 `lovelace` 下）：

```yaml
lovelace:
  resources:
    - url: /local/scale-archive-trend.js?v=2
      type: module
```

`sex` 为模型选择参数，只接受 `male` 或 `female`；出生日期用于称重当地日期的周岁，身高单位 cm。`person_entities` 可选地填写 `person_1: person.example_person` 等实际 HA 实体，它只用于显示关联、不读取位置或参与识别。`initial_references` 为可选的体重 kg / 阻抗 Ω 起始参考，不产生称重记录；上面的注释值不能冒充实测值。

## 称重、认领、纠正

- 完整 JSON 的形状为 `{"id":"synthetic-cycle-001","weight":65.0,"impedance":500.0,"heart_rate":70}`（**虚构协议示例，不是实测记录**）。`id` 必须为每轮称重稳定且跨轮唯一的测量 ID；阻抗、心率可缺省或为 null。不会拼接独立实体的不同时间状态。
- 安装时不会导入现有旧状态。新状态事件到来时，以 HA 第一次收到该测量 ID 的时间归档，非秤内置时间，也不会补造历史。
- 没有参考时记录进入待认领。选择 `select.scale_claim_record`、`select.scale_claim_person` 后按 `button.scale_claim`。每个档案分别认领至少一次；参考齐全后，仅体重与可用阻抗在容差内**唯一匹配**时自动归档；重叠或未匹配保持待认领。
- 最近五条已归档记录的体重与有效阻抗分别取中位数作为学习参考（不足五条用已有记录；无记录才使用可选初始参考）。只有体重可用时，路由只按体重，不假造阻抗或成分。
- 同一个 ID 再次到达会更新同一行、保留首次时间与已认领归属；稍后补齐的阻抗/心率仍属于同一测量。未认领记录补齐阻抗时可以重新匹配。
- 最新记录传感器包含 `measurement_id`；待认领数量传感器的 `records` 属性最多展示最近二十条，`pending_count` 是全部数量。旧待认领记录可按已知 ID 调用服务。历史归属纠正也使用以下动作（ID 必须替换为真实已归档 ID）：

```yaml
action: huawei_scale_archive.claim
data:
  measurement_id: synthetic-cycle-001
  person_id: person_2
```

不填 `measurement_id` 时使用当前选中的待认领记录；不填 `person_id` 时使用当前选中档案。选择器用“名称 [档案ID]”区分同名档案。无需额外自动化、网络账户或 API Token。

## 指标与趋势

每个档案有体重、BMI、体脂率、水分、骨骼肌比例、基础代谢率 BMR、每日总能量 TDEE、内脏脂肪等级、骨矿物质、去脂体重、蛋白质、心率、阻抗和测量时间。实体格式为 `sensor.scale_<档案ID>_<指标>`；活动系数为 `number.scale_<档案ID>_activity`（默认 1.2，可调 1.0–2.5）。活动系数只影响之后新测量的 TDEE，不批量改写历史；手动认领/纠正会以当时档案资料重新计算该条记录。

体重、阻抗、心率来自采集；BMI 来自体重与身高；其余成分为 openScale 成人四电极模型估计，不是华为 SDK、秤屏体脂或医疗诊断。未成年、无阻抗或超出模型适用范围时，只保留可用基础指标，估算值为空。具体模型来源与限制见组件 NOTICE 和 `model.py`。

趋势卡直接读取体重/体脂率传感器的 `archive_history`，按测量 ID 和首次时间展示最近 168 小时、最多 1000 条记录。纠正归属后，原档案趋势移除该记录、新档案趋势加入；没有记录时不会保留旧曲线。不要改回 Recorder `history-graph`，因为 Recorder 历史 UI 状态不会随着归属纠正而改写。本版本不额外增加删除服务。

## 时区、存储与兼容性

- 存储层默认使用 `hass.config.time_zone`，可由 `time_zone` 指定 IANA 时区。它决定称重当天年龄和待认领显示时间。数据库保留带时区的首次接收时间；跨偏移时间按实际时刻排序/筛选。
- JS 卡可单独指定 `time_zone`，否则优先传感器 `archive_time_zone`，再取 HA 时区，最后 UTC。示例没有硬编码地区。
- `storage_path` 默认 `huawei_scale_archive.sqlite3`，相对于 HA 配置目录，可指向独立的本地文件；运行时自动建立。请自行备份数据库并避免把它或实际资料提交到仓库。
- YAML 配置加载，未提供 UI 配置流；变更档案或时区需重启。档案 ID 是归属键和实体唯一 ID 的组成部分；改 ID 不会自动迁移旧记录，删除档案也不自动转移或删除旧数据。显示名可以修改。
- 与私人旧版本相比，**移除了内置档案**；现在必须配置 `profiles`。不自动兼容或迁移任何私人档案 ID、资料或初始参考。已有数据库若需保留归属，需在本地明确配置相同 ID；或设置新的 `storage_path` 从空库开始。不要把私人数据库用于公开示例。
- 使用 HA 现代 Sensor/Number/Select/Button、YAML discovery 与状态变更事件 API；原实现 API 参考为 HA Core 2026.9.3。本次只做离线语法与存储功能检查，未在实际 HA 或浏览器中安装验证，不承诺其他版本兼容。

## 许可与出处

本目录代码、示例与文档采用 **GPL-3.0-or-later**。组件内包含完整 GPLv3 `LICENSE` 与 `NOTICE`，保留 openScale 原始系数、公式、固定提交与源码校验信息。上游为 [oliexdev/openScale](https://github.com/oliexdev/openScale)，固定提交 `8ef93b2092745ea84ddd677eac1871452c27b3d5`，文件 `android_app/app/src/main/java/com/health/openscale/core/bluetooth/libs/HuaweiScale3BodyComposition.kt`。本公开改版不宣称是官方华为 SDK。
