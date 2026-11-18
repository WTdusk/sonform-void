# SONFORM: Void

物理建模手碟与舌鼓音源，当前开发版为 0.9.4，提供 Windows VST3 和独立版。

- 音色型号、音域和使用方式见 [产品手册](plugin/README.md)。
- `build-plugin.ps1` 编译当前版本并运行测试；`Start-HandpanStudio.cmd` 启动当前独立版。
- `package-plugin.ps1` 将当前 VST3、独立版、手册、许可证和对应源码打包到 `dist`。
- `build-installer.ps1` 生成可选择组件和路径的 Windows 安装程序。
- `plugin/data` 保存构建所需的建模系数和键位。

## 开源许可与构建

开发者：**WTdusk**。SONFORM Void 的项目源码采用 GNU AGPL-3.0-only，见 [LICENSE.md](LICENSE.md)。JUCE 及其依赖保留各自的许可证与声明。本项目使用 JUCE 的 AGPLv3 许可选项。发布包内的 `Source` 包含对应源码和构建脚本。

在 Windows 上安装带 C++ 桌面工具的 Visual Studio 和 CMake，使用 PowerShell 7 运行 `./build-plugin.ps1`。可以用 `-JuceDirectory` 指定 JUCE 9.0.2 目录，也可以用 `-BuildDirectory` 指定构建目录。打包时使用同一个构建目录。

文件 `VERSION` 控制构建、打包和独立版启动脚本使用的版本。若有代码签名证书，可在打包时传入 `-CertificateThumbprint` 和 `-TimestampUrl`，并在需要时通过 `-SignTool` 指定 Windows SDK 中的签名工具路径。默认打包为未签名的构建。

已经发布的 VST3 类 ID、参数 ID、音色选项和各音色的奏法槽位属于宿主兼容性约定。尤其不要直接扩充或重排现有 `Model` 参数的 7 个槽位，宿主可能按归一化数值保存自动化。工程状态另有稳定的音色和奏法标识，并保留对 0.9.3 数字索引的读取支持。

## Windows 安装程序

先用 `./build-plugin.ps1` 构建，再用 `./build-installer.ps1` 打包。安装程序使用 Inno Setup 6.4.3 构建。可以通过 `-InnoCompiler` 指定 `ISCC.exe` 的位置。生成的安装程序和手动安装 ZIP 都位于 `dist`。有代码签名证书时，安装包和内含二进制可沿用打包脚本的签名参数。

用户可以分别勾选 VST3 和独立版，各自选择目标目录。VST3 的目标应是宿主扫描的插件目录，安装程序会创建完整的 `.vst3` 包。独立版提供开始菜单快捷方式。Windows 的已安装应用列表提供卸载入口。许可证、手册和对应源码随每种安装选项一起提供。

仓库保留项目源码、安装脚本、建模数据、JUCE 依赖及设计源文件。构建目录和 `dist` 已被忽略，可通过脚本重建。发布时可把安装程序和 ZIP 作为 Release 附件上传。
