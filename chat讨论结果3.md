# 智慧植物养护系统

## 植物图鉴、在线选择与个性化 AI 分析需求方案

## 1. 项目概述

### 1.1 模块名称

植物图鉴与在线植物资源模块。

### 1.2 建设目标

本模块在现有智慧植物养护系统基础上增加植物图鉴、植物选择、在线资源获取和植物个性化 AI 分析功能。

用户可以在 STM32 的 TFT 屏幕上：

1. 查看当前植物；
2. 进入植物选择页面；
3. 浏览内置植物和在线植物；
4. 查看植物环境偏好；
5. 选择当前植物；
6. 联网下载当前植物的像素图片；
7. 使用当前植物身份发起 AI 分析。

系统根据植物唯一标识查询数据库，获得该植物的完整环境偏好和养护信息，并将这些信息与 STM32 采集的温度、空气湿度和光照数据共同提交给大模型。

### 1.3 核心设计原则

1. 一个 STM32 终端代表一个真实环境监测位置。
2. 一个 STM32 终端同一时刻只选择一种当前植物。
3. STM32 不保存完整植物数据库。
4. STM32 至少保存当前植物的唯一标识和显示名称。
5. 植物完整知识保存在电脑端数据库中。
6. 在线植物图片在使用时通过 Wi-Fi 下载到 STM32。
7. 第一版图片只保存在 RAM 中，设备重启后重新下载。
8. AI 请求只上传植物唯一标识和环境数据。
9. 电脑网关根据植物标识查询完整档案，再调用大模型。
10. 大模型只提供分析和建议，不直接控制水泵。

---

# 2. 当前系统基础

现有系统已经完成以下通信闭环：

```text
温湿度传感器 + 光敏传感器
              ↓
            STM32
              ↓ USART3
            ESP07
              ↓ Wi-Fi / TCP
         Windows电脑网关
              ↓ HTTPS
          DeepSeek API
              ↓
       结构化分析结果
              ↓
          STM32 TFT
```

新增植物图鉴模块应在该结构上扩展，不要求 STM32 直接访问数据库、处理 HTTPS 或解析复杂模型 JSON。

---

# 3. 系统总体架构

```text
┌─────────────────────────────┐
│        植物与现实环境         │
└──────────────┬──────────────┘
               │
        温度、湿度、光照
               │
               ▼
┌─────────────────────────────┐
│ STM32植物养护终端            │
│                             │
│ · 采集环境数据               │
│ · 显示当前植物               │
│ · 浏览植物列表               │
│ · 下载当前植物图片           │
│ · 发起AI请求                 │
│ · 显示精简建议               │
└──────────────┬──────────────┘
               │ USART3
               ▼
┌─────────────────────────────┐
│ ESP07网络模块                │
│ · 连接电脑热点               │
│ · TCP数据收发                │
└──────────────┬──────────────┘
               │
               ▼
┌─────────────────────────────┐
│ Windows Python网关           │
│                             │
│ · 查询植物数据库             │
│ · 返回精简植物列表           │
│ · 提供植物详情               │
│ · 转换并发送像素图片         │
│ · 查询植物完整知识           │
│ · 调用DeepSeek              │
│ · 校验AI结果                 │
└──────────┬─────────┬────────┘
           │         │
           ▼         ▼
┌────────────────┐ ┌────────────────┐
│ 植物数据库      │ │ 大模型服务      │
│ · 基本信息      │ │ · 个性化分析    │
│ · 环境偏好      │ │ · 养护建议      │
│ · 图片资源      │ └────────────────┘
└────────────────┘
```

---

# 4. 各端功能边界

## 4.1 STM32 终端

STM32 负责：

* 读取温度、空气湿度和光照；
* 显示当前植物；
* 请求植物列表；
* 浏览和选择植物；
* 请求植物详情；
* 请求并接收当前植物图片；
* 将图片暂存在 RAM；
* 发起植物个性化 AI 请求；
* 显示 AI 精简建议；
* 保存当前植物唯一标识和显示名称。

STM32 不负责：

* 查询正式数据库；
* 保存完整植物知识；
* 保存大量植物图片；
* 直接调用大模型 API；
* 管理用户账户；
* 判断真实土壤湿度；
* 让 AI 直接控制水泵。

## 4.2 ESP07

ESP07 负责：

* 连接 Windows 电脑热点；
* 维持 TCP 通信；
* 转发 STM32 和电脑网关之间的数据。

ESP07 不负责植物业务、数据库查询或图片解析。

## 4.3 电脑网关

电脑网关负责：

* 接收 STM32 植物列表请求；
* 查询植物数据库；
* 将数据库记录转换为 STM32 可处理的精简协议；
* 读取并转换像素图片；
* 分包发送图片数据；
* 根据植物唯一标识获取完整植物档案；
* 构造大模型输入；
* 校验模型输出；
* 返回 STM32 可识别的白名单结果。

## 4.4 数据库

数据库负责：

* 保存植物种类；
* 保存植物环境偏好；
* 保存植物简介和风险信息；
* 保存图片资源路径；
* 保存植物资源版本；
* 保存设备当前选择的植物；
* 可选地保存设备使用过的植物记录。

## 4.5 大模型

大模型负责：

* 比较植物偏好与当前环境；
* 分析温度、空气湿度和光照适配性；
* 识别主要风险；
* 生成植物特定的养护建议；
* 解释建议原因。

大模型不负责：

* 直接启动水泵；
* 判断土壤一定缺水；
* 判断植物已经患病；
* 返回未经限制的硬件指令。

---

# 5. 植物身份设计

## 5.1 唯一标识

植物名称不得直接作为数据库主键。

每种植物应使用稳定的 ASCII 唯一标识：

```text
pothos
mint
succulent
cactus
orchid
tomato
```

名称仅用于显示：

```text
species_id: pothos
display_name: POTHOS
name_zh: 绿萝
name_en: Pothos
```

使用独立唯一标识的原因：

* 避免中文编码问题；
* 支持中英文名称；
* 支持别名；
* 名称修改后不影响历史数据；
* STM32 协议更短；
* 数据库查询更加稳定。

## 5.2 STM32 本地保存内容

STM32 至少保存：

```c
typedef struct {
    char species_id[16];
    char display_name[16];
    uint8_t source_type;
    uint16_t image_width;
    uint16_t image_height;
    uint8_t image_loaded;
} CurrentPlant;
```

字段说明：

| 字段           | 说明        |
| ------------ | --------- |
| species_id   | 植物唯一标识    |
| display_name | TFT显示名称   |
| source_type  | 内置或在线     |
| image_width  | 图片宽度      |
| image_height | 图片高度      |
| image_loaded | 当前图片是否已加载 |

STM32 不保存完整温湿度偏好、长文本简介和大模型提示词。

---

# 6. 植物数据库需求

## 6.1 植物种类表

表名：

```text
plant_species
```

建议字段：

| 字段              | 类型      | 说明               |
| --------------- | ------- | ---------------- |
| species_id      | 字符串，主键  | 植物唯一标识           |
| name_zh         | 字符串     | 中文名              |
| name_en         | 字符串     | 英文名              |
| display_name    | 字符串     | STM32显示短名        |
| scientific_name | 字符串     | 学名               |
| aliases         | JSON/文本 | 别名               |
| category        | 字符串     | 植物分类             |
| description     | 文本      | 植物简介             |
| difficulty      | 字符串     | 养护难度             |
| source_type     | 字符串     | builtin 或 online |
| enabled         | 布尔值     | 是否允许展示           |
| data_version    | 整数      | 数据版本             |
| created_at      | 时间      | 创建时间             |
| updated_at      | 时间      | 更新时间             |

## 6.2 植物环境需求表

表名：

```text
plant_requirements
```

建议字段：

| 字段               | 类型      | 说明       |
| ---------------- | ------- | -------- |
| requirement_id   | 主键      | 记录编号     |
| species_id       | 外键      | 植物唯一标识   |
| temp_min_c       | 数值      | 推荐最低温度   |
| temp_max_c       | 数值      | 推荐最高温度   |
| humidity_min     | 数值      | 推荐最低空气湿度 |
| humidity_max     | 数值      | 推荐最高空气湿度 |
| light_min        | 数值      | 推荐最低相对光照 |
| light_max        | 数值      | 推荐最高相对光照 |
| light_preference | 字符串     | 光照偏好     |
| watering_style   | 字符串     | 浇水原则     |
| ventilation_need | 字符串     | 通风需求     |
| care_summary     | 文本      | 简短养护说明   |
| risk_notes       | JSON/文本 | 常见风险     |

当前光敏传感器输出归一化为 0～100，因此植物光照偏好也暂时使用 0～100 的相对范围。

例如：

```text
绿萝：30～60
多肉：55～90
仙人掌：65～100
兰花：30～55
```

这些值是系统相对标尺，不应描述为实际照度 lux。

## 6.3 图片资源表

表名：

```text
plant_images
```

建议字段：

| 字段            | 类型  | 说明                 |
| ------------- | --- | ------------------ |
| image_id      | 主键  | 图片编号               |
| species_id    | 外键  | 植物唯一标识             |
| image_type    | 字符串 | pixel、thumbnail等   |
| image_path    | 字符串 | 图片文件路径             |
| image_format  | 字符串 | RGB565、RGB565_RLE等 |
| width         | 整数  | 图片宽度               |
| height        | 整数  | 图片高度               |
| byte_size     | 整数  | 转换后数据大小            |
| checksum      | 字符串 | 完整性校验值             |
| image_version | 整数  | 图片版本               |
| enabled       | 布尔值 | 是否可下载              |

第一版推荐：

```text
image_type: pixel
image_format: RGB565
width: 48
height: 48
```

## 6.4 设备表

表名：

```text
devices
```

建议字段：

| 字段                 | 类型     | 说明     |
| ------------------ | ------ | ------ |
| device_id          | 字符串，主键 | 设备编号   |
| device_name        | 字符串    | 设备名称   |
| current_species_id | 外键     | 当前植物   |
| last_seen_at       | 时间     | 最后在线时间 |
| firmware_version   | 字符串    | 固件版本   |
| online_status      | 布尔值    | 是否在线   |

## 6.5 设备植物使用记录

表名：

```text
device_plant_history
```

该表不是必须的第一版功能，但可记录设备使用过哪些植物。

| 字段               | 类型  | 说明            |
| ---------------- | --- | ------------- |
| history_id       | 主键  | 记录编号          |
| device_id        | 外键  | 设备编号          |
| species_id       | 外键  | 植物编号          |
| selected_at      | 时间  | 选择时间          |
| unselected_at    | 时间  | 取消时间          |
| selection_source | 字符串 | STM32、Web或管理员 |

第一版也可以只使用 `devices.current_species_id`。

---

# 7. 初始植物范围

第一版建议录入 6 种植物：

| species_id | 显示名称      | 来源 |
| ---------- | --------- | -- |
| pothos     | POTHOS    | 内置 |
| mint       | MINT      | 内置 |
| succulent  | SUCCULENT | 在线 |
| cactus     | CACTUS    | 在线 |
| orchid     | ORCHID    | 在线 |
| tomato     | TOMATO    | 在线 |

内置植物：

* 名称和图片编译在 STM32 固件中；
* 可离线使用；
* 不需要下载图片。

在线植物：

* 名称和基本列表信息通过网关获取；
* 详情通过数据库查询；
* 图片在用户确认后联网下载；
* 下载图片暂存 RAM；
* 断电后重新下载。

---

# 8. 页面结构需求

系统增加以下页面：

```text
PAGE_HOME
PAGE_ENVIRONMENT
PAGE_AI
PAGE_PLANT_LIBRARY
PAGE_PLANT_DETAILS
PAGE_PLANT_DOWNLOAD
```

## 8.1 首页

首页显示：

* 当前植物像素图；
* 当前植物名称；
* 温度；
  -空气湿度；
* 光照；
* 当前综合状态；
* Wi-Fi 和网关状态。

植物卡片应作为可操作入口。

示意：

```text
┌────────────────────────┐
│ GreenMind       WiFi ● │
│                        │
│   ┌────────────────┐   │
│   │   [植物图片]    │   │
│   │     POTHOS     │   │
│   │  PRESS TO EDIT │   │
│   └────────────────┘   │
│                        │
│ TEMP    HUM     LIGHT  │
│ 26 C    65%      62%   │
│                        │
│ ENVIRONMENT GOOD       │
└────────────────────────┘
```

用户在主页选择植物卡片后进入植物选择页面。

## 8.2 植物选择页

页面采用两列卡片布局。

示意：

```text
┌────────────────────────┐
│ ← SELECT PLANT         │
│                        │
│ [LOCAL 2]  [ONLINE 4]  │
│                        │
│ ┌────────┐ ┌────────┐  │
│ │ [图标] │ │ [图标] │  │
│ │ POTHOS │ │  MINT  │  │
│ │CURRENT │ │BUILT-IN│  │
│ └────────┘ └────────┘  │
│                        │
│ ┌────────┐ ┌────────┐  │
│ │ 云朵↓  │ │ 云朵↓  │  │
│ │ CACTUS │ │ ORCHID │  │
│ │ ONLINE │ │ ONLINE │  │
│ └────────┘ └────────┘  │
│                        │
│ UP:NEXT   KEY1:OPEN    │
└────────────────────────┘
```

卡片状态：

```text
CURRENT
BUILT-IN
ONLINE
LOADING
OFFLINE
ERROR
```

## 8.3 植物详情页

显示内容：

* 植物名称；
* 图片或在线占位图；
* 推荐温度范围；
* 推荐空气湿度范围；
* 推荐光照范围；
* 简短养护说明；
* 资源来源；
* 网络状态；
* 下载按钮或使用按钮。

在线植物示例：

```text
┌────────────────────────┐
│ ← PLANT DETAILS        │
│                        │
│        [云朵↓]          │
│        ORCHID          │
│                        │
│ TEMP       18 - 28 C   │
│ HUMIDITY   55 - 80 %   │
│ LIGHT      30 - 60     │
│                        │
│ Bright indirect light  │
│ Resource: 4.5 KB       │
│                        │
│ DOWNLOAD AND USE       │
└────────────────────────┘
```

内置植物按钮显示：

```text
USE THIS PLANT
```

## 8.4 图片下载页

页面显示：

* 当前植物名称；
* 下载阶段；
* 下载进度；
* 已接收字节数；
* 总字节数；
* 网络状态；
* 错误信息。

示意：

```text
┌────────────────────────┐
│ DOWNLOADING PLANT      │
│                        │
│        [云朵动画]       │
│                        │
│        ORCHID          │
│                        │
│ [■■■■■■■■□□□□]  68%    │
│                        │
│ 3060 / 4500 BYTES      │
│ RECEIVING IMAGE...     │
│                        │
│ KEEP DEVICE ONLINE     │
└────────────────────────┘
```

下载状态：

```text
CONNECTING
LOADING_INFO
RECEIVING_IMAGE
VERIFYING
READY
FAILED
CANCELLED
```

下载成功后显示：

```text
ORCHID READY
```

随后自动返回首页。

---

# 9. 按键交互需求

根据现有两个主要按键设计：

## 9.1 首页

| 操作        | 功能                |
| --------- | ----------------- |
| KEY_UP 短按 | 切换主页功能焦点          |
| KEY1 短按   | 打开当前焦点            |
| KEY1 长按   | 发起 AI 请求或返回，按页面定义 |

## 9.2 植物选择页

| 操作        | 功能                 |
| --------- | ------------------ |
| KEY_UP 短按 | 移动到下一植物卡片          |
| KEY_UP 长按 | 切换 LOCAL/ONLINE 分类 |
| KEY1 短按   | 打开植物详情             |
| KEY1 长按   | 返回首页               |

焦点顺序：

```text
左上 → 右上 → 左下 → 右下 → 下一页
```

## 9.3 植物详情页

| 操作        | 功能            |
| --------- | ------------- |
| KEY1 短按   | 使用内置植物或下载在线植物 |
| KEY1 长按   | 返回植物列表        |
| KEY_UP 短按 | 切换详情内容或按钮焦点   |

## 9.4 下载页

下载期间原则上禁止重复发起其他植物请求。

允许：

* 长按 KEY1 取消下载；
* 严重错误时自动退出；
* 传感器采集继续运行；
* 网络事件继续处理。

---

# 10. 植物列表业务流程

```text
用户从首页进入植物选择页
        ↓
STM32检查本地缓存列表
        ↓
显示内置植物
        ↓
STM32发送在线植物列表请求
        ↓
电脑网关查询数据库
        ↓
网关发送精简在线植物列表
        ↓
STM32合并内置和在线植物
        ↓
用户浏览植物卡片
```

当网络不可用时：

* 仍显示内置植物；
* 在线分类显示 `OFFLINE`；
* 不影响环境采集和 AI 之外的本地功能。

---

# 11. 植物选择业务流程

## 11.1 选择内置植物

```text
用户选择内置植物
        ↓
STM32设置当前 species_id
        ↓
加载内置图片
        ↓
向网关发送植物选择结果
        ↓
网关更新 devices.current_species_id
        ↓
返回首页
```

## 11.2 选择在线植物

```text
用户选择在线植物
        ↓
进入植物详情页
        ↓
用户确认 DOWNLOAD AND USE
        ↓
STM32请求图片元数据
        ↓
网关查询数据库和图片文件
        ↓
网关分包发送图片
        ↓
STM32接收并存入RAM
        ↓
校验图片
        ↓
设置当前 species_id
        ↓
网关更新当前植物
        ↓
返回首页显示新植物
```

---

# 12. AI 个性化分析流程

STM32 发起 AI 分析时发送：

```text
请求编号
设备编号
植物唯一标识
温度
空气湿度
光照值
光照等级
```

示例协议：

```text
V1|AI_REQ|15|AGRI001|orchid|26|65|62|NORMAL
```

电脑网关执行：

```text
接收AI请求
→ 根据 orchid 查询 plant_species
→ 查询 plant_requirements
→ 获得兰花完整档案
→ 构造模型输入
→ 调用DeepSeek
→ 校验结构化结果
→ 返回STM32
```

模型输入示例：

```json
{
  "plant": {
    "species_id": "orchid",
    "name": "Orchid",
    "temperature_range_c": [18, 28],
    "humidity_range_percent": [55, 80],
    "light_range_percent": [30, 60],
    "light_preference": "bright_indirect",
    "watering_style": "check_soil_before_watering",
    "risk_notes": [
      "avoid prolonged strong sunlight",
      "avoid waterlogged roots",
      "maintain suitable ventilation"
    ]
  },
  "environment": {
    "temperature_c": 26,
    "humidity_percent": 65,
    "light_percent": 62,
    "light_level": "normal"
  },
  "constraints": {
    "no_soil_moisture_sensor": true,
    "air_humidity_is_not_soil_moisture": true,
    "ai_cannot_control_hardware": true
  }
}
```

模型返回：

```json
{
  "status": "warning",
  "main_issue": "slightly_strong_light",
  "temperature_fit": "suitable",
  "humidity_fit": "suitable",
  "light_fit": "warning",
  "watering_advice": "check_soil",
  "action_code": "REDUCE_DIRECT_LIGHT",
  "short_advice": "Light is slightly strong for orchid. Avoid direct sunlight.",
  "detailed_reason": "Temperature and humidity are suitable, but the current light level is near the upper preferred limit."
}
```

STM32只接收：

```text
status
main_issue
watering_advice
action_code
short_advice
```

---

# 13. 网络协议需求

## 13.1 请求植物列表

```text
V1|PLANT_LIST_REQ|AGRI001
```

返回：

```text
V1|PLANT_LIST_BEGIN|4
V1|PLANT_ITEM|succulent|SUCCULENT|ONLINE|48|48
V1|PLANT_ITEM|cactus|CACTUS|ONLINE|48|48
V1|PLANT_ITEM|orchid|ORCHID|ONLINE|48|48
V1|PLANT_ITEM|tomato|TOMATO|ONLINE|48|48
V1|PLANT_LIST_END|4
```

## 13.2 请求植物详情

```text
V1|PLANT_DETAIL_REQ|orchid
```

返回：

```text
V1|PLANT_DETAIL|orchid|ORCHID|18|28|55|80|30|60|4500|1
```

字段依次表示：

```text
species_id
display_name
temp_min
temp_max
humidity_min
humidity_max
light_min
light_max
image_size
image_version
```

## 13.3 请求植物图片

```text
V1|PLANT_IMAGE_REQ|orchid|1
```

网关先返回元数据：

```text
V1|PLANT_IMAGE_META|orchid|48|48|RGB565|4608|checksum
```

随后发送专用图片数据块。

图片数据不得作为大量普通文本事件直接塞入现有 AI 事件队列。

## 13.4 图片完成

STM32 校验成功后发送：

```text
V1|PLANT_IMAGE_OK|orchid|1
```

失败时：

```text
V1|PLANT_IMAGE_ERROR|orchid|CHECKSUM
```

## 13.5 设置当前植物

```text
V1|PLANT_SELECT|AGRI001|orchid
```

返回：

```text
V1|PLANT_SELECTED|orchid|ORCHID
```

---

# 14. 图片传输设计

## 14.1 第一版图片规格

推荐：

```text
尺寸：48×48
格式：RGB565
背景：统一背景色
缓存：单张当前植物图片
保存位置：RAM
```

单张未压缩图片大小：

```text
48 × 48 × 2 = 4608 字节
```

第一版暂不要求：

* 多图片并存；
* 掉电保存；
* 动态 Flash 分区；
* 图片增量更新；
* 断点续传。

## 14.2 列表中的在线植物图片

植物列表页不下载所有在线图片。

在线植物卡片统一显示：

```text
云下载图标
通用叶片轮廓
ONLINE 标签
```

只有用户进入详情并确认下载后，才下载正式图片。

## 14.3 图片转换

数据库可保存 PNG 源文件，但网关发送前应转换为 STM32 可显示的 RGB565 数据。

推荐流程：

```text
数据库保存 PNG 路径
→ Python 网关读取 PNG
→ 缩放到 48×48
→ 转换为 RGB565
→ 计算校验值
→ 分包发送
```

STM32 不解析 PNG、JPEG 等复杂图片格式。

---

# 15. 下载状态机

新增独立状态机：

```text
PLANT_DL_IDLE
PLANT_DL_REQUESTING_META
PLANT_DL_RECEIVING
PLANT_DL_VERIFYING
PLANT_DL_READY
PLANT_DL_ERROR
PLANT_DL_CANCELLED
```

下载状态机与 AI 请求状态机分离。

下载期间应满足：

* 传感器继续采集；
* TFT 下载进度局部刷新；
* Wi-Fi 和 TCP 事件继续处理；
* 不阻塞主循环；
* 图片数据不进入普通业务事件队列；
* 网络断开时清理未完成缓存；
* 原当前植物保持不变，直到新图片完整校验成功。

这是一个重要的事务原则：

```text
下载失败
→ 保留原植物

下载成功并校验
→ 才切换当前植物
```

---

# 16. 错误处理

## 16.1 网络离线

显示：

```text
ONLINE PLANTS UNAVAILABLE
```

允许继续选择内置植物。

## 16.2 数据库无记录

显示：

```text
PLANT NOT FOUND
```

不改变当前植物。

## 16.3 图片不存在

可以允许使用在线植物，但显示通用占位图：

```text
IMAGE UNAVAILABLE
```

AI 分析仍可正常进行。

## 16.4 图片校验失败

显示：

```text
DOWNLOAD FAILED
CHECKSUM ERROR
```

丢弃临时图片，保留原植物。

## 16.5 下载超时

显示：

```text
DOWNLOAD TIMEOUT
```

退出下载状态。

## 16.6 RAM不足

网关返回图片元数据后，STM32应先检查缓存容量。

不足时显示：

```text
IMAGE TOO LARGE
```

## 16.7 网关重启

网关恢复后，STM32可以重新请求当前植物信息和图片。

---

# 17. 非功能需求

## 17.1 响应时间

* 植物列表请求建议在 3 秒内返回；
* 植物详情建议在 2 秒内返回；
* 48×48 图片建议在 10 秒内完成传输；
* AI 分析沿用当前 45 秒超时机制。

## 17.2 可用性

即使在线植物功能失败，系统仍应支持：

* 环境数据采集；
* TFT 实时显示；
* 内置植物选择；
* 本地状态提示；
* 原有 AI 功能的错误提示；
* 后续水泵本地安全控制。

## 17.3 数据一致性

* 当前植物只在图片校验成功后切换；
* 数据库和 STM32 应使用同一 `species_id`；
* AI 分析记录应保存请求时使用的植物 ID；
* 设备历史数据不能因后续换植物而被篡改。

## 17.4 安全性

* STM32 不保存数据库密码；
* STM32 不保存模型 API Key；
* 图片尺寸和字节数必须校验；
* 网关不得发送超过协议限制的图片；
* 模型结果不得直接控制硬件。

---

# 18. MVP范围

## 18.1 必须实现

1. 数据库维护至少 6 种植物；
2. 2 种内置植物；
3. 4 种在线植物；
4. STM32 首页植物入口；
5. 植物选择页面；
6. 植物详情页面；
7. 在线植物图片下载页面；
8. 单张 48×48 RGB565 图片下载到 RAM；
9. 图片校验；
10. 当前植物切换；
11. AI 请求携带 `species_id`；
12. 网关查询植物完整档案；
13. 不同植物产生差异化 AI 建议；
14. 网络失败时不影响内置植物和环境采集。

## 18.2 暂不实现

* 用户注册登录；
* 手机 App；
* 多设备管理；
* 图片永久写入 Flash；
* 多张图片本地缓存；
* 图片断点续传；
* 在线植物搜索；
* 植物收藏；
* 自动识别植物；
* 生长阶段管理；
* 病虫害识别；
* 多植物同时监测；
* AI 直接控制水泵。

---

# 19. 验收标准

## AC-01 首页入口

用户能够从主页植物卡片进入植物选择页。

## AC-02 内置植物

系统能够显示并选择至少两种内置植物。

## AC-03 在线列表

网络和网关正常时，系统能够获取至少四种在线植物。

## AC-04 详情展示

用户能够查看在线植物的温度、湿度和光照偏好。

## AC-05 图片下载

用户选择在线植物后，系统能够：

* 显示下载页面；
* 显示下载进度；
* 完整接收图片；
* 校验图片；
* 在首页显示下载后的植物图片。

## AC-06 失败保护

下载失败时：

* 当前植物不改变；
* 原植物图片仍然显示；
* 系统不死机。

## AC-07 个性化 AI

在相同环境数据下选择不同植物时，模型应返回不同分析。

例如：

```text
温度：30℃
湿度：45%
光照：80
```

绿萝可能返回：

```text
STRONG LIGHT
MOVE TO SHADE
```

仙人掌可能返回：

```text
LIGHT SUITABLE
WATCH TEMPERATURE
```

## AC-08 离线降级

关闭电脑网关或热点后：

* 在线植物不可下载；
* 内置植物仍可选择；
* 传感器采集正常；
* TFT 不死机；
* 网络错误可见。

## AC-09 AI安全边界

任何模型结果均不得直接启动水泵。

---

# 20. 推荐开发顺序

## 第一阶段：数据库与网关查询

* 建立 SQLite 数据库；
* 录入 6 种植物；
* 完成植物列表查询；
* 完成植物详情查询；
* 使用电脑脚本测试返回结果。

## 第二阶段：植物选择 UI

* 增加首页植物入口；
* 开发植物选择页；
* 开发植物详情页；
* 先使用 Mock 列表测试焦点和按键。

## 第三阶段：植物身份切换

* STM32 保存 `species_id`；
* 网关更新当前植物；
* AI 请求增加植物 ID；
* 验证不同植物产生不同建议。

## 第四阶段：静态图片链路测试

* Python 将一张 PNG 转为 RGB565；
* 暂时通过本地数组验证 TFT 显示；
* 确认 48×48 图片格式和颜色正确。

## 第五阶段：联网图片传输

* 设计专用图片接收状态机；
* 实现元数据请求；
* 实现图片分包；
* 实现下载进度；
* 实现完整性校验；
* 图片暂存在 RAM。

## 第六阶段：异常测试

* 下载中断；
* 网关退出；
* 热点断开；
* 图片大小错误；
* 校验失败；
* 用户取消；
* 连续切换植物。

---

# 21. 最终业务闭环

```text
STM32采集真实环境
        ↓
用户点击主页植物卡片
        ↓
浏览内置和在线植物
        ↓
选择在线植物
        ↓
从数据库获取植物详情
        ↓
联网下载像素图片到RAM
        ↓
设置为当前植物
        ↓
STM32上传植物ID和环境数据
        ↓
网关查询完整植物知识
        ↓
大模型进行个性化分析
        ↓
STM32显示植物专属建议
```

---

# 22. 模块最终定位

本模块不是简单的植物图片下载功能，而是：

> 一个由云端植物知识库、STM32图形化植物选择、在线资源分发和大模型个性化分析组成的植物内容服务模块。

核心价值可以概括为：

```text
STM32知道“当前选择了哪种植物”；
数据库知道“这种植物需要什么环境”；
传感器知道“当前位置是什么环境”；
大模型负责解释“当前环境对这种植物意味着什么”。
```


修改：水泵 个性化AI的Galgame展示

就是说，当前AI返回的只有非常简单的建议（在stm32上），不够个性化，不够有趣。AI应该结合植物的特点和当前环境特点，给出一段不枯燥的分析（比如结合该植物养殖的趣味小知识或是啥的），比如对话框或者galgame底部的文字。


现在我这个项目（GreenMind）要交给同学写报告和写ppt，请你提取这个GreenMind项目的所有有用信息，忽略错误的试错，发挥你的总结能力，整理一份md文档，要求对方可以根据所有信息制作当前阶段的项目汇报ppt和当前阶段的实验报告。当然，也要承认现在没有拍摄运行图片。