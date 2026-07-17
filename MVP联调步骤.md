# AgricultureSys 最小 MVP 联调步骤

## 1. 本版范围

包含：DHT11、光敏、TFT、按键、ESP07、正式 TCP 协议、Python 网关、Mock 和 DeepSeek。

不包含：水泵、PWM、自动浇水、彩灯动画和蜂鸣器。

## 2. 下载固件

打开：

```text
AgricultureSys/firmware/Template.uvprojx
```

执行 `Rebuild` 和 `Download`，然后复位开发板。

电脑热点保持：

- 名称：`0Range777`
- 频段：2.4 GHz
- 密码使用现有本地配置

TFT 应先后显示 `ESP STARTING`、`SCANNING`、`CONNECTING`、`CONNECTED`，并给出 STA IP。

## 3. 先运行 Mock 网关

在 PowerShell 中执行，IP 替换为 TFT 的 STA IP：

```powershell
cd "D:\2026SecondHalf\Puzhong\普中开发板\AgricultureSys\gateway"
python .\run_gateway.py 192.168.137.23 --provider mock
```

连接成功后：

- 电脑显示 `TCP connected`、收到 STM32 `HELLO`、发送 `HELLO_ACK`；
- TFT 显示 `TCP: CONNECTED` 和 `GATEWAY: READY`；
- 实时页显示整数温度、空气湿度和 0～100 光照；
- KEY_UP 在实时页和 AI 页之间切换；
- KEY1 发起 AI 请求。

按下 KEY1 后应看到：

```text
SENDING
PREPARING
THINKING...
VALIDATING
DONE
```

Mock 模式不会消耗 API 额度。

## 4. 真实 DeepSeek 测试

停止 Mock 网关后，直接运行：

```powershell
python .\run_gateway.py 192.168.137.23
```

网关会从 `AgricultureSys/myDeepseekApiKey.md` 读取密钥，不会输出密钥内容。按 KEY1 后，电脑日志显示请求数据、`THINKING...`、耗时、校验枚举和完整英文建议；TFT 显示调用阶段及英文短建议码。

## 5. 异常测试

### 网关关闭

1. 在网关窗口按 `Ctrl+C`；
2. TFT 应回到 `GATEWAY: OFFLINE` 或 `TCP: WAITING`；
3. 温湿度和光照继续刷新；
4. KEY1 应显示 `GATEWAY OFFLINE`，不能卡死；
5. 重启网关后应重新握手并恢复 `GATEWAY: READY`。

### 电脑热点关闭

1. 关闭热点；
2. TFT 应显示 `DISCONNECTED`、`SCANNING` 或 `TARGET NOT FOUND`；
3. 本地传感器和页面切换继续运行；
4. 重新打开热点后 ESP07 应自动扫描并连接；
5. Python 网关会循环重连，STA IP 变化时需要使用新 IP 重启网关。

### DeepSeek 异常

电脑断网或 API 失败时：

- 网关记录 `MODEL_NETWORK_ERROR`、`API_AUTH_ERROR` 等错误码；
- TFT 显示 `AI: ERROR` 和短错误码；
- 45 秒无有效结果时 STM32 显示 `AI: TIMEOUT`；
- 再次按 KEY1 可以重新请求。

### 传感器异常

在断电状态下断开 DHT11 后重新上电：

- TFT 应显示 `DHT ERROR`；
- 光敏和网络仍工作；
- KEY1 不发送无效数据，并显示 `SENSOR INVALID`。

## 6. 串口调试信息

可选使用 USART1 串口，参数为 `115200 / 8N1`。日志包括：

```text
[WIFI]
[TCP]
[GATEWAY]
[AI]
[PROTOCOL]
[STATUS]
```

每约 2 秒输出一次状态快照，方便判断问题发生在传感器、WiFi、TCP、网关还是模型层。

## 7. 通过标准

- Mock 与真实 DeepSeek 都能完成一次 KEY1 请求闭环；
- `THINKING...` 在模型等待期间可见；
- TFT 和电脑日志显示一致的请求编号和结果；
- 关闭网关或热点后系统不死机；
- 恢复连接后无需重新烧录即可再次请求；
- 全程没有水泵或 PWM 输出。
