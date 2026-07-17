# GreenMind 12×12 中文字库来源与生成

GreenMind 的 12×12 单色 GB2312 字库主要由开源的
[Fusion Pixel Font](https://github.com/TakWolf/fusion-pixel-font)
生成。该字体覆盖全部6763个GB2312汉字，但缺少177个数学、拼音和特殊符号；
这些符号使用同为SIL OFL 1.1许可证的
[Noto Sans CJK SC](https://github.com/notofonts/noto-cjk)
补齐。

- 上游版本：`2026.07.01`
- 上游文件：`fusion-pixel-font-12px-monospaced-bdf-v2026.07.01.zip`
- 上游压缩包 SHA-256：
  `c461c312720e005f5a49843a965842e8749f196f00f97facf1ad926f330296df`
- 使用字形：`fusion-pixel-12px-monospaced-zh_hans.bdf`
- 字体许可证：SIL Open Font License 1.1
- 上游发布页：
  <https://github.com/TakWolf/fusion-pixel-font/releases/tag/2026.07.01>
- Noto备用字体提交：
  `165c01b46ea533872e002e0785ff17e44f6d97d8`
- Noto备用字体SHA-256：
  `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`

生成结果位于：

```text
firmware/Assets/fonts/greenmind_gb2312_12x12.bin
firmware/Assets/fonts/greenmind_gb2312_12x12.json
```

二进制按 GB2312 双字节编码直接寻址：

```text
slot = (lead - 0xA1) * 94 + (trail - 0xA1)
offset = slot * 18
```

编码范围为首字节 `0xA1..0xF7`、尾字节 `0xA1..0xFE`。每个字模为
12×12、1 bpp、行优先连续位、最高位在前，共18字节。未分配的GB2312码位保留
为空白，因此整个文件大小固定为147204字节。

重新生成：

```cmd
python tools\fonts\generate_gb2312_font.py ^
  path\to\fusion-pixel-12px-monospaced-zh_hans.bdf ^
  firmware\Assets\fonts\greenmind_gb2312_12x12.bin ^
  --metadata firmware\Assets\fonts\greenmind_gb2312_12x12.json ^
  --fallback-font path\to\NotoSansCJKsc-Regular.otf
```

这里只准备字库资产，尚未把字库链接进固件，也没有修改现有UI渲染路径。
