# GreenMind 植物个性化 AI 闭环记录 v1

## 1. 本次目标

在不破坏原有 AI 闭环的前提下完成：

1. SQLite 植物档案和查询测试；
2. 后端确定性植物规则；
3. 植物个性化 Mock AI；
4. 设备 ID、植物 ID 和兼容协议；
5. 默认 `GM001/pothos` 板端请求。

本次不实现植物选择页面和图片联网传输。

## 2. 当前默认身份

```text
DEVICE_ID: GM001
SPECIES_ID: pothos
DISPLAY_NAME: POTHOS
```

这些值集中定义在：

```text
firmware/APP/agriculture/app_config.h
```

TFT AI 页面显示 `GM001/POTHOS`，串口日志在启动和 AI 请求时打印设备与植物 ID。

主页植物卡片显示 `POTHOS`。AI 完成后，拟人化绿萝按规则层返回状态更新：

| AI 状态 | 主页形象 |
|---|---|
| `NORMAL` | 绿色叶片和笑脸 |
| `WARN` | 黄绿色叶片和平嘴 |
| `DANGER` | 棕色叶片、皱眉和红色提示 |
| 传感器无效 | 灰色植株和叉眼 |

`THINKING` 阶段仍显示思考气泡。SYSTEM 页显示 `GM001 / POTHOS`，方便实物核对。

## 3. 兼容协议

旧请求继续支持：

```text
V1|AI_REQ|REQ_ID|T|H|LIGHT|LIGHT_LEVEL
```

旧请求在网关端映射为 `GM001/pothos`。

当前固件发送：

```text
V1|PLANT_AI_REQ|REQ_ID|DEVICE_ID|SPECIES_ID|T|H|LIGHT|LIGHT_LEVEL
```

示例：

```text
V1|PLANT_AI_REQ|15|GM001|pothos|30|45|80|STRONG
```

结果帧保持不变：

```text
V1|AI_RESULT|15|WARN|LIGHT_HIGH|NO_NEED|MOVE_TO_SHADE
```

因此 TFT 结果解析和原有 AI 状态机不需要重写。

## 4. 后端处理顺序

```text
解析并校验 ID 与传感器值
→ 查询 devices
→ 查询 plant_species + plant_requirements
→ PlantRuleEvaluator 计算三项适配性
→ Mock 或 DeepSeek 生成植物相关解释
→ 强制使用规则层状态码
→ 保存 ai_analysis_logs
→ 返回原有 AI_RESULT
```

规则层输出：

```text
temperature_fit: LOW / SUITABLE / HIGH
humidity_fit:    LOW / SUITABLE / HIGH
light_fit:       LOW / SUITABLE / HIGH
```

模型不能修改 `STATUS / ISSUE / WATERING / ADVICE`。如果 DeepSeek 返回的枚举与
规则层冲突，网关保留规则结果，并使用规则层的安全英文说明。

## 5. 个性化验证样例

输入相同环境：

```text
T=30 C
H=45 %
LIGHT=80
```

绿萝 `pothos`：

```text
WARN / LIGHT_HIGH / MOVE_TO_SHADE
```

仙人掌 `cactus`：

```text
NORMAL / NONE / KEEP_CURRENT
```

这证明差异来自 SQLite 植物档案和确定性规则，不依赖大模型随机发挥。

## 6. 异常边界

| 场景 | 网关结果 |
|---|---|
| 植物不存在 | `PLANT_NOT_FOUND` |
| 设备不存在 | `DEVICE_NOT_FOUND` |
| ID 格式非法 | `BAD_VALUE` |
| 数据库异常 | `DATABASE_ERROR` |
| 模型失败 | 保留原有模型错误码 |

任何异常都不会产生水泵、GPIO 或 PWM 控制。

## 7. 验证结果

- Python 自动化测试：21 项通过；
- Keil 重建：0 Error，7 个原有 TFT 未使用变量警告；
- Code：31440 bytes；
- RO-data：19208 bytes；
- RW-data：208 bytes；
- ZI-data：3608 bytes。

## 8. 实物验证

1. 烧录最新 `GreenMind.hex`；
2. 启动 Mock 网关；
3. 确认 TFT AI 页显示 `GM001/POTHOS`；
4. 点击 `ANALYZE`；
5. 电脑日志应出现：

```text
device=GM001 plant=pothos
fit=.../.../...
```

6. TFT 应按原流程进入 `PREPARING → THINKING → VALIDATING → DONE`；
7. Mock 通过后，再使用 DeepSeek 网关验证真实调用。
