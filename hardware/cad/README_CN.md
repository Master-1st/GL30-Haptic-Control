# V7 Product Edition 参数化 CAD

当前发布标签：`CONCEPT_FIT_DEFAULTS`。模型用于在无实物阶段验证官方部件包络、结构层次和默认清距，不是生产图、加工图，也不是载荷/寿命/热验证。

## 当前默认方案

- 壳体：`128 × 100 mm`，前缘 `17 mm`，后缘 `52 mm`。
- 有效操作面：`26°`，水平投影 `71.7606 mm`；后部平台 `28.2394 mm`。
- 旋环中心：距前缘水平 `45 mm`。
- 旋环：外径 `54 mm`、内径 `40 mm`。
- 固定屏幕边框：外径 `39 mm`；与旋环名义径向运动间隙 `0.50 mm`。
- 独立支撑预留：NSK 6808 尺寸包络 `40 × 52 × 7 mm`。这只是为独立承载预留空间，未冻结品牌、密封形式、游隙、配合、预紧或 BOM。
- 电机和屏幕：直接导入供应商官方 STEP，不用圆柱占位冒充。

6808 尺寸依据：NSK 官方产品页 <https://www.nsk.com/engineering/6808-apn.html>。实际轴承选择仍受用户载荷、摩擦预算、配合和寿命计算约束。

## 生成方法（Windows，隔离环境）

```powershell
python -m venv .venv-cad
.\.venv-cad\Scripts\python.exe -m pip install -r hardware\cad\requirements.txt
.\.venv-cad\Scripts\python.exe hardware\cad\v7_product_model.py
```

脚本会生成：

- `V7_CONCEPT_FIT_DEFAULTS_assembly.step`：包含官方 GL30 与 Waveshare STEP 的装配模型。
- `parts/*.stl`：外壳、旋环壳、耦合盘、固定支撑等概念件。
- `V7_CONCEPT_FIT_DEFAULTS_geometry_report.json`：尺寸、来源、包络和断言结果。
- `V7_CONCEPT_FIT_DEFAULTS_isometric.png`：外观视图。
- `V7_CONCEPT_FIT_DEFAULTS_exploded.png`：同轴堆叠/载荷路径视图。
- `V7_CONCEPT_FIT_DEFAULTS_side.png`：侧面包络视图。

CadQuery/OCP 7.8.1 在 Windows CPython 3.12 处理大型 STEP compound 后，解释器析构阶段可能访问冲突。脚本在所有文件关闭并刷新后，仅在 Windows 用 `os._exit()` 跳过该已知析构阶段；构建失败仍会在生成完成前抛错并返回失败。

## 本次自动检查结果

- GL30 官方 STEP：轴向 `28.2000 mm`，最大半径 `17.2500 mm`。
- Waveshare 官方 STEP：厚度 `11.3000 mm`，包含连接器/突出特征的最大半径 `19.3125 mm`。
- 屏幕官方完整几何到旋环内径的最小径向余量：`0.6875 mm`。
- GL30 完整几何到 3 mm 内底面的轴向投影余量：`2.0908 mm`。
- 旋环到斜面/后平台折线的水平余量：`2.4932 mm`。
- 当前几何断言：全部通过。

这些数值只证明当前数字模型不违反已写入的包络断言。编码器出线扫掠、安装面方向、中心孔用途、轴承载荷和实物公差仍未知。

## 厂家回复后优先修改

1. 确认 GL30 3-M3 / 4-M3 的定子、转子和出线侧，必要时翻转电机并重跑检查。
2. 加入真实编码器线束/连接器弯折包络。
3. 确认 `Ø6 mm` 通孔能否容纳固定支撑/线束。
4. 根据允许径向、轴向载荷和实际用户载荷，决定取消独立支撑，或正式选定轴承及配合。
5. 加入 PCB、电池、扬声器、麦克风、双 USB-C 和紧固件的真实 STEP。
