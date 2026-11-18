# SONFORM Void 0.9.4

WTdusk · Windows x64 · VST3 和独立版

Windows 安装程序可以分别选择 VST3 和独立版及各自的安装目录。VST3 请选择 DAW 会扫描的目录。本版改进实时音频重置和快速切换音色的淡化，保留 0.9.3 的 VST3 类 ID 与状态读取支持。重新打开工程时会解除 Granular Freeze，以便重新录入临时音频。

## 发布范围

仓库收录项目源码、建模数据、测试、构建脚本、安装脚本、设计原型和构建所需的 JUCE 源码及许可声明。JUCE 示例、可选工具、本地构建、临时文件和 `dist` 由仓库忽略规则排除。

发布附件包括 `SONFORM Void 0.9.4 Windows Setup.exe`、`SONFORM Void 0.9.4 Windows.zip` 和 `SHA256SUMS.txt`。安装程序和 ZIP 均附带对应源码。当前 Windows 文件带有 WTdusk 发布者信息，未进行 Authenticode 数字签名。

## 公开发布前待核实

- 实际 DAW 的低缓冲多实例表现尚待验证。若对外声明支持特定宿主，先在该宿主中验证扫描、演奏和旧工程恢复。

## 已完成验证

- 从发布 ZIP 解压精简后的源码，在 Windows x64 上全新编译 VST3 和独立版，`plugin_checks` 通过。
- 独立测试宿主加载新构建的 VST3，验证 Orvane、Serevin 和 Somera 出声。
- 安装向导的默认路径页面及自定义路径安装与卸载已验证。
