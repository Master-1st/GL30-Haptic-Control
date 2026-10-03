"""Write the R2 review notes and a hash-verified model package from passed CAD."""
from pathlib import Path
import hashlib
import json
import zipfile

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'output/models/GL30_COMPACT_R2'
DELIVERY=ROOT/'outputs/GL30_R2_紧凑旋钮_20260911'

def main():
    report=json.loads((OUT/'verification.json').read_text(encoding='utf-8'))
    assert all(report['checks'].values()), report['checks']
    d=report['dimensions']; rows=report['parts']
    top=max(p['bbox']['zmax'] for p in rows.values())
    bottom=min(p['bbox']['zmin'] for p in rows.values())
    board_sizes={'A':'56 × 51','B':'90 × 66','C':'72 × 66；主圆环 Ø66 / Ø58，左侧连接耳'}
    text=f'''# GL30 R2：紧凑旋钮与固定屏幕结构评审

2026-09-11。用户已确认可定制合理的电池外形。本版是可检查的机械方案，含真实板框、机构分件和装配包络；PCB 尚未布线，电池和手感尚未完成实物验证。

![R2 外观](assembly.png)

## 尺寸与空间

| 项目 | 上一版 | R2 |
|---|---:|---:|
| 机身平面包络 | 118 × 114 mm | **104 × 100 mm** |
| 桌面占地包络 | 13,452 mm² | 10,400 mm²，减少 **{d['footprint_reduction_percent']:.1f}%** |
| 机身前缘 / 后平台高度 | 20 / 58 mm | 24 / 56 mm |
| 旋钮外径 | 54 mm | 56 mm |
| 旋钮露出斜面的高度 | 13.2 mm | **19.7 mm**，增加 6.5 mm |
| 外露灯环径向宽度 | 约 9 mm | **1.4 mm** |
| 灯窗相对斜面的最高点 | 1.0 mm | 0.2 mm |
| PCB 厚度 | 1.2 mm | 全部保持 1.2 mm |
| 按压总行程 | 仅预留包络 | 0.35 mm，由独立硬止挡限定 |

机身为 26° 斜面接水平后平台，平面四角 R12。旋钮轴线位于机身中心左侧 8 mm、前侧 10 mm，为右侧电池仓腾出空间。整套装配包含后桥、脚垫和接口的包络约 **104 × 102 × {top-bottom:.1f} mm**。占地缩小不等于整机高度降低：这次有意加高握持圈，后桥也是最高结构。

| 主板 | 本地板框最大包络，mm | 布置 |
|---|---|---|
| A 控制/电机 | {board_sizes['A']} | 左后侧异形板，绕开中央机构和屏幕后桥；底面 Z=35.5 |
| B 电源/接口 | {board_sizes['B']} | L 形板，侧翼布在电池上方；底面 Z=29.0 |
| C RGB/输入 | {board_sizes['C']} | 随斜面固定，24 颗 LED；局部板底法线坐标 -5.2 |

板框文件在 `boards/`，每板均有 STEP 和 DXF。A/B 内侧避让拐角 R1.5。板上盒体是功能区域与连接器预留，不是最终封装、焊盘或完整布线证明。B 前侧安装点由壳体悬臂台阶支撑；C 的三点支撑直接做在壳体中，不能只按独立隔柱零件数量判断固定点。

![内部布局](internal_layout.png)

## 定制电池

采用普通长方形 3S 软包组合，不要求弧形、环形或绕电机的特殊电芯。

- **成品本体目标：60 × 32 × 21 mm**。这是含保护板、绝缘包覆和 NTC 后的外形目标，不是只给裸电芯的尺寸。
- 机内预留 **62 × 34 × 23 mm** 的装配与尺寸变化空间；引线及接插件走独立通道。
- 电气目标延续 3S 架构；容量可先按约 1 Ah 与供应商协商，不能由 CAD 体积保证。持续电流、充电限值、逐节保护、均衡、温度测量和成品最大外形需由厂商书面确认。
- 原 101 mm 长的 LPHD5919096 不再是 R2 的尺寸基准，不能拿旧 BOM 中该电池直接装入本版。

厂家已有类似体积的 [DNK503450-3S 定制包](https://www.dnkpower.com/products/11-1v-1000mah-lipo-battery-dnk503450-3s-battery-pack-3s-lithium-polymer-battery/)可用于评估定制工艺；它并不是本次 60 × 32 × 21 mm 的已确认型号。

## 按压怎样稳住

旋环的旋转由独立 6808 尺寸包络的轴承承载；不旋转的承载架由一根 MGN7 微型滚珠直线导轨约束。电机定子、承载架和轴承支撑一起作 0.35 mm 轴向微移，导轨负责限制横移、倾斜与不希望出现的扭转；外旋环仍可连续旋转。

受力路径：手指 → 旋环 → 独立承载轴承 → 不转的承载架 → 弹簧 / 硬止挡 → 固定底架 → 底壳。开关位于固定支架，只检测行程，过压由硬止挡承受。两只复位弹簧有独立容纳孔，具体弹簧预压、开关及小转接/软板件仍待选定。

固定底架增加三处底部紧固点；屏幕后桥底座两孔固定；屏幕托盘使用官方模型读取的三个 M2 安装轴线。PCB 的支撑独立于电池，主承力件不压住软包电芯。

把电机一起浮动并不会自动消除电机轴承的轴向载荷。R2 在传扭路径中增加 **0.10 mm 弹簧钢六辐条膜片**，允许小幅轴向顺从，避免刚性传动桥形成过硬的并联受力路径。按六根理想固导梁、有效长 6 mm、宽 2 mm、E=200 GPa 估算，轴向刚度约 11.1 N/mm；这是筛查计算，不是零轴向载荷、疲劳寿命或强度合格证明。该件不能用普通打印薄片替代。

导轨包络采用 MGN7C 的 17 × 8 × 22.5 mm，导轨长 35 mm；两轨孔距 15 mm、首尾各 10 mm 是订制孔位要求。5 N 作用在半径 28 mm 处的力矩为 0.14 N·m，低于目录中 2.84 N·m 的最小静态额定力矩；这个比较不能代替整机刚度与手感测试。参见 [HIWIN 官方目录](https://www.hiwin.com/wp-content/uploads/HIWIN-Linear-Guideway-Catalog.pdf)与[轨道长度范围](https://www.hiwin.de/en/Products/Linear-guideways/Profile-rails/Miniature-guides/MGNR-HIRES-series/MGNR07R600HM/p/5-001060)。

## 屏幕为什么不会跟转

**屏幕由机身后侧的固定桥架承托。桥架固定在底架，屏幕既不随电机旋转，也不随按压移动。** 后桥顶部高于旋钮顶面 1.2 mm，旋环从桥架下方转过；桥内布线不承担防转或承重作用。

必须纠正此前的中心管假设：当前带编码器版 GL30 的原始官方 STEP 与 Ø5 / Ø3.2 固定管有 **{report['factory_encoder_centre_access']['overlap_mm3']:.4f} mm³** 的干涉，阻挡位于后部结构。厂家图上的 Ø6 标注不足以证明带编码器总成能贯通穿管。CAD 发现不等于已测量实物通孔；R2 结构直接避开这个不确定性。

保留这台电机的代价是屏幕背后有一条约 6 mm 宽的可见固定桥。若要求屏幕四周完全无桥，需更换真正贯通的中空电机总成，或重新设计传动及编码器布置；不能把排线拧紧来代替固定支架。

![机构剖面：金色为固定屏幕支撑](mechanism_section.png)

## 排线与灯环

板间信号及屏幕链路改为锁扣 FFC/FPC，后桥内有独立线槽和盖片；跨按压组件的线束留弯曲余量。屏幕桥中的裸线段按约 3.5 mm 宽、0.2 mm 厚预留，两端连接器在桥外。

普通 [Hirose FH12](https://www.hirose.com/product/series/FH12?lang=en) 信号连接器单触点额定 0.5 A；主电源和电机三相使用独立额定电流足够的扁平软线束。屏幕供电的触点数量、铜厚、电流分配、最终针脚及锁扣朝向仍需结合实测功耗确定。不能因为线材外形扁平就认为所有排线都能传输电机电流。

24 颗 LED 藏在固定光腔里，通过连续扩散窗和窄出光口出光。CAD 渲染中的青色仅表示灯窗位置；均匀度、蓝色指针可辨识度、颜色分界和漏光要用实物 LED/扩散材料验证。

## 本轮验证与未完成项

生成器当前检查全部通过：**{len(report['intersections'])} 组干涉检查、{len(report['press_position_checks'])} 项按压位置检查、{len(report['component_area_checks'])} 个器件区域检查**；三块板的 STEP 回读厚度均为 1.2 mm。主要新结构件均为单个有效实体，旋转采用覆盖全周的包络检查，按压检查 0 / 0.175 / 0.35 mm 三个位置。

这不是全公差、有限元、寿命、热、光学或实体手感验收。建议首样检查四象限 5 N 偏压、回弹与导轨阻力、膜片残余轴向力、全圈与按压到底时的擦碰，以及灯环热点。显示原始 STEP 含无效拓扑：有效装配 STEP 中未嵌入它，预览使用原始参考；包内另附官方显示文件与放置变换，不能把装配有效性说成官方显示模型已修复。

芯片主功能分区延续原三板设计，本轮没有完成新的 PCB 电路布局/布线，也没有修改固件、烧录、给电机上电或采购。结构/互连改动见 [R2 变更清单](R2_BOM_delta_CN.md)。

## 文件与复现

- `GL30_COMPACT_R2_ASSEMBLY.step`：有效机械装配与预留体；导轨、轴承、电池等采用明确标注的尺寸包络。
- `parts/`：主要结构分件 STEP；`boards/`：三块板的 STEP + DXF。
- `verification.json`：本轮几何结果和原始来源哈希；`independent_review.json` 为独立审查记录，须结合其中记录的文件哈希判断版本。
- `display_reference_transform.json`：官方显示参考的放置变换。
- 原仓库执行：`.venv-cad\\Scripts\\python.exe hardware/cad/compact_knob.py`。依赖 CadQuery / OCP / VTK；复现不会操作硬件。
'''
    (OUT/'设计说明_CN.md').write_text(text,encoding='utf-8')
    (OUT/'R2_BOM_delta_CN.md').write_text('''# R2 结构与互连变更清单

2026-09-11。这是相对 9/10 电子 BOM 的结构/互连差异，不是采购已下单清单，也不替代完整电气 BOM。

| 类别 | 数量 | R2 内容 | 状态 |
|---|---:|---|---|
| 电池组 | 1 | 定制长方形 3S 成品本体 60×32×21 mm，含 PCM/绝缘/NTC；线束另留空间 | 用户允许定制，容量、电流、均衡与厂商可制造性待确认 |
| PCB A / B / C | 各 1 | 56×51、90×66、72×66 mm 最大包络，全部厚 1.2；实际异形轮廓以 DXF 为准 | 板框已建，未布线 |
| 微型直线导轨 | 1 套 | MGN7C 22.5 mm 滑块 + 35 mm 轨，孔距/预压依最终订单确认 | 官方尺寸包络；预压、微行程阻力待样件验证 |
| 独立旋转轴承 | 1 | 6808，40×52×7 mm 包络；金属轴承座与轴向保持件 | 配合、间隙、密封阻力和承载待选定 |
| 扭转膜片 | 1 | 0.10 mm 弹簧钢六辐条，用于传扭并提供轴向顺从 | 初步几何与刚度估算，未强度/疲劳定型 |
| 复位弹簧 | 2 | 约 Ø3 mm 安装包络，工作长度约 3.5 mm | 弹簧刚度、预压和按压力需实测确定 |
| 按压检测件 | 1 组 | 固定开关、绝缘安装/小转接或软板、调节压头 | 包络已留，最终料号与触发行程未冻结 |
| 硬止挡/垫片 | 1 组 | 独立于开关，外部总行程 0.35 mm | 名义尺寸已建，装配调节范围待样件 |
| FFC/FPC | 按连接关系定量 | A↔B、显示链路、RGB/输入；锁扣连接器，桥内线槽 | 针脚、长度、铜厚、供电电流和弯折半径未冻结 |
| 主电流扁平软线束 | 1 组 | 电池/SYS/电机三相，独立于普通信号 FFC | 按实际持续及峰值电流选定，不以0.5A信号触点代替 |
| 屏幕后桥/托盘 | 各 1 | 与底架机械固定，屏幕不跟转；取消穿过电机的固定中心管 | 本版采用，官方带编码器STEP阻挡旧管路 |
| 连续灯窗/遮光件 | 各 1 组 | 24 LED 固定腔体、1.4 mm 外露环、左侧服务耳盖 | 机械几何完成，光学尚未实测 |
| 固定底架/承载架/板隔柱/紧固件 | 1 组 | 导轨与屏幕承力单独闭合到机身，电池不承压 | 名义安装已建，材料/螺纹/预紧待出加工图 |

原 STM32、DRV8316、电源管理及 ESP32 AMOLED 的系统分工延续；器件最终摆放、散热和电源/地回路需要基于新板形重新布置，因此不能称旧 BOM 与 PCB 都已无需修改。原来的电池、竖直板间连接方案和机械支撑件不得直接按旧尺寸采购。
''',encoding='utf-8')
    import math
    center=d['hmi_center'];angle=math.radians(26)
    transform={'file_in_package':'references/ESP32-S3-Touch-AMOLED-1_32_official.step',
      'rotate_about_X_degrees':-64,'then_translate_mm':[center[0],center[1]-math.sin(angle)*8.8,center[2]+math.cos(angle)*8.8],
      'sampled_tessellation_radius_mm':19.312451623963163,'tessellation_tolerance_mm':.02,
      'mesh_vertices':1418194,'note':'Mesh clearance evidence, not healing of vendor topology.'}
    (OUT/'display_reference_transform.json').write_text(json.dumps(transform,ensure_ascii=False,indent=2),encoding='utf-8')
    DELIVERY.mkdir(parents=True,exist_ok=True)
    files=[OUT/n for n in ('GL30_COMPACT_R2_ASSEMBLY.step','assembly.png','internal_layout.png','mechanism_section.png','verification.json','设计说明_CN.md','R2_BOM_delta_CN.md','display_reference_transform.json')]
    files+=list((OUT/'boards').glob('*.step'))+list((OUT/'boards').glob('*.dxf'))
    allowed=('housing','bottom_cover','fixed_chassis','press_cartridge','fixed_screen_bridge','fixed_screen_tray','rotating_grip','rotating_bearing_seat','rotor_drive_bridge','torque_diaphragm_0p1','diaphragm_outer_clamp','bearing_outer_lower_retainer','diffuser','optical_ear_cap','battery_tray')
    files += [OUT/'parts'/(n+'.step') for n in allowed]
    if (OUT/'independent_review.json').exists(): files.append(OUT/'independent_review.json')
    entries=[(p,'GL30_COMPACT_R2/'+p.relative_to(OUT).as_posix()) for p in files]
    for name in ('compact_knob.py','write_compact_review.py','v7_params.py','v7_product_model.py'):
        entries.append((ROOT/'hardware/cad'/name,'GL30_COMPACT_R2/source/hardware/cad/'+name))
    for rel in ('waveshare/ESP32-S3-Touch-AMOLED-1_32_official.step','cubemars/GL30_KV290_factory_encoder_official.step'):
        p=ROOT/'hardware/cad/vendor'/rel;entries.append((p,'GL30_COMPACT_R2/references/'+p.name))
    manifest={arc:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p,arc in entries}
    target=DELIVERY/'GL30_R2_结构评审模型包.zip'
    with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p,arc in entries: z.write(p,arc)
        z.writestr('GL30_COMPACT_R2/manifest.json',json.dumps(manifest,ensure_ascii=False,indent=2))
    with zipfile.ZipFile(target) as z:
        assert z.testzip() is None
        for arc,row in manifest.items(): assert hashlib.sha256(z.read(arc)).hexdigest()==row['sha256']
    result={'file':str(target),'bytes':target.stat().st_size,'files':len(entries)+1,'crc_pass':True,'all_entry_sha256_pass':True,
        'checks':report['checks'],'overall_bbox_mm':[104,102,top-bottom]}
    (DELIVERY/'package-verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False,indent=2))

if __name__=='__main__': main()
