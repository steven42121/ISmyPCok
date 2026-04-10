
这个项目要针对不同的使用场景

我准备集成到：

1. 游戏引擎
2. Maa和Maaend等Maa项目
3. 大模型推理服务器

我的设计思路是模块化，不同的用户/使用场景可以只集成不同的模块（如CPU浮点，AVX512或者GPU图形vulcan或者DX12或者并行计算能力(Cuda,Hip,xpu）或者现代CPU集成的NPU

以及内存，硬盘，网络，虚拟化状态等等

来获取不同硬件性能预测值


cpu_fp、cpu_avx2、cpu_avx512、gpu_vulkan、gpu_dx12、cuda、hip、xpu、npu、memory_bw、disk_seq、disk_rand、net_rtt、net_bw、virt_state

不仅仅这些，我只是举例，帮我完善

另外可以帮我找找有没有GPU性能测试库，好像python有那也可以



另外，我觉得有几种方式供接入使用

1. cli 方便稳定
2. SDK api用extern C等等暴露接口也可以
3. 你来想想还有什么办法

对于开源性能测试库，优先使用cpp库，然后是python，最后没有才自己写（可以用ASM优化）
