# 官方 CAD 资料清单

检索日期：2026-08-29。以下文件仅作为供应商参考几何；产品外壳、旋环、支撑和装配关系仍是 `CONCEPT_FIT_DEFAULTS`，不是量产冻结。

| 文件 | 来源 | SHA-256 |
| --- | --- | --- |
| `cubemars/GL30_KV290_factory_encoder_official.step` | CubeMars GL30 KV290 官方“带编码器”三维下载：<https://www.cubemars.com/data/cms/202602/gl30-gimbal-motor-with-encoder-2d-drawing.rar> | `11FB05DACBB01C5BFCA0D030B915645373499C2E65C8C6DF4388E06F790A7B96` |
| `cubemars/GL30_KV290_factory_encoder_official_2D.pdf` | CubeMars 官方二维图：<https://www.cubemars.com/data/cms/202602/gl30-gimbal-motor-with-encoder-2d-drawing.pdf> | `CA34D415251AF450FF8DAC2E48CB24707FAF987C501522EF384B153A62281BC7` |
| `cubemars/GL30_KV290_factory_performance_curve.png` | 2026-09-01 用户提供的厂家性能曲线截图；原始曲线未提供测功机条件 | `DFD293CA2B157BCDF4BEB4C4D77219209708C17D2179F7555808DB4710D11CD4` |
| `waveshare/ESP32-S3-Touch-AMOLED-1_32_official.step` | Waveshare 官方资源页中的 3D 文件：<https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.32/ESP32-S3-Touch-AMOLED-1.32-3dFile.rar> | `BEC7D12B0A084FBAC14A1C7411C1F72E1961D0CC4779A7143AEA1F3F39CA50B3` |
| `waveshare/ESP32-S3-Touch-AMOLED-1_32_official_dimensions.pdf` | 同一 Waveshare 官方 3D 包中的二维图 | `758B01932311148E227E69C8CF9E0DC16E5A7DA5600146C5A9DF11E65CB794C5` |

## 已核对包络

- GL30 工厂编码器版：约 `Ø34.5 mm × 28.2 mm`，中孔 `Ø6 mm`。
- Waveshare 模块：STEP 总包络约 `36.53 mm × 37.70 mm × 11.30 mm`；圆形主体约 `Ø36.5 mm`。

## 2026-09-01 到货资料核验

- 用户上传的 `gl30-gimbal-motor-with-encoder-2d-drawing.rar` SHA-256 为 `0A2BC77F4B0C0010B13EBC2773D50F0365AD21AA6CF827909323314A843510FF`，包内只有一个 STEP。
- 解出的 STEP 为 `699802` bytes，SHA-256 与仓库现有官方 STEP 完全一致，因此未保存重复副本。
- 用户上传二维截图与现有官方 PDF 的 `Ø34.5 × 28.2 mm`、`Ø6`、两侧 `Ø20` 分布圆及 M3 深度标注一致；仓库保留可追溯的官方 PDF，不另存截图副本。
- 电气参数与首测边界见 [`../../motor-control/GL30_KV290_ARRIVAL_BASELINE_CN.md`](../../motor-control/GL30_KV290_ARRIVAL_BASELINE_CN.md)。

## 尚未从公开资料确认

- AS5048A 线色与功能映射已确认（黑 GND、红 +5V、绿 MISO、黄 MOSI、蓝 CLK、白 CSn）；但实际供电/逻辑电平、连接器准确型号（含厂商件号）、SPI 时序、刷新率、延迟与工厂校准方式仍未从公开资料闭环。
- 3-M3 / 4-M3 两侧的定子/转子功能、出线弯折包络、中孔穿线限制和允许轴向/径向载荷。

## 本轮夹具使用的几何基准

直接回读上述原始 STEP：出线侧为 3 个 M3 孔，另一侧为 4 个 M3 孔，均为 Ø20 分布圆。夹具保留孔阵列实际角度，不把两侧都画成十字孔阵列。组装前手转确认哪一面固定、哪一面随转子转动。

二维图标注深度分别为 3.5 / 2.5 mm，STEP 中对应圆柱孔段约为 3.0 / 2.3 mm。几何孔深也不等于可用的完整螺纹深度。本轮夹具按 M3×6、3.5 mm 打印安装厚度、0.5 mm 垫圈得到 **2.0 mm 名义伸入量**；实物量完尺寸链再装，不强拧到底。无需为此拆掉厂家编码器。
