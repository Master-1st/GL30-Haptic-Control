# 实际代码评审裁决

第三轮主线程已在读取外部结论前独立记录判断和握手授权栅栏缺口。DeepSeek V4 Pro 完成（job 20261002175454-cf0ffe70ae37，finish=stop，全文4046字符）。用户取消关机并允许今天保持开启后，按新版运行授权规则复用已在线千问实例；首个补交请求503，重试 fccd6c7f1eee 完整返回。第三、四、五轮两方使用各轮相同证据包，当前参与范围见 [最终记录](review-final.json)。第一轮架构评审中的千问意见不是当前代码意见。

1. DeepSeek 第1项“ARM取走后永久阻塞”不接受：其第4步执行 owner_disarm，实际调用 gl30_motor_stop→gl30_motor_arm_gate_stop，明确把 arm_request 清为 false 并推进 stop_generation。不能按它建议重新排队或自动恢复故障前的 ARM。新增实际同poll FIFO_OVF回归验证该反例。
2. DeepSeek 第2项不属于出力门控绕过。CONTROL_RUNNING 表示逻辑租约可用，零租约回显允许未对齐/故障状态，这使安全停机维护与物理READY分离。公开 arm 返回值只确认当前人工请求通过本地栅栏；owner 当次健康检查和 STM 本地资格仍控制执行，故障/未READY会立即撤销且不会自动恢复。已明确API注释并补实际无正限值/故障保持的回归，而不把存储绑定到物理对齐。主线程另发现更精确的实际缺口：握手期间捕获的旧输入到READY后仍被接受；同源多poll RED已复现，修复为每次 FIRST_ZERO→RUNNING 无条件推进停止栅栏。此项必须GREEN后才接受。
3. DeepSeek 第3项“无伙伴不持久化”本来就是明确约定，不能因未回应而允许Flash；pending/status 已公开。接受其可观测性建议：稳定设置申请维护却未取得token时，记录 INVALID_STATE并保留pending，沿用5s重试间隔。中途对端失联时不清控制预约恢复ARM；待新租约确认才能清维护状态是有意的关闭策略。

其证据缺口部分如实保留：软件回显不是物理GPIO/相线/电流证据，实际Flash暂停、UART溢出、掉电写入及装配手感未验收。本包后续提供实际 gl30_foc_force_zero 与安全监督器/字段映射以及完整当前主机回归，不把模型意见当作实板验证。

## 第四轮交叉复核

DeepSeek V4 Pro（job 20261002181959-f199187a95c1，finish=stop，全文2263字符）完成当前补充证据审查，明确撤回第三轮第1、第2项，接受第3项可观测性修正，并确认 FIRST_ZERO→RUNNING 无条件停止栅栏的执行顺序有效。主线程已复跑当前 owner 及生命周期回归；最终总矩阵另见验证记录。

它另提出最终停止检查至 uart_tx_chars 之间可再送出一帧。主线程接受这是一个真实的软件边界，保留为未完成硬件停止时序验证，未增加提交互斥锁：这正是前阶段明确保留并由 stop_at_tx_commit_allows_one_frame_then_sends_zero 覆盖的异步STOP契约，不是本轮新发现或本轮声称消除的风险。实际 ESP-IDF 5.5.1 的 uart_tx_chars 内部使用 tx_mux 和 portMAX_DELAY，不可直接置于共享 portMUX 临界区；新增任务互斥还会改变解析停止的即时提交契约。本轮把API注释写明这一帧边界，不宣称STOP返回即可物理关桥。该边界不能放行Flash：维护仍需随后完整零命令身份回显、精确RELEASE及独占确认，早一帧正命令不能满足这些条件。

千问第四轮 3c1bd7b01d3f 完整返回，接受该轮三项裁定和无条件完成栅栏，另提出HELD未end与代次耗尽可能长期阻挡控制。该轮包原本回应DeepSeek第三轮意见，不能把这次回答说成已直接撤回千问自己的第三轮五项结论。因此另做了下方针对性第五轮。

## 第五轮针对性反驳与最终裁决

同一份 [第五轮证据包](final-cross-packet-round5.txt)含千问第三/四轮最终意见、主线程逐项反证、实际候选复制/安全监督器/Flash生命周期/快照代码、本机NVS open实现及已有回归；各源选取范围与SHA显式记录。DeepSeek 20261003030607-0b66672da616 与千问 28d14d12469f 均完整返回（finish=stop），见 [DeepSeek最终意见](deepseek-final-cross-r5.json)和 [千问最终意见](qwen-final-cross-r5.json)。

主线程按实际代码裁决，接受当前离线候选：

- 千问明确撤回第三轮五项安全缺陷结论；DeepSeek也确认释放清时间戳后不老化到COMM_LOST、拒绝ACQUIRE不提交局部候选、快照二次锁校验及NVS open失败语义。主线程逐项取证早于读取本轮结论，见 [反证记录](host-qwen-code-r3-adjudication.md)。不加HELD零心跳，不放宽精确G/N释放。
- 保留“外部API持有者永不end会阻挡ARM/新维护”的观察，作为预约契约而非本应用遗漏。实际preferences_service在超时、显示失败、claim失败和同步写返回的成功/失败路径均结束预约。已claim且Flash尚未返回时，不能因UART错误自动清token/重新获取；现有HELD错误静默回归直接验证预约仍被持有。
- 保留64位lease_counter耗尽时进入CONTROL_FAILED的限制：不回绕、不复用、不自动ARM；恢复需显式重启。未新增推测性的恢复层，也未声称已经执行饱和故障注入测试。
- DeepSeek第四轮关于异步STOP提交间隙的意见继续保留；有主机在途帧测试，但实际物理停止时限尚未验收。第五轮它末尾误把该边界称作zero_echo检查，最终定义仍以真实run发送路径与API注释为准。

三方完成的是当前代码与边界审查，不是PCB、物理桥、实屏FPS或电机手感认证。后续源码仅另改foc.h旧线长度注释为72字节，MSVC完整回归和两个Keil重建通过；各轮源码与最终源码的关系见 [覆盖核对](review-source-coverage.json)。
