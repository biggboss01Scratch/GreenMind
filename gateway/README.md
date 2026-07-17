# GreenMind Python Gateway

电脑网关连接 ESP07 的 STA IP:8080，接收 STM32 传感器数据，调用 DeepSeek，并将校验后的短枚举返回 STM32。

仅使用 Python 标准库，不需要安装第三方包。

## 启动

在 `GreenMind/gateway` 目录运行，IP 替换为 TFT 显示的 STA IP：

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

## 植物数据库和像素资源

生成 6 种植物、3 种状态的 18 张 48×48 像素图片：

```powershell
python .\tools\generate_plant_assets.py
```

图片同时输出为 PNG 预览和大端字节序的原始 `RGB565_BE`。资源清单位于
`assets/plants/manifest.json`，其中包含字节数、CRC32 和 SHA-256。

创建或重建本地 SQLite：

```powershell
python .\tools\init_database.py --reset
```

默认数据库位置为 `data/greenmind.sqlite3`。运行库属于本地生成文件，不提交
Git；`database/schema.sql`、`database/seed_plants.json`、资源文件和初始化脚本
会提交，因此任何电脑都可以得到一致的数据库。

当前数据库层已经可以：

- 查询全部、内置或在线植物；
- 查询温度、空气湿度和 0～100 相对光照偏好；
- 查询每种植物的三种像素状态图；
- 保存设备当前植物；
- 保存带植物身份的 AI 分析记录。

当前固件使用默认设备 `GM001` 和默认植物 `pothos` 发送：

```text
V1|PLANT_AI_REQ|REQ_ID|GM001|pothos|T|H|LIGHT|LIGHT_LEVEL
```

网关查询 SQLite 档案，先用确定性规则计算温度、空气湿度和相对光照适配性，
再把植物档案和规则结果交给 Mock 或 DeepSeek。模型只能解释结果，不能覆盖
规则状态码。旧的 `V1|AI_REQ|...` 仍按 `GM001/pothos` 兼容处理。

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
