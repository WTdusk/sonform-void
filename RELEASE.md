# SONFORM Void 0.9.4

**中文** · [English](#english)

WTdusk · Windows x64 · VST3 与独立版

## 本版更新

- 改进实时音频处理的重置行为，并优化快速切换型号时的淡化过渡。
- 保留 0.9.3 的 VST3 类 ID，支持读取 0.9.3 的工程和预设。
- 重新打开工程或预设时会解除 Granular Freeze。再次演奏即可建立新的粒子效果缓冲。
- Windows 安装向导可分别选择 VST3 与独立版，并为每个组件指定安装目录。

## 下载与安装

从 [0.9.4 发布附件](https://github.com/WTdusk/sonform-void/releases/tag/v0.9.4#assets) 选择需要的文件。

| 文件 | 用途 |
| --- | --- |
| `SONFORM Void 0.9.4 Windows Setup.exe` | 按安装向导选择组件和目录。独立版可从开始菜单启动。 |
| `SONFORM Void 0.9.4 Windows.zip` | 手动安装。先解压，再复制完整的 `.vst3` 插件包或运行独立版 EXE。 |
| `SHA256SUMS.txt` | 核对安装程序和 ZIP 的 SHA-256 文件校验值。 |

VST3 应安装到 DAW 会扫描的路径。安装后重新扫描插件，在乐器轨道加载 SONFORM Void。独立版的音频设备和 MIDI 输入可在 Settings 中设置。两个 Windows 发布包都附带对应源码及许可证，并带有 WTdusk 发布者信息。目前的二进制文件未做 Authenticode 数字签名。

## 兼容性与验证

已在 Windows x64 上从发布 ZIP 的源码重新构建 VST3 和独立版，`plugin_checks` 回归测试通过。独立测试宿主验证了 Orvane、Serevin 和 Somera 的 VST3 音频输出。安装向导的默认路径页面、自定义路径安装及卸载均已验证。各 DAW 中低缓冲多实例的表现尚待验证。

[项目首页及界面截图](https://github.com/WTdusk/sonform-void) · [安装与演奏手册](https://github.com/WTdusk/sonform-void/blob/main/Hand%20Pan%20Modeling/plugin/README.md)

## English

WTdusk · Windows x64 · VST3 and standalone

### What’s changed

- Improved real-time audio reset behavior and the fade when switching models quickly.
- Kept the 0.9.3 VST3 class ID and support for loading projects and presets saved with 0.9.3.
- Granular Freeze turns off when you reopen a project or preset. Play again to fill a new granular buffer.
- The Windows installer lets you choose VST3, standalone, or both, with a separate destination for each component.

### Download and install

Choose a file from the [0.9.4 release assets](https://github.com/WTdusk/sonform-void/releases/tag/v0.9.4#assets).

| File | Use |
| --- | --- |
| `SONFORM Void 0.9.4 Windows Setup.exe` | Install with the setup wizard. Launch the standalone instrument from the Start menu. |
| `SONFORM Void 0.9.4 Windows.zip` | Extract the archive for manual installation. Copy the complete `.vst3` bundle or run the standalone EXE. |
| `SHA256SUMS.txt` | Verify the SHA-256 checksums for the installer and ZIP. |

Install VST3 to a folder your DAW scans, rescan plug-ins, and load SONFORM Void on an instrument track. In the standalone app, choose your audio device and MIDI input under Settings. Both Windows packages include corresponding source code and license files. The binaries carry WTdusk publisher metadata and are not Authenticode-signed.

### Compatibility and verification

The VST3 and standalone builds were rebuilt from the release ZIP’s source on Windows x64 and passed the `plugin_checks` regression suite. A test host verified VST3 audio output for Orvane, Serevin, and Somera. The default folder screens, installation to custom paths, and uninstallation were checked. Performance with multiple instances at small buffer sizes remains unverified across DAWs.

[Project home and interface screenshot](https://github.com/WTdusk/sonform-void) · [Playing guide (Chinese)](https://github.com/WTdusk/sonform-void/blob/main/Hand%20Pan%20Modeling/plugin/README.md)
