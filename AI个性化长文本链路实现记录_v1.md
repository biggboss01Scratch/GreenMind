# GreenMind AI 个性化长文本链路实现记录 v1

## 1. 实现目标

把电脑网关中 AI 生成的自然语言真正传到 STM32，并在 TFT 上用接近
Galgame 底部文字框的形式展示。文案需结合：

- 当前植物及其数据库档案；
- 实时温度、空气湿度和相对光照；
- 确定性规则得出的环境适配结论；
- 一条与该植物相关的趣味养护知识；
- 一条安全、可操作但不直接控制硬件的建议。

本功能不替换原有 `STATUS / ISSUE / WATERING / ADVICE` 安全枚举。模型文本是
展示层，确定性规则仍是业务结论，模型不能启动水泵。

## 2. 最终链路

```text
STM32 上传植物身份和传感器值
  → Python 查询 SQLite 植物档案
  → 确定性规则计算安全枚举
  → Mock 或 DeepSeek 生成个性化中文台词
  → UTF-8 字节限制与 GB2312 可显示字符清洗
  → AI_TEXT_BEGIN / CHUNK / END 分包
  → STM32 固定缓冲区接收
  → 请求号、顺序、长度、UTF-8、CRC32 校验
  → AI_RESULT 安全枚举完成请求
  → TFT 显示植物头像和“AI养护手记”
```

## 3. 文案生成

### 3.1 DeepSeek

DeepSeek 提示词要求模型：

- 保持后端计算的四个安全枚举不变；
- 生成 60～85 个常用简体中文字符；
- 提到当前传感器读数及其是否适合该植物；
- 加入一条真实的植物特性或养护趣味知识；
- 给出一条安全建议；
- 不使用 Emoji、竖线或换行；
- 不把空气湿度误认为土壤湿度；
- 不控制水泵或其他执行器。

模型输出仍为 JSON，并新增 `dialog_zh` 字段。若模型缺少该字段、文本过短、
包含不可显示字符或试图修改安全枚举，网关会使用确定性中文文案回退。

### 3.2 Mock

Mock 不调用 API，但会根据植物、环境和规则结果生成结构相同的中文台词。
六种植物分别配置了趣味知识，例如绿萝气生根、薄荷叶片腺体、仙人掌肉质茎、
蝴蝶兰气生根等，因此适合在无网络或无余额时演示完整下发链路。

### 3.3 长度与字符

- 最大正文：768 个 UTF-8 字节；
- 通常可容纳约 200～250 个中文字符；
- 超长文本按完整 UTF-8 字符边界截断，不会整段丢弃；
- 网关过滤控制字符，并把非 GB2312 字符替换为 `?`；
- 当前完整 GB2312 12×12 字库足以显示清洗后的正文。

## 4. 分包协议

```text
V1|AI_TEXT_BEGIN|REQ_ID|BYTE_SIZE|CHUNK_COUNT|CRC32
V1|AI_TEXT_CHUNK|REQ_ID|SEQUENCE|HEX_PAYLOAD
V1|AI_TEXT_END|REQ_ID|CHUNK_COUNT|CRC32
V1|AI_TEXT_ERROR|REQ_ID|ERROR_CODE
```

每个 `AI_TEXT_CHUNK` 最多携带 32 个原始字节，转成 64 个 ASCII 十六进制
字符，保证整个协议帧不超过 120 字节。网关默认每帧间隔 80 ms，降低 ESP07
和 STM32 接收队列被瞬间塞满的风险。

发送顺序为：

```text
ACK
AI_STAGE PREPARING
AI_STAGE THINKING
AI_STAGE VALIDATING
AI_TEXT_BEGIN / CHUNK / END
AI_RESULT
```

STM32 只有同时收到匹配请求编号的完整正文和 `AI_RESULT`，才显示长文本卡片。
正文失败时仍处理 `AI_RESULT`，显示原有枚举建议。

## 5. STM32 实现

`ai_text_service.c` 使用固定 769 字节缓冲区，不使用堆内存；ESP07 TCP 字节流
环形缓冲区由 256 字节扩大为 1024 字节，主循环每轮最多取走 512 字节。接收状态包括：

```text
IDLE → REQUESTING → RECEIVING → READY
                         ↘ ERROR
```

校验项：

- 当前请求编号；
- 包序号必须从 0 连续递增；
- 单包十六进制格式；
- 预计字节数和包数；
- 禁止正文内嵌 NUL；
- UTF-8 必须是合法的 1～3 字节序列；
- 开始帧、结束帧和本地计算的 CRC32 必须一致；
- 接收超时为 8 秒，等待模型阶段沿用 45 秒 AI 超时。

异常只更新 `ai_dialog_error` 和回退状态，不覆盖安全枚举结果。

## 6. TFT 展示

AI 请求完成且正文校验成功时，原枚举详情区切换为：

```text
┌──────────────────────────┐
│ [植物像素头像]  植物名    │
│                 AI养护手记 │
├──────────────────────────┤
│ AI 生成的中文台词……       │
│ 自动按中英文字符宽度换行   │
│ 一屏显示 6 行，可上下滑动  │
│                      ▐滚动条│
└──────────────────────────┘
```

正文使用 12×12 字库绘制，中文按 12 像素、ASCII 按 6 像素计宽自动换行。
在对话框内上滑查看后文、下滑返回前文，每次移动两个完整文本行；右侧滚动条
同步显示当前位置。新 AI 文本到达时自动回到首行。局部重绘范围仅限 AI
详情卡片，不执行整屏刷新。

## 7. 数据库

`ai_analysis_logs` 新增：

```sql
dialog_zh TEXT
```

数据库模式版本由 1 升为 2。启动网关时会检查旧数据库；若缺少该列，会执行
向后兼容的 `ALTER TABLE`，不删除已有分析记录。

## 8. 主要改动文件

- `gateway/agri_gateway/providers.py`：个性化提示词、Mock 文案、清洗和回退；
- `gateway/agri_gateway/client.py`：长文本分包发送；
- `gateway/agri_gateway/database.py`：旧数据库迁移；
- `gateway/agri_gateway/plant_repository.py`：保存 `dialog_zh`；
- `gateway/database/schema.sql`：新增日志字段；
- `firmware/APP/agriculture/ai_text_service.c/.h`：固定缓冲接收与校验；
- `firmware/APP/agriculture/protocol.c/.h`：解析 `AI_TEXT_*`；
- `firmware/APP/agriculture/font_service.c/.h`：UTF-8 自动换行；
- `firmware/APP/agriculture/ui_service.c`：AI 对话框；
- `firmware/User/main.c`：请求生命周期、异常和 UI 状态接入。

## 9. 软件验证结果

完成日期：2026-07-18。

- Python 自动化测试：38 项通过（包含后续加入的 TCP 心跳和在线植物资源测试）；
- 测试会重组所有 `AI_TEXT_CHUNK`，核对长度、序号、UTF-8 和 CRC32；
- 额外验证超过 768 字节的正文会在 UTF-8 边界截断并正常传输，而非整段丢弃；
- 中文资产：147204 字节字库、7445 条映射、134 条 UI 文案通过校验；
- SQLite 当前库：模式版本 2，`dialog_zh` 列存在；
- ARM Compiler 5 固件完整重建：0 error、7 个原 TFT 驱动未使用变量 warning；
- 实际调用 DeepSeek 成功生成 254 字节蝴蝶兰中文台词，安全枚举与后端一致。

真实 DeepSeek 示例包含当前 29℃、62% 空气湿度、48% 光照，指出蝴蝶兰温度
略高、湿度和光照合适，并加入气生根与浇水前检查水苔的知识。说明个性化字段
和植物档案已实际进入模型输出，而非固定一句建议。

## 10. 启动与验收

Mock：

```cmd
cd /d "D:\2026SecondHalf\Puzhong\普中开发板\GreenMind\gateway"
python run_gateway.py <STM32屏幕显示的STA-IP> --provider mock
```

DeepSeek：

```cmd
python run_gateway.py <STM32屏幕显示的STA-IP> --provider deepseek
```

预期网关日志：

```text
[DIALOG] <中文台词>
[TX] V1|AI_TEXT_BEGIN|...
[TX] V1|AI_TEXT_END|...
[DIALOG] sent request=... bytes=... chunks=... crc=...
[TX] V1|AI_RESULT|...
```

预期 STM32 串口日志：

```text
[AI TEXT] BEGIN request=... bytes=... chunks=...
[AI TEXT] READY request=... bytes=...
[AI] RESULT status=... issue=... water=... advice=...
```

预期 TFT：AI 状态进入“分析完成”，详情区出现植物头像、“AI养护手记”和多行
中文台词。

## 11. 回退方法

仅关闭长文本发送、保留原枚举协议：

```cmd
python run_gateway.py <STA-IP> --provider mock --no-ai-dialog
```

固件仅关闭对话框显示：

```c
#define APP_AI_DIALOG_ENABLE 0
```

长文本缺失、超时、乱序、CRC 错误或 UTF-8 错误时，固件会自动显示原来的
`STATUS / ISSUE / WATERING / ADVICE`，不需要人工切换。

## 12. 尚未验证

当前没有硬件在手，因此以下必须在下次拿到开发板后验收：

- ESP07 实际连续接收最多 24 个文本分包是否稳定；
- TFT 6 行中文的边距、字号和可读性；
- 对话框上滑、下滑、两行步进和滚动条位置是否符合手感；
- 多次连续请求后是否出现残影或明显闪屏；
- 网关断线、正文中断和 CRC 错误时的实机回退观感；
- 六种植物分别调用 Mock/DeepSeek 后的显示效果。

这些属于实机显示与通信验收，不影响当前已完成的软件构建、协议重组和真实
模型输出验证结论。
