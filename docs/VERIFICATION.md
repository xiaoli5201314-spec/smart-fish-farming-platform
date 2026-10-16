# 验证记录

验证日期：2026-10-16。验证对象为本目录中的独立公开源码包。

环境：Windows PowerShell、Python `3.14.7`、Node.js `v24.19.0`、pytest `9.1.1`、Flask `3.1.2`、paho-mqtt `2.1.0`。

## 1. 执行入口

```powershell
python -B tools/check_public_package.py
python -B tools/run_offline_checks.py
python -B -m unittest discover -s tests -v
python -B tools/render_protocol_fixture.py
```

## 2. 实际结果

| 命令 / 检查 | 结果 |
| --- | --- |
| `check_public_package.py` | 发布检查通过，`0` 项问题 |
| `run_offline_checks.py` | `18 passed, 19 subtests passed`，退出码 `0` |
| 单独执行根目录 unittest | `Ran 7 tests`，`OK` |
| Python 内存语法检查 | `17` 个文件通过 |
| JavaScript 语法检查 | `11` 个 `.js` 文件及 `1` 组 HTML 内联脚本通过 |
| JSON | `12` 个文件解析通过 |
| Qt XML | `2` 个文件解析通过，资源引用有效 |
| 来源摘要 | `82` 个复制文件 SHA-256 校验通过 |
| 浏览器协议样例 | 原页面渲染 `3` 张池塘卡片，`0` 个页面脚本错误 |

原有测试覆盖 MQTT 主题与字段、发布间隔、跨午夜采集窗口、状态切换、Flask 输入校验、假 MQTT 发布订阅、前端未保存表单状态，以及固件的网络、时序、控制主题、温度和引脚契约。

## 3. 检查方式

| 范围 | 验证方式 |
| --- | --- |
| 发布文件与配置 | 扩展名与目录检查、空固件连接常量、示例 IP、凭据格式检测 |
| 来源 | 逐个检查来源清单中的公开副本 SHA-256 |
| Python | 内存编译；原有单元测试与 Flask 本地测试客户端 |
| JavaScript | Node `--check`，包含 HTML 内联脚本 |
| JSON / Qt XML | JSON 解析、XML 解析、`.qrc` 引用存在性 |
| 文档 | 相对链接存在性、模块与配置入口核对 |

离线统一入口在 Python 进程阻断网络操作，使用原有 `FakeClient` 和 Flask 本地测试客户端，临时目录位于 `.verification/`。

## 4. 界面样例

`render_protocol_fixture.py` 使用本机 Playwright / Chromium 渲染 `FishWaterSimulator/static/` 原页面，在浏览器请求拦截中返回内存协议样例。样例由 `simulator_core.py` 的状态和载荷函数生成，截图标注为 **protocol fixture**；演示标注在截图浏览器的 DOM 中设置。

截图：`docs/images/simulator-protocol-fixture.png`，尺寸 `1600 × 1200`。三个池塘的指标、表单、控制区和标注均已目视检查。

复现截图需要本地 Playwright Python 包及其 Chromium；脚本直接使用现有浏览器组件。浏览器全部页面请求由样例拦截器处理，应用服务不参与截图。

## 5. 来源与构建范围

来源清单记录 `82` 个复制文件，其中 `64` 个与原文件字节一致，`18` 个做了公开配置、示例地址及文案调整。新增文件包括文档、示例配置、依赖示例、检查工具和界面样例图。

本次实际执行覆盖软件协议回归、源码契约、语法与资源检查以及协议样例页面渲染。固件和 Qt 的目标编译、微信开发者工具编译及设备联调按 [BUILD_AND_TEST.md](BUILD_AND_TEST.md) 中的入口在相应环境执行。
