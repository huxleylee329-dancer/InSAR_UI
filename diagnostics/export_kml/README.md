# KML 导出回归测试

`ExportKMLRegression.cpp` 直接调用生产版 `ExportKMLWorker`，通过实际 Hdf5IO DLL 创建临时 H5，并通过实际 FormatConversion/Utils DLL 读取坐标、渲染 JPG 和写入 KML。无需打开主窗口。

`run_regression.py` 使用 MSVC v143、C++14 和 Release 运行库编译 Worker、BaseWorker、日志实现及 MOC 文件。为避免依赖整个主窗口，脚本从当前 `NodeUtils.cpp` 原样提取本测试涉及的读取函数和锁实现；没有模拟 HDF5 返回值。源码结构变化导致无法提取时，脚本会失败。

从项目根目录运行（路径变量指向本机已安装的依赖）：

```powershell
python diagnostics/export_kml/run_regression.py --qt-root "$qtRoot" --sdk-root "$sdkRoot" --vs-root "$vsRoot" --out "$buildDirectory"
```

- Qt：5.15.2 MSVC x64，包含运行库及 offscreen、JPEG 图像插件。
- SDK 根目录：包含 `opencv/build`、`HDF5-1.8.22` 和项目使用的 GDAL SDK 目录。
- 核心算法目录默认是同级 `InSAR0629`，可通过 `--core-root` 指定。
- v143 默认版本为 `14.44`，可通过 `--toolset-version` 指定兼容 Qt 5.15 的版本。
- 构建与测试日志写入 `--out`，不会替换应用的 `bin/SatExplorer.exe`。

33 个用例验证 float/double 坐标与图像输出、KML 四角顺序及参考点、缺失数据集、参考点上下界、矩阵尺寸、错误坐标类型、10 个采样位置的非有限值、经纬度范围、渲染/输出错误，以及工作线程在 35%/60% 取消时的信号和文件清理。验证失败必须恰好发送一个错误信号，不得发送成功信号，也不得生成输出文件。

可选传入 `--baseline-source` 和 `--baseline-header`，分别指向修复前 Worker 源文件和头文件。脚本另行构建旧版本，以缺失 `mapped_lat` 的 H5 对照测试：旧版本预期访问冲突退出码 `0xC0000005`，修复版本预期正常报错退出。

本测试使用合成 H5；不代表已完成真实项目数据及 GUI 手动/自动工作流验收。
