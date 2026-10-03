# GL30 日照金山照片表盘

本页保留照片表盘素材与当时验收记录。菜单已按后续要求改为黑底八项循环圆环，当前交互与验证见 [循环菜单验收](orbit-menu-demo-20260910-cn.md)；下文同图双入口菜单描述仅为历史状态。

日期：2026-09-10。本次按用户“发挥高分辨率圆屏优势，像手表一样同时呈现图片与信息”的要求，更新桌面与菜单外观。

- [打开新版预览](http://127.0.0.1:8768/firmware-esp32/ui/preview/?v=summit-20260910)
- [466×466 表盘特写](../output/alpenglow-watch-20260910/face-466.png) · [后台计时特写](../output/alpenglow-watch-20260910/face-466-running.png) · [完整桌面](../output/alpenglow-watch-20260910/home-desktop.png) · [菜单](../output/alpenglow-watch-20260910/menu-desktop.png) · [320px 布局](../output/alpenglow-watch-20260910/home-320.png)
- [本地入口](../firmware-esp32/ui/preview/index.html) · [操作说明](../firmware-esp32/ui/preview/README_CN.md)

## 外观与素材

1254×1254 的日照金山背景铺满圆屏，上方天空叠加大号时分、独立秒数和日期；下方半透明卡片显示天气示例与后台计时。金色山尖位于信息之间，不被时钟遮挡。菜单使用同一背景并适度暗化，保留清楚的应用选择卡片。

图片与文字固定在玻璃层，不随外环旋转；熄屏由既有显示层统一关闭。时间、日期、秒数和计时状态仍为实际运行的文字，未烘焙进图片。天气仍明确标为示例，不是本地天气实况。

背景使用内置 `image_gen` 生成，源图原样复制进项目，没有再使用脚本修改像素；裁切和信息叠加由 SVG/CSS 完成。它是生成的雪山景观，不代表具体山峰实拍或实时景象。

- 素材：[assets/summit-dawn.png](../firmware-esp32/ui/preview/assets/summit-dawn.png)，1254×1254，2,038,247 字节。
- [完整生成提示词](../output/alpenglow-watch-20260910/image-prompt.txt) · [来源和 SHA-256](../output/alpenglow-watch-20260910/asset-manifest.json)。
- 图片随 `preview/assets` 一起携带，HTTP 与本地 file 均可打开，无远端运行依赖。

## 验证

| 检查 | 结果与证据 |
| --- | --- |
| 壁纸加载、分辨率、圆形裁切、文字完整性、熄屏/唤醒/切页 | HTTP 20/20，本地 file 20/20；[HTTP](../output/alpenglow-watch-20260910/wallpaper-http.json) · [file](../output/alpenglow-watch-20260910/wallpaper-file-320.json) |
| 导航与应用回归 | HTTP 与 file 各 35 项导航、51 项应用检查；[HTTP 导航](../output/alpenglow-watch-20260910/navigation-http.json) · [HTTP 应用](../output/alpenglow-watch-20260910/interaction-http.json) · [file 导航](../output/alpenglow-watch-20260910/navigation-file-320.json) · [file 应用](../output/alpenglow-watch-20260910/interaction-file-320.json) |
| 触摸 | 25/25，覆盖外环和确定按钮的单击/双击/长按以及运行中手动调时；[结果](../output/alpenglow-watch-20260910/touch.json) |
| 原状态/手势模型 | 51/51；[日志](../output/alpenglow-watch-20260910/unit-tests.txt) |
| 466×466 实际浏览器渲染 | [截图尺寸记录](../output/alpenglow-watch-20260910/native-face.json)；照片细节、日期、时间、秒数和卡片已目视检查 |

主线程验收时实际发现日期与时钟重叠、天气文字被圆边裁切，并已修正后复测；初版截图留在 `output/alpenglow-watch-20260910/first-home.png`，不作为当前交付。截图工具曾命中同 URL 的旧标签页，已改为核对当前照片表盘 DOM 后再截图。

运行代码中的 `model.js`、`gesture.js` 与前一验收版字节相同，`interaction.js` 只改两处桌面说明文案。原单击确认、双击返回、长按未设置、计时回转、手动调时、连续彩色灯带和模拟零点止挡保持原规则。旧“全素材为 SVG、禁止位图”的集成断言依据新需求替换为“照片只在桌面/菜单，应用图形与外环保持矢量”，没有删减业务断言。

本轮验收的是浏览器渲染与操作，466×466 特写不是实屏照片。未修改固件、连接电机或验证面板亮度/功耗、解码内存与光学表现；图片尚未移植为 ESP/LVGL 资源。当前代码及图片校验值见 [运行文件清单](../output/alpenglow-watch-20260910/runtime-sha256.json)，结果汇总见 [验收摘要](../output/alpenglow-watch-20260910/audit-summary.json)。
