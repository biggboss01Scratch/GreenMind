# GreenMind 当前阶段总结（供后续讨论）

更新时间：2026-07-17

## 1. 文档用途

本文档用于把当前已经落地的代码、实物验证结果和仍未完成的部分交接给 ChatGPT 或其他协作者。

重要原则：早期需求和 ChatGPT 建议仅作参考，后续方案必须以本工程的实际代码、开发板引脚和实物现象为准，不要假设尚未实现的功能已经存在。

## 2. 当前结论

当前已经完成并实物验证了智慧农业最小 MVP 的核心闭环：

```text
DHT11温湿度 + 板载光敏
          ↓
        STM32
          ↓ USART3
        ESP07
          ↓ WiFi / TCP
Windows电脑热点 + Python网关
          ↓ HTTPS
     DeepSeek API
          ↓
结构化英文状态和建议码
          ↓
     STM32 TFT显示
```

真实联调已经出现以下结果：

```text
[RX] V1|AI_REQ|2|26|65|62|NORMAL
[MODEL] THINKING... request=2 T=26 H=65 L=62/NORMAL
[MODEL] VALIDATED request=2 elapsed=1.05s
[ADVICE] Conditions are good. Check soil moisture before watering.
[TX] V1|AI_RESULT|2|NORMAL|NONE|CHECK_SOIL|KEEP_CURRENT
```

TFT 最终显示 `AI: DONE`。这证明传感器、STM32、ESP07、电脑网关、DeepSeek 和返回显示链路已经闭环。

这里的“核心 MVP 闭环完成”不等于整个课程大实验已经完成；水泵、安全控制、完整异常验收和最终交互仍待实现。

## 3. 实际系统角色

- Windows 电脑开启 2.4 GHz 热点，SSID 为 `0Range777`，电脑是 WiFi AP。
- ESP07 作为 STA 主动连接电脑热点。
- ESP07 同时保留自身 AP，名称为 `ESP07`，即当前采用 AP+STA 模式。
- ESP07/STM32 侧开启 TCP Server，端口为 `8080`。
- Windows 上的 Python 网关是 TCP Client，连接 TFT 显示的 ESP07 STA IP。
- Python 网关负责访问 DeepSeek HTTPS API；STM32 不直接处理 HTTPS、证书或大模型 JSON。
- STA IP 由电脑热点动态分配，例如本次实测为 `192.168.137.171`，下次可能变化。

Python 网关不是电脑热点。ESP07 可以先连上热点；只有启动 Python 网关后，TFT 才会从 `TCP: WAITING / GATEWAY: OFFLINE` 变成 `TCP: CONNECTED / GATEWAY: READY`。

## 4. 当前硬件和引脚

| 模块 | 当前实际引脚/接口 | 状态 |
|---|---|---|
| TFT 3.5寸屏 | FSMC，背光 PB0 | 已显示传感器、网络和 AI 状态 |
| DHT11 | PG11 | 已接入综合工程 |
| 板载光敏 | PF8 / ADC3_CH6 | 已接入，输出 0～100 |
| ESP07 | USART3：PB10 TX、PB11 RX | 已完成 WiFi/TCP/AI 实物闭环 |
| ESP07使能/复位 | PA4、PA15 | 当前仅置高；完整硬件复位和自动恢复仍待完善 |
| KEY_UP | PA0 | 页面切换 |
| KEY1 | PE3 | 发起 AI 请求 |
| WS2812彩灯 | PE5 | 综合 MVP 当前未初始化 |
| 水泵/直流电机预留 | PB5 / TIM3_CH2 | 当前未接入、未初始化 |

开发板连接注意事项：

- ESP07 插在 P3，VCC 必须对准板上 `3.3V`，不能插反或接 5V。
- P10 两颗跳帽负责 WiFi USART3：`PB10 → WiFi RX`，`PB11 ← WiFi TX`。
- 蓝牙模块也使用 USART3/PB10/PB11。蓝牙连接时会与 ESP07 总线冲突，曾导致 `TIMEOUT:AT`；当前运行 WiFi 时必须断开蓝牙。若以后要求 WiFi 和蓝牙同时工作，必须给蓝牙迁移串口，不能并接两个 TX。

## 5. 当前工程位置

```text
GreenMind/
├─ firmware/                       STM32 Keil工程
│  ├─ User/main.c                  主循环、WiFi状态机、TCP和AI调度
│  ├─ APP/agriculture/             状态、协议、传感器、按键、UI模块
│  └─ Obj/GreenMind.hex             最近一次编译固件
├─ gateway/
│  ├─ run_gateway.py               Python网关入口
│  ├─ agri_gateway/                TCP、协议和DeepSeek provider
│  └─ tests/                       协议和套接字测试
├─ MVP需求规格说明书_v1.md
├─ TCP通信协议_v1.md
├─ MVP联调步骤.md
└─ 开发与验证记录.md
```

`wifi_tft` 是此前验证成功的 WiFi 基线工程，应保持不动，作为回退和对照。综合功能只在 `GreenMind` 中继续开发。

API Key 位于本地 `GreenMind/myDeepseekApiKey.md`，热点密码位于忽略提交的 `firmware/APP/agriculture/app_secrets.h`。两者均不得复制进讨论文档、日志或公开仓库。

## 6. 当前固件功能

- 启动 ESP07 AP+STA 模式并开启 TCP 8080 服务器。
- 持续搜索并连接电脑热点 `0Range777`。
- TFT 传感器页显示温度、湿度、光照、WiFi、STA IP、TCP 和网关状态。
- TFT AI 页显示请求编号、调用阶段、结构化状态、问题、浇水建议码、动作建议码和错误信息。
- KEY_UP 切换页面，KEY1 发起 AI 请求。
- 传感器无效或网关离线时拒绝发起 AI 请求。
- AI 等待超时为 45 秒。
- TCP 使用 ASCII + CRLF 分帧，能够处理拆包、粘包和连续消息。
- `AI_STAGE` 包括 `PREPARING`、`THINKING`、`VALIDATING`。
- `AI_RESULT` 使用白名单英文枚举，不把任意模型文本直接交给 STM32。
- 当前未初始化水泵 PWM，也未让模型控制任何执行器。

## 7. 当前 Python/DeepSeek 网关

默认运行真实 DeepSeek：

```powershell
cd "D:\2026SecondHalf\Puzhong\普中开发板\GreenMind\gateway"
python .\run_gateway.py 192.168.137.171
```

其中 IP 必须换成 TFT 当前显示的 STA IP。

不调用 API 的 Mock 测试：

```powershell
python .\run_gateway.py 192.168.137.171 --provider mock
```

不要把 `<屏幕显示的STA-IP>` 原样输入 Windows CMD，因为尖括号会被解释为重定向符号。

当前网关：

- 默认 provider 为 `deepseek`。
- 默认模型为 `deepseek-v4-flash`。
- 每次新的 `AI_REQ` 都会重新发起 API 请求，没有结果缓存。
- 使用 JSON 结构化输出并对白名单枚举进行二次校验。
- 完整建议暂时统一为不超过 160 字符的 ASCII 英文 `suggestion_en`。
- `thinking` 设置为 disabled，以降低延迟和费用；TFT 的 `THINKING` 表示“模型调用进行中”，不是展示模型内部推理过程。
- 明确限制：空气湿度不等于土壤湿度，涉及浇水只能建议检查土壤；模型不得控制水泵或其他硬件。
- 网关断线后每 3 秒自动尝试重新建立 TCP 连接。

## 8. 已发现并修复的问题

### 8.1 ESP初始化错误过于笼统

原界面只显示 `MODULE ERROR`，现在已经细化为：

- `TIMEOUT:AT`
- `ERROR:CWMODE`
- `ERROR:CWSAP`
- `ERROR:CIPAP`
- `ERROR:CIPMUX`
- `ERROR:CIPSERVER`

并同步输出 USART1 日志。

### 8.2 蓝牙与WiFi串口冲突

实测蓝牙接着时出现 `TIMEOUT:AT`；断开蓝牙后 ESP07 正常连接。根因是两者共用 USART3/PB10/PB11。

### 8.3 AI停留在VALIDATING后超时

Mock 网关会在同一秒连续发送 5 条消息，原 STM32 协议环形队列实际只能保存 3 条，最终 `AI_RESULT` 可能被静默丢弃。

现在队列为 8 个槽位、实际容量 7 条，并增加 `EVENT OVERFLOW` 检测。修复后已经实物确认进入 `AI: DONE`。

### 8.4 TFT状态变化时整屏闪烁

原 UI 每次 `ui_dirty` 都执行 `LCD_Clear()` 并重画整个 320×480 页面。现在已经改为：

- 首次显示和页面切换时整页绘制；
- 同一页面内仅更新发生变化的字段矩形；
- 温湿度、光照、WiFi、TCP、网关和 AI 状态分别局部更新。

代码已编译通过；仍需继续观察实物上的局部刷新观感。中文 UI 暂缓。

### 8.5 AI建议语言

模型提示词、Mock 建议、完整建议和电脑日志已统一为英文。TFT 继续显示英文枚举。STM32 固件协议没有因此改变。

## 9. 当前明确未完成的部分

1. 水泵尚未接线，PB5/TIM3_CH2 未初始化。
2. 没有土壤湿度传感器，因此系统不能判断土壤是否缺水。
3. AI 只能提出 `CHECK_SOIL`、`NO_NEED` 等建议，不能直接启动水泵。
4. ESP07 在最初 `AT` 初始化失败后，目前不会自动硬件复位并重新初始化；这是正式版本前需要补的可靠性缺口。
5. Python 网关需要用户手动输入 TFT 上的动态 STA IP，尚未实现自动发现。
6. 尚未完成热点关闭/恢复、网关退出/恢复、ESP重启、传感器断开等系统化异常验收。
7. 中文显示暂缓；当前不要把任意中文直接发送给 TFT。
8. 彩灯和蜂鸣器尚未加入综合工程。
9. 尚未开发手机 App；当前电脑 Python 网关已经承担 STM32 与大模型之间的中间站角色。

## 10. 建议的下一阶段顺序

### 第一优先：稳定性验收

- 连续执行 10 次 Mock 请求，确认均进入 `AI: DONE`。
- 执行 2～3 次真实 DeepSeek 请求。
- 停止并重启 Python 网关，验证自动重连。
- 关闭再开启电脑热点，验证 STA 恢复。
- 整板断电重启，验证从 ESP 初始化恢复到 `GATEWAY: READY`。
- 验证 TFT 局部刷新不再整屏闪烁。
- 模拟传感器失败，确认网络、按键和界面不被阻塞。

### 第二优先：网络可靠性

- ESP07 上电硬件复位。
- `AT` 探测和初始化自动重试。
- TCP/网关心跳和更明确的断线状态。
- 评估自动发现 STA IP，减少手动输入。

### 第三优先：水泵安全子系统

水泵准备好后，先独立实现手动安全控制，再考虑和建议流程结合：

- 水泵必须经过电机驱动或 MOSFET，不能直接由 STM32 GPIO 供电。
- 水泵使用合适的外部电源，并与开发板共地。
- KEY 手动启动，单次最多运行约 3 秒后自动关闭。
- 保留独立紧急停止。
- AI 结果只提示用户，由用户按键确认后才能启动；不得让模型直接控制水泵。

### 第四优先：交互完善

- 彩灯显示正常、警告、故障和模型调用状态。
- 蜂鸣器提示严重异常。
- 最后再评估固定中文字模或其他中文显示方式。

## 11. 给后续讨论的约束

后续建议请遵守：

1. 不要推翻已经实物验证的 WiFi/TCP/网关结构，除非有明确收益和回退方案。
2. 不要修改 `wifi_tft` 基线工程；只修改 `GreenMind`。
3. STM32 使用 Keil ARMCC 5.06，现有固件为 C 工程，应保持兼容。
4. 不要让 STM32 直接承担 HTTPS、证书和复杂大模型 JSON。
5. 不要让 AI 直接控制水泵。
6. 没有土壤湿度数据时，不得根据空气湿度直接决定浇水。
7. 所有网络、模型和传感器失败都必须有可见状态，并且不能影响紧急停止等本地安全功能。
8. 新功能应模块化，保留可测试、可回退和后续扩展空间。

## 12. 下一次讨论可以聚焦的问题

- 是否先完成网络异常恢复，还是先准备水泵驱动和接线？
- 课程大实验最终必须综合哪些指定模块？
- 是否增加土壤湿度传感器，使浇水建议有直接依据？
- 水泵确认按键、运行时长和紧急停止如何映射？
- 是否需要自动发现 ESP07 STA IP？
- 最终演示脚本和验收指标如何设计？

