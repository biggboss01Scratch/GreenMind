# AgricultureSys TCP 通信协议 v1

## 1. 目的

定义 STM32/ESP07 与电脑 Python 网关之间的应用层协议。协议运行在 TCP 长连接之上，用于传感器上传、AI 请求和建议结果返回。

该协议不承载模型 API Key，也不允许远程启动水泵。

## 2. 传输角色

| 组件 | 角色 |
|---|---|
| ESP07 | TCP Server，监听 STA IP:8080 |
| Python 网关 | TCP Client，主动连接 ESP07 |
| STM32 USART3 | 解析 ESP07 `+IPD`，通过 `AT+CIPSEND` 返回 |

电脑热点负责局域网接入；大模型 HTTPS 通信只发生在电脑网关一侧。

## 3. 帧格式

```text
V1|TYPE|FIELD1|FIELD2|...<CR><LF>
```

规则：

- 编码：ASCII；
- 版本字段固定为 `V1`；
- 字段分隔符：`|`；
- 结束符：回车加换行；
- 单帧最大 120 字节（包括结束符）；
- 字段中不得包含 `|`、回车或换行；
- 枚举使用大写字母和下划线；
- 数字使用十进制，不发送浮点文本；
- 接收端遇到未知类型时返回错误，不执行任何硬件动作。

TCP 是字节流，正式解析器必须支持：

- 一帧被拆成多次 `+IPD`；
- 多帧合并在一次 `+IPD`；
- 半帧跨主循环保存；
- 超长帧丢弃至下一个换行；
- 一次解析最多处理有限帧数，避免饿死其他任务。

当前 `wifi_tft` 中按一次 `+IPD` 处理一条消息的代码仅用于全双工冒烟测试，不能直接作为农业系统正式解析器。

## 4. 请求编号

- AI 请求编号 `REQ_ID` 范围为 0～65535；
- STM32 每发起一次有效请求递增，65535 后回到 0；
- 同时只允许一个未完成 AI 请求；
- 网关的 `ACK`、`AI_RESULT` 或 `ERROR` 必须携带相同编号；
- 编号不匹配的迟到结果应记录并忽略。

## 5. 连接流程

```text
Python 连接 ESP07:8080
← V1|HELLO|STM32|READY
→ V1|HELLO_ACK|GATEWAY|READY
← V1|PING
→ V1|PONG
进入业务状态
```

### 5.1 STM32 HELLO

```text
V1|HELLO|STM32|READY
```

与现有全双工测试兼容。后续版本可增加固件版本，但不得改变前四个字段的含义。

### 5.2 网关确认

```text
V1|HELLO_ACK|GATEWAY|READY
```

STM32 收到后将网关状态设为可用。只有 TCP 连接但没有 `HELLO_ACK` 时，不允许发起 AI 请求。

### 5.3 心跳

任一端可以发送：

```text
V1|PING
```

对端回复：

```text
V1|PONG
```

MVP 不要求高频心跳。建议连接空闲 10 秒以上再发送，连续多次失败后关闭并重连。

## 6. AI 请求

STM32 发送：

```text
V1|AI_REQ|REQ_ID|T|H|LIGHT|LIGHT_LEVEL
```

字段：

| 字段 | 范围/枚举 | 示例 |
|---|---|---|
| `REQ_ID` | 0～65535 | `42` |
| `T` | DHT11 整数温度 | `31` |
| `H` | 0～100 整数空气湿度 | `39` |
| `LIGHT` | 0～100 相对亮度 | `82` |
| `LIGHT_LEVEL` | `DARK/NORMAL/STRONG` | `STRONG` |

示例：

```text
V1|AI_REQ|42|31|39|82|STRONG
```

网关通过基本校验并开始处理后立即回复：

```text
V1|ACK|42|AI_REQ
```

ACK 只表示网关接收请求，不表示模型分析完成。

### 6.1 模型调用阶段

网关在调用过程中发送：

```text
V1|AI_STAGE|REQ_ID|PREPARING
V1|AI_STAGE|REQ_ID|THINKING
V1|AI_STAGE|REQ_ID|VALIDATING
```

STM32 根据阶段更新 TFT。`THINKING` 只表示请求正在由模型处理，不显示或传输模型内部推理内容。每收到一个合法阶段，STM32 刷新当前请求的超时计时。

## 7. AI 结果

网关发送：

```text
V1|AI_RESULT|REQ_ID|STATUS|ISSUE|WATERING|ADVICE
```

### 7.1 STATUS

```text
NORMAL
WARN
DANGER
ERROR
```

### 7.2 ISSUE

MVP 允许：

```text
NONE
TEMP_LOW
TEMP_HIGH
HUMIDITY_LOW
HUMIDITY_HIGH
LIGHT_LOW
LIGHT_HIGH
HOT_AND_BRIGHT
SENSOR_INVALID
UNKNOWN
```

### 7.3 WATERING

```text
NO_NEED
CHECK_SOIL
WATER_IF_DRY
UNKNOWN
```

该字段是建议，不是控制命令。

### 7.4 ADVICE

MVP 使用固定建议码：

```text
KEEP_CURRENT
MOVE_TO_SHADE
INCREASE_LIGHT
IMPROVE_VENTILATION
CHECK_SOIL
CHECK_SENSOR
OBSERVE_PLANT
```

示例：

```text
V1|AI_RESULT|42|WARN|HOT_AND_BRIGHT|CHECK_SOIL|MOVE_TO_SHADE
```

STM32 根据枚举更新 TFT、彩灯或蜂鸣器。完整英文解释只在电脑端显示和记录。

## 8. 错误帧

```text
V1|ERROR|REQ_ID|ERROR_CODE
```

无请求编号的连接级错误使用 `0`。

MVP 错误码：

```text
BAD_VERSION
BAD_FRAME
FRAME_TOO_LONG
BAD_VALUE
BUSY
SENSOR_INVALID
MODEL_TIMEOUT
MODEL_ERROR
MODEL_BAD_OUTPUT
GATEWAY_NOT_READY
```

示例：

```text
V1|ERROR|42|MODEL_TIMEOUT
```

收到错误后 STM32 结束当前 AI 请求并显示失败，不自动启动任何执行器。

## 9. 调试消息

以下消息仅用于联调，可在正式演示中保留：

```text
V1|ECHO|text
V1|PONG
```

现有 `ON/OFF` 彩灯命令属于 WiFi 独立实验，不属于农业系统正式协议。农业系统状态灯由本地 `app_state` 决定，不接受模型任意控制。

## 10. 接收状态机要求

固件协议层建议状态：

```text
RX_COLLECTING
RX_FRAME_READY
RX_DISCARD_OVERSIZE
```

逐字节处理：

1. 普通字符追加到固定缓冲；
2. 收到换行后去除可选回车，形成一帧；
3. 超过上限后进入丢弃状态，直到下一个换行；
4. 完整帧交给协议解析器；
5. 协议解析器只生成事件，不直接绘屏或控制水泵。

USART3 中断只负责保存字节和更新轻量状态，不调用模型业务、TFT、延时或 `CIPSEND` 等待。

## 11. 发送队列要求

- 至少支持一条正在发送和一条等待发送；
- AI 结果/错误优先于心跳；
- `CIPSEND` 等待 `>` 和 `SEND OK` 均有超时；
- 发送失败时产生事件，由连接状态机决定重试；
- 不在中断中忙等；
- 连接关闭时清空属于旧连接的待发帧。

## 12. 模型安全边界

协议中明确禁止：

```text
PUMP_ON
PUMP_SECONDS
SET_PWM
WRITE_GPIO
AT_COMMAND
```

若模型或网关产生未知字段，STM32 必须忽略。水泵只响应本地按键服务产生的事件。

## 13. 兼容和扩展

- 增加字段时优先新增消息类型，不改变现有字段顺序；
- 不兼容修改升级为 `V2`；
- 后续可增加 `SOIL`、植物类型和设备编号；
- 后续若引入手机 App 或云端，只替换网关传输实现，不修改传感器和水泵业务模块。
