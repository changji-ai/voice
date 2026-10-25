# 实时语音助手（Qt + C++）

实时语音对话桌面应用：**实时语音识别（STT）→ LLM 理解 → 实时语音合成（TTS）**，全程可打断。
STT / TTS / LLM 的引擎与模型全部可在设置里切换，支持**本地离线**与**在线 API** 两条链路。

下载安装：[GitHub Releases](https://github.com/changji-ai/voice/releases/latest)，提供 win-x64 / mac-x64 / mac-arm64 / linux-x64 / linux-arm64 五个平台的免安装 zip。

## 功能

- 实时对话：麦克风持续监听，说完一句话（静音判定）自动送 LLM，回答流式逐句朗读
- 打断：说话时自动打断当前朗读（也有关闭开关）；音量滑块实时调节
- STT 引擎可选：
  - **本地**（默认，离线免费）：流式 Zipformer 中英双语（sherpa-onnx）
  - **DashScope Paraformer** 实时（WebSocket 流式，服务端断句）
  - **OpenAI 兼容** /audio/transcriptions（分段上传）
- TTS 引擎可选：
  - **本地**（默认，离线免费）：Kokoro 中英 37 个预置音色，语速 0.5~2.0 可调
  - **CosyVoice**（阿里云百炼）：预置音色 + 复刻音色，语速/音调/音量可调
  - **OpenAI 兼容** /audio/speech
- LLM：任意 OpenAI 兼容接口（Base URL / API Key / 模型名 / system prompt 可配），流式输出按句朗读
- **音色克隆**：设置 → 音色克隆，拖入视频/音频文件 → ffmpeg 自动提取前 25 秒人声（去除背景音乐）→ 上传云端复刻生成音色 → 在 CosyVoice 引擎下选用
- 本地模型一键下载（ModelScope / hf-mirror 双源，断点续传，进度显示）

## 自动更新

应用内自带自动更新：启动 5 秒后自动检查 GitHub Releases 最新版，发现新版本弹窗确认后：
进度条下载 → 解压 → 静默替换安装目录 → 自动重启新版，全程无需手动操作；也可点「⟳ 检查更新」手动触发。
设置环境变量 `VA_GITHUB_TOKEN` 可绕过匿名 API 限流（可选）。

macOS 首次打开未签名应用需右键→打开，或 `xattr -d com.apple.quarantine VoiceAssistant.app`。

## 发布新版本

```powershell
git tag v1.2.3
# （CMakeLists.txt 里的 project VERSION 也要同步改）
git push origin v1.2.3
```
推 tag 后 GitHub Actions 自动构建 5 个平台并发布 Release（见 `.github/workflows/release.yml`）。

## 环境要求

- Windows 10/11 x64
- 构建：CMake ≥ 3.21 + Qt 6.8（MinGW 13，组件 Widgets/Network/WebSockets/Multimedia/Concurrent）+ Ninja
- 运行：无需安装任何依赖（Qt 与 sherpa-onnx DLL 已随构建输出到 `build/bin/`）
- 本地模型存放在 `%APPDATA%\VoiceAssistant\models\`（首次使用点「下载模型」，STT 约 200MB、TTS 约 350MB）
- 音色克隆需要 ffmpeg（自动探测 PATH、exe 同目录、winget 安装位置）

## 构建与运行

```powershell
$env:PATH = "C:/Qt/Tools/mingw1310_64/bin;C:/Qt/Tools/Ninja;$env:PATH"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64
cmake --build build
# 部署 Qt DLL（首次或 Qt 更新后）
C:/Qt/6.8.3/mingw_64/bin/windeployqt --release build/bin/VoiceAssistant.exe
./build/bin/VoiceAssistant.exe
```

调试入口：`VoiceAssistant.exe --settings --tab 3` 启动即打开设置第 4 页。

## 快速上手

1. 首次启动 → 设置 → 语音识别 / 语音合成：本地模型状态若未就绪，点「下载模型」
2. 设置 → 对话模型：填 Base URL / API Key / 模型名（如 `https://dashscope.aliyuncs.com/compatible-mode/v1` + `qwen-plus`，或 `https://api.deepseek.com` + `deepseek-chat`）
3. 保存 → 回主界面点「开始对话」，说完一句话停顿即自动应答
4. 想换克隆音色：设置 → 音色克隆 → 拖入视频/音频 → 命名 → 开始克隆 → 选用（TTS 引擎切到 CosyVoice 并自动选中新音色）

## 语音克隆：本地 vs 远程的差别

| | 远程（本工具实现） | 本地 |
|---|---|---|
| 原理 | 上传 ≥10 秒音频到云端（如阿里百炼声音复刻 voice-enrollment），服务端大模型注册音色，返回 voice_id，之后 TTS 直接引用 | 需要支持「参考音频」的开源模型（XTTS-v2 / OpenVoice / CosyVoice 本地部署），在本机做零样本合成或说话人适配 |
| 音质 | 高：云端大模型复刻效果好，中文自然度高 | 中：开源模型效果参差，多数需要 GPU 才有可用速度 |
| 隐私 | 音频要上传云端 | 全程不出本机 |
| 费用 | 按量付费（需要 API Key） | 免费 |
| 延迟/可用性 | 依赖网络；注册一次性，之后合成走云端仍需网络 | 完全离线 |
| 集成难度 | 低（纯 HTTP，本工具已内置） | 高：这些模型只有 Python/PyTorch 生态，C++/Qt 原生没有官方推理实现（sherpa-onnx 的 kokoro 等本地模型不支持声音克隆） |

因此本工具的取舍：**克隆走云端（质量高、集成简单），本地引擎用预置音色（离线免费）**；两者可以在设置里随时切换。

## 已验证

- 本地链路闭环：Kokoro 合成中文 → Zipformer 流式识别，文本正确（`tests/test_chain.cpp`）
- LLM 流式：DeepSeek 真实调用，SSE 增量解析与逐句朗读切分正常（`tests/test_llm.cpp`）
- GUI：主窗口与设置四个页签截图检查，无乱码无布局异常
- DashScope 的 Paraformer / CosyVoice / 声音复刻协议按官方文档实现（协议见源码注释），因手头 DashScope Key 失效未做在线实测，填入有效 Key 即可用

## 目录结构

```
CMakeLists.txt            构建脚本（含 sherpa-onnx 导入、测试目标）
src/
  main.cpp                入口（--settings 调试入口）
  MainWindow.*            主窗口：会话调度、对话气泡、逐句朗读、打断
  SettingsDialog.*        设置四页签（识别/合成/对话模型/音色克隆+模型下载）
  audio/                  麦克风采集、顺序播放器、能量 VAD
  stt/                    引擎抽象 + 本地 Zipformer / Paraformer WS / OpenAI 分段
  tts/                    引擎抽象 + Kokoro 本地 / CosyVoice / OpenAI / 音频解码重采样
  llm/                    OpenAI 兼容 SSE 流式
  models/                 本地模型清单与双源下载器
  clone/                  ffmpeg 人声提取 + 声音复刻 API
  util/                   设置持久化（QSettings）
tests/                    闭环冒烟测试 / LLM 在线测试
third_party/sherpa-onnx/  预编译 C API 库（bin/sherpa-onnx-c-api.dll、onnxruntime.dll、include/c-api.h）
```
