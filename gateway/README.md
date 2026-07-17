# AgricultureSys Python Gateway

电脑网关连接 ESP07 的 STA IP:8080，接收 STM32 传感器数据，调用 DeepSeek，并将校验后的短枚举返回 STM32。

仅使用 Python 标准库，不需要安装第三方包。

## 启动

在 `AgricultureSys/gateway` 目录运行，IP 替换为 TFT 显示的 STA IP：

```powershell
python .\run_gateway.py 192.168.137.23
```

默认读取上级目录的 `myDeepseekApiKey.md`，调用 `deepseek-v4-flash`。密钥不会输出到日志。

不消耗 API 额度的 Mock 模式：

```powershell
python .\run_gateway.py 192.168.137.23 --provider mock
```

停止网关使用 `Ctrl+C`。

## 模型调用过程

STM32 发出 AI 请求后，电脑日志和 TFT 会显示：

```text
PREPARING
THINKING...
VALIDATING
DONE / ERROR / TIMEOUT
```

电脑端额外显示完整英文建议；STM32 只接收经过白名单校验的英文状态、问题、浇水建议和建议码。

## 单元测试

```powershell
python -m unittest discover -s .\tests -v
```

## 常见错误

- `API_KEY_MISSING`：找不到密钥文件，也没有 `DEEPSEEK_API_KEY` 环境变量。
- `API_AUTH_ERROR`：密钥无效。
- `API_BALANCE_ERROR`：余额不足。
- `API_RATE_LIMIT`：请求过快。
- `MODEL_NETWORK_ERROR`：电脑无法访问 DeepSeek API。
- `MODEL_BAD_OUTPUT`：模型 JSON 未通过白名单校验。
- `Connection refused`：固件未运行 TCP Server，或 STA IP/端口错误。
