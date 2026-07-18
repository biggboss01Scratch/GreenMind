# TCP 心跳与 STM32 重启恢复记录 v1

## 1. 问题现象

Python 网关已经连接并进入 `GATEWAY READY` 后，如果只复位或重新烧录 STM32、但不重启 ESP07 和 Python 网关，新启动的 STM32 可能长期无法重新进入 `GATEWAY READY`。手动重启网关或给整板断电后可以恢复。

## 2. 原因

- STM32 复位会丢失内存中的 TCP 连接状态；
- ESP07 没有一起复位，原来的 TCP socket 可能仍然存在；
- Python 网关因此没有收到 socket 断开，也不会重新创建连接；
- ESP07 不会再次产生新的 `CONNECT` 通知；
- 网关等待 STM32 的 `HELLO`，STM32等待连接事件，形成互相等待。

STM32 的 USART3 接收代码原本已经支持通过新的 `+IPD` 数据重新识别现有 TCP link，因此不需要修改 Wi-Fi 配置或强制重启 ESP07。

## 3. 实现方案

修改范围仅为电脑端网关：

- 网关连接空闲时每 3 秒发送 `V1|PING`；
- STM32现有协议返回 `V1|PONG`；
- 网关将 `PONG` 识别为合法心跳，不回复、不记录普通日志；
- `PING` 经过 ESP07 形成 `+IPD`，可以让复位后的 STM32重新发现旧连接；
- STM32发现连接后发送 `HELLO`，网关回复 `HELLO_ACK`；
- 连续 12 秒没有收到任何 STM32 合法帧时，网关关闭旧连接；
- 关闭后继续使用原有的 3 秒自动重连循环。

心跳位于网关已有的单线程接收循环中。DeepSeek 调用、AI 长文本分片和植物图片分片执行期间不会并发插入心跳，因此不会破坏分片顺序。

## 4. 配置

默认配置：

```text
heartbeat interval = 3.0 s
heartbeat timeout  = 12.0 s
reconnect interval = 3.0 s
```

启动时可选覆盖：

```cmd
python run_gateway.py 192.168.137.30 --provider deepseek --heartbeat-interval 3 --heartbeat-timeout 12
```

通常不需要填写后两个选项，使用默认值即可。超时时间必须大于心跳周期。

## 5. 软件验证

新增测试：

1. 空闲连接到期后发送 `V1|PING`；
2. 收到 `V1|PONG` 后不发送错误帧；
3. 12 秒无设备响应时产生心跳超时并进入重连。

截至本记录，全套 Python 自动化测试结果：

```text
Ran 38 tests
OK
```

本次没有修改 STM32 固件，上一版 Keil 构建结果和内存占用不变。

## 6. 实机验收

1. 正常启动 STM32、ESP07 和 Python 网关；
2. 等待 TFT 显示 `GATEWAY READY`；
3. 保持 Python 网关运行，只按 STM32 复位键或重新烧录固件；
4. 等待 STM32完成 Wi-Fi 初始化；
5. 预期无需重启 Python，TFT 自动重新显示 `GATEWAY READY`；
6. 再调用一次 Mock AI，确认业务通信正常；
7. 断开 ESP07 电源或关闭热点，预期网关出现：

```text
[CONNECTION] offline: STM32 heartbeat timeout
[CONNECTION] retrying in 3.0s
```

因为当前没有硬件在手，第 3～7 步仍需实机验证。

## 7. 回退

如实机发现心跳与特定 ESP AT 固件不兼容，可回退以下电脑端文件：

- `gateway/agri_gateway/client.py`
- `gateway/agri_gateway/config.py`
- `gateway/run_gateway.py`

STM32 固件无需回退。
