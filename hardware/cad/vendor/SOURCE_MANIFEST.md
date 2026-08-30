# 官方 CAD 资料清单

检索日期：2026-08-29。以下文件仅作为供应商参考几何；产品外壳、旋环、支撑和装配关系仍是 `CONCEPT_FIT_DEFAULTS`，不是量产冻结。

| 文件 | 来源 | SHA-256 |
| --- | --- | --- |
| `cubemars/GL30_KV290_factory_encoder_official.step` | CubeMars GL30 KV290 官方“带编码器”三维下载：<https://www.cubemars.com/data/cms/202602/gl30-gimbal-motor-with-encoder-2d-drawing.rar> | `11FB05DACBB01C5BFCA0D030B915645373499C2E65C8C6DF4388E06F790A7B96` |
| `cubemars/GL30_KV290_factory_encoder_official_2D.pdf` | CubeMars 官方二维图：<https://www.cubemars.com/data/cms/202602/gl30-gimbal-motor-with-encoder-2d-drawing.pdf> | `CA34D415251AF450FF8DAC2E48CB24707FAF987C501522EF384B153A62281BC7` |
| `waveshare/ESP32-S3-Touch-AMOLED-1_32_official.step` | Waveshare 官方资源页中的 3D 文件：<https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.32/ESP32-S3-Touch-AMOLED-1.32-3dFile.rar> | `BEC7D12B0A084FBAC14A1C7411C1F72E1961D0CC4779A7143AEA1F3F39CA50B3` |
| `waveshare/ESP32-S3-Touch-AMOLED-1_32_official_dimensions.pdf` | 同一 Waveshare 官方 3D 包中的二维图 | `758B01932311148E227E69C8CF9E0DC16E5A7DA5600146C5A9DF11E65CB794C5` |

## 已核对包络

- GL30 工厂编码器版：约 `Ø34.5 mm × 28.2 mm`，中孔 `Ø6 mm`。
- Waveshare 模块：STEP 总包络约 `36.53 mm × 37.70 mm × 11.30 mm`；圆形主体约 `Ø36.5 mm`。

## 尚未从公开资料确认

- GL30 编码器芯片、接口、电压、线序、刷新率、延迟与工厂校准方式。
- 3-M3 / 4-M3 两侧的定子/转子功能、出线弯折包络、中孔穿线限制和允许轴向/径向载荷。
