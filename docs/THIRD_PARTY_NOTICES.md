# 第三方依赖与资源说明

本包保留原工程中可选源文件的名称、模块边界、库引用和原有注释。依赖通过各自包管理器或公开页面引用，使用与再分发应遵循其上游许可证及服务条款。本说明不替第三方重新授权，也不将框架、模型或服务实现归为本项目原创。

## 固件与桌面依赖

| 依赖 | 源码位置 / 用途 | 交付方式 |
| --- | --- | --- |
| Espressif32 / Arduino-ESP32 | 全部 ESP32 固件的板卡与运行框架 | PlatformIO 工程声明 |
| PubSubClient，knolleary | 网关 MQTT 客户端 | `platformio.ini` 的库依赖 |
| Adafruit GFX Library | OLED 图形绘制 | `platformio.ini` 的库依赖 |
| Adafruit SSD1306 | OLED 驱动 | `platformio.ini` 的库依赖 |
| Qt Core / GUI / Widgets / SerialPort / Network | 现场控制应用 | `test.pro` 与源码 include |

## Python 依赖

| 依赖 | 用途 | 声明位置 |
| --- | --- | --- |
| Flask | 仿真器 HTTP 应用与测试客户端 | `FishWaterSimulator/requirements.txt` |
| paho-mqtt | 仿真器 MQTT 客户端 | 同上 |
| pytest | 原有测试的统一执行 | `requirements-test.txt` |
| FastAPI / uvicorn | 两套问答 API | 各服务依赖文件 |
| python-multipart | 音频表单上传 | `smpapi_voice_assistant/requirements.txt` |
| Vosk | WAV 语音识别 | 同上 |
| edge-tts | 语音播报相关工具链 | 同上及 Qt 调用代码 |

Vosk 模型是独立资源，其模型包的许可证与来源说明应随实际使用的模型核对。模型 API 由配置的服务商提供，账号、配额、模型标识及数据处理遵循对应服务规则。

## 网页与公共服务

| 名称 | 原工程引用方式 |
| --- | --- |
| Tailwind CSS | 网页中的 CDN script |
| Apache ECharts | 网页中的 `echarts@5.5.0` CDN script |
| Font Awesome | 网页中的 CSS CDN |
| Inter / Google Fonts | 网页字体 CSS |
| Open-Meteo | Qt 与小程序天气请求 |
| MJPG Streamer | 小程序和看板配置的摄像头 URL 形状 |
| NTP 服务 | 固件中的校时地址 |
| DeepSeek 兼容 HTTP API | 两套 Python 问答服务的请求实现 |

CDN 内容与外部服务在使用时由相应来源提供，仓库保留名称和请求入口。依赖版本与授权应以各上游发行版为准。

## Qt 界面资源与源码许可

`smpqt_control/test/fishpond_bg_v1.png` 与 `fishpond_login_bg_v1.png` 是原工程 `res.qrc` 引用的界面背景资源，按原文件保留，文件摘要见 [SOURCE_MANIFEST.json](SOURCE_MANIFEST.json)。

源码、界面资源与第三方依赖分别适用各自的授权范围；本说明保留原引用和归属，不变更其许可。
