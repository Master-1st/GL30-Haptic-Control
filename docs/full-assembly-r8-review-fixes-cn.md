# R8：按 R7.1 独立机械审查修改

日期：2026-09-12。保持104×98外形、收腰握环、四侧键/后电源键、单MGN7、1:1短同步带和独立固定屏幕。

当前模型入口：[整机STEP](../output/models/GL30_FULL_R8/GL30_FULL_R8_ASSEMBLY.step)、[验证JSON](../output/models/GL30_FULL_R8/verification.json)、[38类无电样件与用量](../output/print/GL30_R8_REVIEW_SAMPLES/先读_打印数量.md)、[修改与复审包](../outputs/r8-review-fixes/GL30_R8_机械修改与复审包_20260912/README_先读.md)。

主要改动：

- 输出轴、三臂架与握环改为可区分的分件连接，内外圈各有定位链；从动轮到薄盖的名义间隙增至0.65 mm。
- 滑块主安装面增加四孔L转接件；导轨、连续支架、底部背板、底盖与壳体嵌件补齐名义紧固接口。
- 电机后端三孔安装座、前端四孔转接毂、主动轮沉孔锁母及张紧槽重新建模；保留6 mm皮带工作宽度。
- 四侧键让出0.4 mm总行程；后键增加防脱肩和固定座；主旋钮增加释放止挡、弹簧座和固定弹片根部。
- 固定屏柱顶部用两只M8锁环夹托盘，按屏幕原厂图3-M2.00增加三颗模组固定螺钉。
- 重新布置电源、UVW、编码器、控制线和FFC包络，板边/支架避让随之更新；电池增加绑带空间。
- 取消额外盖玻璃、浮动装饰圈、实体蓝标；普通打印件、精密/薄金属尺寸样和假件分开标注。

全量检查包含同组分件、线束/排线、指定螺纹区域、按压、侧键、止挡、输出回转和电机张紧。以验证JSON的实际结果与文件哈希为准，旧R7.1的23项结果不沿用。

本轮没有实物装配或带电测试。弹片触发余量的条件估算仅约0.029 mm；真实开关F-S曲线、弹簧、导轨型号、轴承公差、短螺纹承载、夹紧防松、线束卡固/弯曲、连续装入路径、光学和温升仍需验证。**只交付无电尺寸样候选，不放行整套正式加工或带电力反馈。**

可复跑入口：

```powershell
.venv-cad\Scripts\python.exe hardware\cad\full_knob_assembly.py --no-render
.venv-cad\Scripts\python.exe hardware\cad\export_r8_review_samples.py
.venv-cad\Scripts\python.exe outputs\r8-review-fixes\build_release.py
```

打印样件出口仅在当前R8源码哈希和几何检查一致时运行。输出STEP/STL/3MF，不生成G-code，不连接打印机。
