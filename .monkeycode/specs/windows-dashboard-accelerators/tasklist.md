# 需求实施计划

- [ ] 1. 实现仪表盘报告模型与解析器
  - [x] 1.1 创建普通 C++ 报告模型并使用 Windows.Data.Json 解析核心 JSON
  - [x] 1.2 处理缺失场景、未知字段、类型错误和解析异常
  - [ ] 1.3 添加解析器单元测试，覆盖有效、降级、错误和畸形报告

- [x] 2. 实现 WinUI 卡片式仪表盘
  - [x] 2.1 在 MainWindow XAML 中增加状态区、场景卡、换行模块卡和原始 JSON 折叠区
  - [x] 2.2 在 UI 线程渲染场景分数、瓶颈、模块状态、分数和指标
  - [x] 2.3 完善 ready、running、success、empty 和 error 状态转换
  - [x] 2.4 保持 AppT、MainWindowT 和 InitializeComponent 启动链不变

- [x] 3. 实现 Direct3D 12 GPU 插件
  - [x] 3.1 增加硬件适配器选择、WARP 诊断开关和 D3D12 资源管理
  - [x] 3.2 实现 FP32 GEMM compute pipeline、计时、读回和 checksum 校验
  - [x] 3.3 输出统一分数与指标，并实现 not_supported/error 语义
  - [x] 3.4 添加 Windows 构建和 WARP 正确性测试

- [x] 4. 实现 DirectML NPU 插件
  - [x] 4.1 动态探测 DXCore、D3D12 和 DirectML API
  - [x] 4.2 选择非图形 ML/Core Compute 适配器并创建 DirectML 设备
  - [x] 4.3 实现 FP16 GEMM、同步、读回、校验和指标输出
  - [x] 4.4 添加无 NPU 环境的插件加载与降级测试

- [x] 5. 集成构建、占位模块与发布包
  - [x] 5.1 增加 Windows-only CMake 选项和独立插件目标
  - [x] 5.2 将 gpu_dx12 与 npu 内建占位状态改为 not_supported
  - [x] 5.3 将新插件加入 Windows release 构建、签名、ZIP 和文件检查
  - [x] 5.4 更新中英文开发文档和模块支持状态

- [ ] 6. 检查点
  - [ ] 6.1 确保 Linux 测试、Windows 构建、插件 smoke test 和 WinUI 启动测试通过

- [ ] 7. 提交完整功能分支
  - [ ] 7.1 复核差异、提交所有规格与代码并推送功能分支
  - [ ] 7.2 创建单一 Pull Request 并附上验证结果
