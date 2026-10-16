# 模块与目录指南

目录沿用原工程名称，使相对路径、PlatformIO 工程边界、Qt 资源引用和现有测试可以直接对应。下面按职责分组阅读。

## 1. 固件工程

| 目录 | 平台 / 依赖 | 源码责任 |
| --- | --- | --- |
| `esp32s3_water_gateway_pond1/` | ESP32-S3、Arduino、PubSubClient、Adafruit GFX/SSD1306 | 一号池塘 pH、溶解氧、水温、MQTT、HTTP、OLED |
| `esp32s3_water_gateway_pond2/` | 同上 | 二号池塘配置及网关实现 |
| `esp32s3_water_gateway_pond3/` | 同上 | 三号池塘配置及网关实现 |
| `esp32_wroom32ue_water_gateway/` | 经典 ESP32、Arduino、PubSubClient | 无 OLED 的池塘网关，LED 运行指示，三个构建环境 |
| `esp32s3_environment_gateway/` | ESP32-S3、Arduino、PubSubClient | 风、雨、气压、温湿度、光照读取与 HTTP/MQTT 输出 |
| `new-esp32s3_water_gateway/` | ESP32-S3、Arduino、PubSubClient、Adafruit OLED | 五地址多参数水质采集、JSON 输出与本地显示 |
| `new-esp32s3_actuator/` | ESP32-S3、Arduino | UART 帧解析、双路电机脉冲、方向和照明输出 |
| `new-esp32s3_fish_counter/` | ESP32-S3、Arduino、Adafruit OLED | 红外边沿计数、去抖、重新布防、清零、显示与校时 |

每个固件工程的 `platformio.ini` 定义板型、框架、编译环境与库依赖，`src/main.cpp` 为运行入口。池塘工程使用 `include/pond_config.h`，环境工程使用 `include/environment_config.h`；三个 `new-*` 工程主要在 `main.cpp` 顶部定义配置。

推荐顺序：先查看配置，再看读取函数、状态结构、JSON 组装、网络维护和 `loop()`。轮询、数据发布与命令处理分别阅读，可以快速定位传感器异常、数据时间与通信状态问题。

## 2. 协议仿真

`FishWaterSimulator/` 保留纯协议逻辑、应用层和原有测试：

| 路径 | 责任 |
| --- | --- |
| `simulator_core.py` | `SimulatorState`、主题集合、采集时段、发布间隔、读数模式和 JSON 载荷 |
| `app.py` | Flask 路由、池塘管理器、线程生命周期、可注入 MQTT 客户端 |
| `static/` | 仿真面板、表单设置、状态刷新与操作请求 |
| `tests/test_simulator_core.py` | 主题、字段、计数、模式、跨午夜时段、间隔校验与客户端 ID |
| `tests/test_app.py` | Flask 请求处理、输入校验、假 MQTT 发布订阅和表单状态 |
| `tests/conftest.py` | 将当前仿真器源码目录加入测试导入路径 |
| `simulator_config.example.json` | 不含真实连接信息的示例配置 |
| `requirements.txt` | 原有 Flask、paho-mqtt 依赖声明 |

仿真器面板的“启动”会创建 MQTT worker；离线检查只使用测试中的假客户端，不通过面板连接 broker。

## 3. 树莓派与 Qt

`raspi_water_display/` 使用 Python 标准库：

- `app.py` 负责静态页面、配置、健康检查与网关 JSON 代理。
- `static/` 负责轮询、指标显示和配置交互。
- `config.example.json` 给出网关地址与刷新间隔的配置形状。

`smpqt_control/test/` 是 Qt 应用工程，`test` 为原工程名：

| 文件 | 责任 |
| --- | --- |
| `test.pro` | Qt Core、GUI、Widgets、SerialPort、Network 模块及资源声明 |
| `main.cpp` | Qt 应用入口 |
| `widget.h` | 窗口状态、控件、网络管理器、串口与计时器声明 |
| `widget.cpp` | 登录与触屏界面、水质数据、主机状态、气象、串口与远程控制、问答录音播放 |
| `widget.ui` | Qt Designer UI 定义 |
| `res.qrc` | 两张原有 Qt 界面背景 PNG 的资源引用 |

两张背景图随 Qt 资源文件一起参与界面构建。

## 4. 智能问答服务

`x86api_text_ai/after_api.py` 面向网页与小程序。核心入口包括 `normalize_question`、`normalize_water_data`、`normalize_history`、`make_user_context` 和 `call_deepseek`；路由集中在文件末段。`requirements.example.txt` 根据源码导入列出 FastAPI 与 uvicorn，作为独立环境的安装示例。

`smpapi_voice_assistant/smp_api.py` 面向现场 Qt 控制端。除文本上下文整理，还包括 `transcribe_wav_bytes`、Vosk 模型状态与惰性加载、回答清理、播报文本生成和健康/能力接口。`requirements.txt` 保留原有依赖版本范围，`.env.example` 展示环境变量名称。

两套服务均通过 HTTP 请求桥接配置的模型服务。密钥读取逻辑支持环境变量与本地文件，公开包使用空密钥示例，并通过忽略规则保护本地密钥文件。

## 5. 网页看板

`web_dashboard/dashboard.html` 集中包含 HTML、CSS 和 JavaScript，使用 Tailwind CSS、Apache ECharts、Font Awesome 与字体 CDN。网关、问答和摄像头地址通过界面配置保存到浏览器本地存储。

主要代码区域包括水质拉取、指标与图表更新、有限趋势历史、问答请求、摄像头展示及配置连通性检查。页面依赖配置的服务地址与浏览器网络环境；离线验证解析其中的 JavaScript 语法。

## 6. 微信小程序

| 路径 | 责任 |
| --- | --- |
| `miniprogram/app.js`、`app.json`、`app.wxss` | 应用初始化、页面与底部导航、全局样式 |
| `pages/auth/` | 本地账号注册与会话入口 |
| `pages/index/` | 水质首页、天气、网关配置与展示内容 |
| `pages/report/` | 指标分析、上下文与文本问答 |
| `pages/camera/` | 摄像头服务地址、预览入口与手动控制请求 |
| `pages/camera-view/` | 视频页面 |
| `pages/manual/` | 使用说明页面 |
| `utils/water-quality.js` | URL 归一化、请求、快照、缓存、指标、设备状态与报告构建 |
| `utils/auth.js` | 基于 `wx` 本地存储的账号与会话工具 |
| `project.config.example.json` | 可复制的开发者工具工程配置 |

小程序页面的 `.js`、`.json`、`.wxml`、`.wxss` 四类文件一起构成页面。问答、网关、控制与摄像头使用独立配置，联调时按对应服务分别设置。

## 7. 测试与文档

根目录 `tests/` 包含六个原有测试文件，检查 Wi-Fi 重连设置、传感器时间间隔、MQTT 主题、采集与发布解耦、芯片温度和 WROOM 引脚配置。它们通过文件读取检查源码契约，保持原路径后可直接复用。

`tools/check_public_package.py` 检查配置卫生、受限文件类型、Python/JSON/XML 解析、资源引用、文档链接和来源摘要。`tools/run_offline_checks.py` 在此基础上调用 Node 解析 JavaScript，并通过禁用网络的 Python 进程运行原有 pytest 测试。

| 文档 | 内容 |
| --- | --- |
| [ARCHITECTURE.md](ARCHITECTURE.md) | 数据流、MQTT、HTTP、串口与问答边界 |
| [BUILD_AND_TEST.md](BUILD_AND_TEST.md) | 工具链、独立环境、构建和离线检查 |
| [CONFIGURATION.md](CONFIGURATION.md) | 示例配置、环境变量和本地调整位置 |
| [VERIFICATION.md](VERIFICATION.md) | 实际命令、结果与验证范围 |
| [SOURCE_MANIFEST.json](SOURCE_MANIFEST.json) | 逐文件来源、复制状态与公开副本 SHA-256 |
| [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) | 第三方依赖、服务、模型与界面资源说明 |
