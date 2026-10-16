# 配置说明

公开源码中的 Wi-Fi 名称、密码、MQTT 地址及账号均为空值；原部署 IP 已换成 `192.0.2.10` 文档示例地址。服务使用的模型名称、设备路径、端口和第三方公共接口按原代码保留，联调时根据自己的环境配置。

## 1. 固件配置位置

| 工程 | 配置位置 |
| --- | --- |
| 三池塘 ESP32-S3 | 各工程 `include/pond_config.h` |
| WROOM-32UE | `include/pond_config.h`，池塘由 `POND_NUMBER` 选择 |
| 环境网关 | `include/environment_config.h` |
| 多参数水质网关 | `src/main.cpp` 顶部 `cfg` 命名空间 |
| 红外鱼口计数 | `src/main.cpp` 顶部匿名命名空间 |
| 执行器 | `src/main.cpp` 顶部 `cfg` 命名空间 |

连接常量：

```cpp
constexpr char kWifiSsid[] = "";
constexpr char kWifiPassword[] = "";
constexpr char kMqttBrokerHost[] = "";
constexpr char kMqttUsername[] = "";
constexpr char kMqttPassword[] = "";
```

在本地工程副本填写实际配置，保持发布版本为空。原固件直接读取这些常量，`.gitignore` 不会自动忽略已跟踪源码中的改动；发布检查会拒绝非空连接常量。

池塘标识、设备标识与 MQTT 主题保留为协议名称，例如 `pond_1`、`water_gateway_pond_1`，不对应个人账号。RS485 波特率、引脚、传感器地址和设备输出参数应按实际硬件核对。

## 2. 三池塘仿真器

复制示例后配置自己的测试 broker：

```powershell
Copy-Item FishWaterSimulator/simulator_config.example.json FishWaterSimulator/simulator_config.json
```

程序读取 `simulator_config.json`。环境变量优先于 JSON：

| 环境变量 | JSON 位置 | 公开默认值 |
| --- | --- | --- |
| `FISH_MQTT_HOST` | `mqtt.host` | `127.0.0.1` |
| `FISH_MQTT_PORT` | `mqtt.port` | `1883` |
| `FISH_MQTT_USERNAME` | `mqtt.username` | 空 |
| `FISH_MQTT_PASSWORD` | `mqtt.password` | 空 |
| `FISH_SIMULATOR_PORT` | `webPort` | `5057` |
| `FISH_SIMULATOR_NO_BROWSER` | 环境变量 | 设为 `1` 可关闭自动打开浏览器 |

发布间隔由仿真器设置与协议状态维护；示例 JSON 只列出实际读取的连接与 Web 端口配置。离线测试使用假客户端，既不需要复制配置，也不需要运行 broker。

## 3. 树莓派轻量显示

```powershell
Copy-Item raspi_water_display/config.example.json raspi_water_display/config.json
```

`gatewayUrl` 留空时，显示端返回等待数据状态。设置网关地址时可填写完整的 `/api/water/latest` 路径，程序也会为没有路径的地址补齐该路径。`pollIntervalMs` 默认 `2500`，程序将有效范围限制在 `1000` 至 `10000 ms`。

轻量显示的 `/api/config` 支持保存配置，`/api/config/test` 会向所填地址发出实际读取请求。本包离线验证只检查源码，不调用这些网络路由。

## 4. 文本问答服务

两套服务均提供 `.env.example`，复制为同目录 `.env`：

```powershell
Copy-Item x86api_text_ai/.env.example x86api_text_ai/.env
Copy-Item smpapi_voice_assistant/.env.example smpapi_voice_assistant/.env
```

| 配置 | 用途 |
| --- | --- |
| `DEEPSEEK_API_KEY` | 自己的模型服务密钥，公开示例为空 |
| `DEEPSEEK_API_URL` | 使用兼容请求结构的模型接口 URL |
| `DEEPSEEK_MODEL` | 所选服务支持的模型标识 |
| `DEEPSEEK_TIMEOUT` | 模型请求超时，单位秒 |
| `SMP_API_HOST` / `SMP_API_PORT` | 树莓派问答服务监听位置 |
| `SMP_ASSISTANT_NAME` | 树莓派问答的助手名称 |
| `SMP_VOSK_MODEL_PATH` | 本地 Vosk 中文模型目录 |

`api.example.invalid` 与 `YOUR_MODEL_ID` 是配置占位符。原程序的默认值作为源码记录保留，实际使用应提供与账号和服务匹配的 URL、模型及密钥。

**x86 配置读取顺序。** `after_api.py` 在模块导入时读取 URL 与超时；`.env` 是在读取密钥时加载。因此 URL、超时应在启动进程前通过环境变量提供，密钥与模型可以从本地 `.env` 读取。例如：

```powershell
$env:DEEPSEEK_API_URL = "https://api.example.invalid/chat/completions"
$env:DEEPSEEK_TIMEOUT = "40"
```

将示例 URL 换为自己的服务地址后再启动应用。树莓派服务的 `env()` 在使用配置时加载本地 `.env`，其读取时机与 x86 服务不同。

Vosk 的相对路径按进程当前目录解释。使用示例中的 `models/vosk-model-small-cn-0.22` 时从 `smpapi_voice_assistant/` 启动，或者改为本机的绝对路径。

## 5. 多端界面

**Qt。** 水质地址通过界面和 `QSettings` 保存。问答与录音相关端点、串口设备、声卡名称位于 `widget.cpp` 顶部。公开副本的演示输入统一为 `demo-example`，只是本地 UI 示例。Linux 串口和音频设备名称需与实际系统匹配。

**网页。** 配置界面保存网关、AI 与摄像头地址到浏览器本地存储；看板会向网关基地址追加 `/api/water/latest`，向 AI 基地址追加 `/api/ai/chat`。应区分基地址与完整接口地址。

**小程序。** 复制开发者工具配置：

```powershell
Copy-Item wx_mini_program/project.config.example.json wx_mini_program/project.config.json
```

示例使用 `touristappid`。自己的 AppID 只写入本地配置。网关请求使用 `water-quality.js` 的 URL 归一化规则，问答页维护完整的 `/api/ai/chat` 地址，摄像头页分别配置视频与控制端口。首页展示视频地址使用 `media.example.invalid` 占位符，可按自己的展示内容调整。

账号数据、会话、地址和水质快照保存在当前微信运行环境的本地存储中。更换测试环境或发布环境时，应使用对应环境自己的配置。

## 6. 连接边界

HTTP 控制和问答服务采用局域网联调配置，Qt 远程控制映射到串口命令，显示代理读取配置的网关 URL。监听地址与网络访问范围按自己的运行环境设置。构建与检查入口见 [BUILD_AND_TEST.md](BUILD_AND_TEST.md)。
