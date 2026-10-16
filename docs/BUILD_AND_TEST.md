# 构建与测试

本文提供离线检查、固件与 Qt 构建，以及 Python、网页和小程序的本地运行入口。

## 1. 工具与环境

| 模块 | 工具 / 依赖 |
| --- | --- |
| Python 离线检查与仿真器 | Python 3.10+、pytest、Flask、paho-mqtt |
| JavaScript 语法检查 | Node.js，使用 `node --check` |
| ESP32 固件 | PlatformIO Core、Espressif32 平台、各 `platformio.ini` 声明的 Arduino 库 |
| Qt 控制端 | Qt 5.x；原工程使用 Qt 5.12 风格 API，包含 Widgets / SerialPort / Network |
| 树莓派显示 | Python 3 标准库 |
| x86 问答 | FastAPI、uvicorn |
| 树莓派问答 | FastAPI、uvicorn、python-multipart、Vosk、edge-tts；本地语音模型 |
| 微信小程序 | 微信开发者工具及自己的项目配置 |

Python 的 `3.10+` 来自源码类型注解语法要求；本次实际验证环境列于[验证记录](VERIFICATION.md)。各固件保留原始平台依赖声明，重建时应记录本机实际解析到的平台与库版本。

## 2. 离线检查

已有 Python 依赖和 Node.js 时，从仓库根目录运行：

```powershell
python -B tools/run_offline_checks.py
```

执行内容：

1. 检查可发布文件的类型、固件空连接常量与非示例地址。
2. 在内存中编译 Python，解析 JSON、Qt `.ui` 和 `.qrc`，检查资源与文档引用。
3. 校验 `SOURCE_MANIFEST.json` 中的公开源码 SHA-256。
4. 使用 `node --check` 解析 `.js` 文件及 HTML 内联脚本，不执行页面脚本。
5. 禁用 pytest 外部插件自动加载及缓存，运行原有测试。

Python 审计钩子拒绝套接字连接、绑定、DNS 与 UDP 发送。测试使用本地 Flask 客户端和假 MQTT 对象；临时目录位于仓库 `.verification/`，结束后清理本轮临时文件。`-B` 关闭字节码缓存。

需要创建自己的独立依赖环境时：

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-test.txt
.\.venv\Scripts\python.exe -B tools/run_offline_checks.py
```

Linux / 树莓派可使用 `.venv/bin/python` 代替 Windows 路径。依赖安装需要预先准备包源或离线 wheel；检查脚本不安装依赖。

只做公开包静态检查：

```powershell
python -B tools/check_public_package.py
```

单独运行原有根目录源码回归：

```powershell
python -B -m unittest discover -s tests -v
```

统一入口提供网络阻断与临时目录约束，作为本包记录结果的标准命令。静态发布检查针对整理后的干净包；本地真实配置应按 `.gitignore` 留在自己的运行环境中。

## 3. 固件构建

只编译、不上传的命令示例：

```powershell
pio run -d esp32s3_water_gateway_pond1
pio run -d esp32s3_water_gateway_pond2
pio run -d esp32s3_water_gateway_pond3
pio run -d esp32s3_environment_gateway
pio run -d new-esp32s3_water_gateway
pio run -d new-esp32s3_actuator
pio run -d new-esp32s3_fish_counter
```

WROOM-32UE 的池塘环境：

```powershell
pio run -d esp32_wroom32ue_water_gateway -e pond1
pio run -d esp32_wroom32ue_water_gateway -e pond2
pio run -d esp32_wroom32ue_water_gateway -e pond3
```

PlatformIO 可能在首次构建时下载平台、编译器和库。空连接配置用于审阅和构建，实际硬件环境按[配置说明](CONFIGURATION.md)设置。源码契约测试与编译解决的问题不同，构建结果应以目标工具链实际输出为准。

## 4. Qt 控制端构建

在已配置 Qt 5.x 与匹配 C++ 编译器的终端中，采用独立输出目录：

```powershell
New-Item -ItemType Directory -Path .verification/qt-build -Force
Set-Location .verification/qt-build
qmake ../../smpqt_control/test/test.pro
```

随后使用该 Qt 套件匹配的构建工具，例如 MinGW 的 `mingw32-make`、MSVC 的 `nmake` 或 Linux 的 `make`。`.pro` 已声明 `core gui widgets serialport network`，Qt 资源由 `res.qrc` 引用，`ui_widget.h` 由构建过程生成。

Qt 程序包含串口、系统信息、网络状态、录音播放和远程控制代码。编译与界面阅读可以和设备联调分别进行；实际运行时需要匹配 Linux/树莓派的串口、GPIO、音频及命令行工具环境。

## 5. Python 应用本地运行

下列命令用于启动对应模块进行本地联调。

### 三池塘仿真器

安装 `FishWaterSimulator/requirements.txt`，按配置文档准备自己的测试 broker。启动面板：

```powershell
python FishWaterSimulator/app.py
```

面板默认位于 `http://127.0.0.1:5057`。加载页面本身不会启动池塘 worker；面板中的启动操作会建立 MQTT 连接。将仿真器与真实采集设备安排在独立的联调环境，保持数据来源可辨认。

### 树莓派轻量显示

```powershell
python raspi_water_display/app.py --host 127.0.0.1 --port 8088
```

配置自己的网关后，显示服务通过 HTTP 代理读取 `/api/water/latest`。默认空网关配置用于等待数据。

### x86 文本问答

在局部虚拟环境中按 `x86api_text_ai/requirements.example.txt` 安装依赖，并在启动前设置 URL、模型与密钥等配置：

```powershell
python x86api_text_ai/after_api.py --host 127.0.0.1 --port 8000
```

URL 与超时读取时机详见[配置说明](CONFIGURATION.md)。模型请求会访问配置的外部服务。

### 树莓派语音问答

在该模块自己的虚拟环境安装 `smpapi_voice_assistant/requirements.txt`，根据本机环境设置 Vosk 模型目录：

```powershell
Set-Location smpapi_voice_assistant
python smp_api.py --host 127.0.0.1 --port 8010
```

文本问答与语音能力分别由健康/能力接口表达，WAV 上传字段名为 `audio`。Vosk 模型按其来源与许可证单独准备。

## 6. 网页与小程序

网页入口为 `web_dashboard/dashboard.html`。通过浏览器打开后配置网关、AI 和摄像头地址；Tailwind、ECharts、Font Awesome 与字体从页面声明的 CDN 加载。浏览器对跨域、混合内容与文件页请求的限制由实际运行方式决定，可在自己的 Web 服务中托管该文件。

小程序从 `wx_mini_program/` 导入微信开发者工具。先将 `project.config.example.json` 复制为 `project.config.json`，再使用自己的 AppID 和合法请求域名设置。页面代码按微信运行时执行，Node.js 仅用于语法检查，不替代开发者工具的编译与真机验证。

## 7. 结果解释

Python 测试覆盖协议、状态和本地接口行为；固件源码测试检查配置与关键契约；语法检查覆盖各语言的解析入口。固件、Qt 与小程序分别使用对应工具链构建。

本次已执行命令和结果以 [VERIFICATION.md](VERIFICATION.md) 为准。
