#include "widget.h"
#include "ui_widget.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QAbstractSocket>
#include <QHostAddress>
#include <QIODevice>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPainter>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QPixmap>
#include <QScopedPointer>
#include <QSerialPort>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>
#include <QVBoxLayout>

namespace {

constexpr uint32_t kPiUartBaud = 115200;
constexpr uint8_t kFrameHead = 0xAA;
constexpr uint8_t kFrameTail = 0xBB;
constexpr qint64 kWifiStatusRefreshMs = 8000;
constexpr uint8_t kCmdForward = 0x01;
constexpr uint8_t kCmdReverse = 0x02;
constexpr uint8_t kCmdStop = 0x03;
constexpr uint8_t kCmdLedOn = 0x04;
constexpr uint8_t kCmdLedOff = 0x05;
constexpr int kPumpGpioBcm = 17;

const char *kPiSerialPort = "/dev/serial0";
constexpr int kWeatherRefreshMs = 5 * 60 * 1000;
constexpr int kWaterRefreshMs = 5000;
constexpr int kVoiceRecordMaxMs = 8000;
constexpr quint16 kRemoteControlPort = 8090;
const char *kWaterDefaultApiEndpoint = "http://192.0.2.10/api/water/latest";
const char *kAssistantChatEndpoint = "http://127.0.0.1:8010/api/smp/chat";
const char *kVoiceApiEndpoint = "http://127.0.0.1:8010/api/smp/voice/chat";
const char *kVoiceAudioDevice = "plughw:CARD=UACDemoV10,DEV=0";
const char *kVoiceNaturalTtsRelativePath = "smpapi_voice_assistant/.venv/bin/edge-tts";
const char *kVoiceEdgeVoice = "zh-CN-XiaoxiaoNeural";

QString firstExecutablePath(const QStringList &candidates)
{
    for (const QString &candidateRaw : candidates) {
        const QString candidate = QDir::cleanPath(candidateRaw.trimmed());
        if (candidate.isEmpty()) {
            continue;
        }

        const QFileInfo info(candidate);
        if (info.exists() && info.isFile() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
    }

    return QString();
}

QString inferredUserHomeFromAppPath()
{
    const QString appDir = QDir::cleanPath(QCoreApplication::applicationDirPath());
    const QStringList parts = appDir.split(QLatin1Char('/'), QString::SkipEmptyParts);
    if (parts.size() >= 2 && parts.at(0) == QStringLiteral("home")) {
        return QStringLiteral("/home/%1").arg(parts.at(1));
    }

    return QDir::homePath();
}

struct WeatherCity
{
    const char *name;
    double latitude;
    double longitude;
};

const WeatherCity kWeatherCities[] = {
    {"\u7f8e\u5170\u533a", 20.0308, 110.3624},
    {"\u79c0\u82f1\u533a", 20.0308, 110.3286},
    {"\u9f99\u534e\u533a", 20.0081, 110.2937},
    {"\u743c\u5c71\u533a", 20.0039, 110.3547},
    {"\u6587\u660c\u5e02", 19.5433, 110.7975},
    {"\u743c\u6d77\u5e02", 19.2584, 110.4746},
    {"\u4e07\u5b81\u5e02", 18.7953, 110.3897},
    {"\u510b\u5dde\u5e02", 19.7385, 110.0075},
    {"\u4e1c\u65b9\u5e02", 19.5175, 109.5768},
    {"\u4e09\u4e9a\u5e02", 18.2528, 109.5119}
};

const WeatherCity &defaultWeatherCity()
{
    return kWeatherCities[0];
}

QString normalizeWeatherCityName(const QString &rawName)
{
    const QString name = rawName.trimmed();
    if (name == QStringLiteral("\u6d77\u53e3\u5e02") ||
        name == QStringLiteral("\u6d77\u53e3\u5e02\u7f8e\u5170\u533a")) {
        return QStringLiteral("\u7f8e\u5170\u533a");
    }
    if (name == QStringLiteral("\u6d77\u53e3\u5e02\u79c0\u82f1\u533a")) {
        return QStringLiteral("\u79c0\u82f1\u533a");
    }
    if (name == QStringLiteral("\u6d77\u53e3\u5e02\u9f99\u534e\u533a")) {
        return QStringLiteral("\u9f99\u534e\u533a");
    }
    if (name == QStringLiteral("\u6d77\u53e3\u5e02\u743c\u5c71\u533a")) {
        return QStringLiteral("\u743c\u5c71\u533a");
    }
    if (name == QStringLiteral("\u6587\u660c") ||
        name == QStringLiteral("\u6587\u660c\u5e02")) {
        return QStringLiteral("\u6587\u660c\u5e02");
    }
    if (name == QStringLiteral("\u743c\u6d77") ||
        name == QStringLiteral("\u743c\u6d77\u5e02")) {
        return QStringLiteral("\u743c\u6d77\u5e02");
    }
    if (name == QStringLiteral("\u4e07\u5b81") ||
        name == QStringLiteral("\u4e07\u5b81\u5e02")) {
        return QStringLiteral("\u4e07\u5b81\u5e02");
    }
    if (name == QStringLiteral("\u510b\u5dde") ||
        name == QStringLiteral("\u510b\u5dde\u5e02")) {
        return QStringLiteral("\u510b\u5dde\u5e02");
    }
    if (name == QStringLiteral("\u4e1c\u65b9") ||
        name == QStringLiteral("\u4e1c\u65b9\u5e02")) {
        return QStringLiteral("\u4e1c\u65b9\u5e02");
    }
    if (name == QStringLiteral("\u4e09\u4e9a") ||
        name == QStringLiteral("\u4e09\u4e9a\u5e02")) {
        return QStringLiteral("\u4e09\u4e9a\u5e02");
    }
    return name;
}

const WeatherCity *findWeatherCity(const QString &name)
{
    const QString normalizedName = normalizeWeatherCityName(name);
    for (const WeatherCity &city : kWeatherCities) {
        if (QString::fromUtf8(city.name) == normalizedName) {
            return &city;
        }
    }
    return nullptr;
}

void writeHttpJson(QTcpSocket *socket, int statusCode, const QJsonObject &payload)
{
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const QByteArray statusText = statusCode == 200 ? QByteArrayLiteral("OK")
                                : statusCode == 204 ? QByteArrayLiteral("No Content")
                                : statusCode == 400 ? QByteArrayLiteral("Bad Request")
                                : statusCode == 404 ? QByteArrayLiteral("Not Found")
                                                     : QByteArrayLiteral("Internal Server Error");

    QByteArray header;
    header.append("HTTP/1.1 ");
    header.append(QByteArray::number(statusCode));
    header.append(' ');
    header.append(statusText);
    header.append("\r\nContent-Type: application/json; charset=utf-8\r\n");
    header.append("Access-Control-Allow-Origin: *\r\n");
    header.append("Access-Control-Allow-Methods: GET, OPTIONS\r\n");
    header.append("Access-Control-Allow-Headers: Content-Type\r\n");
    header.append("Connection: close\r\n");
    header.append("Content-Length: ");
    header.append(QByteArray::number(body.size()));
    header.append("\r\n\r\n");

    socket->write(header);
    socket->write(body);
    socket->disconnectFromHost();
}

int boundedProgress(double value)
{
    return qBound(0, static_cast<int>(value + 0.5), 100);
}

double numberBefore(const QString &text, const QChar separator, bool *ok)
{
    return text.section(separator, 0, 0).trimmed().toDouble(ok);
}

bool jsonNumberValue(const QJsonValue &value, double *number)
{
    if (value.isDouble()) {
        *number = value.toDouble();
        return true;
    }

    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().trimmed().toDouble(&ok);
        if (ok) {
            *number = parsed;
            return true;
        }
    }

    return false;
}

bool jsonObjectNumber(const QJsonObject &object, const QStringList &keys, double *number)
{
    for (const QString &key : keys) {
        if (jsonNumberValue(object.value(key), number)) {
            return true;
        }
    }

    return false;
}

QString pumpGpioDirectory()
{
    return QStringLiteral("/sys/class/gpio/gpio%1").arg(kPumpGpioBcm);
}

bool writeTextFile(const QString &path, const QByteArray &value, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        }
        return false;
    }

    QByteArray payload = value;
    if (!payload.endsWith('\n')) {
        payload.append('\n');
    }

    const qint64 written = file.write(payload);
    if (written != payload.size()) {
        if (error) {
            *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        }
        return false;
    }

    return true;
}

QJsonObject sensorObjectByAddress(const QJsonArray &sensors, const QString &address)
{
    for (const QJsonValue &value : sensors) {
        const QJsonObject sensor = value.toObject();
        if (sensor.value(QStringLiteral("address")).toString() == address) {
            return sensor;
        }
    }

    return QJsonObject();
}

int onlineSensorCount(const QJsonArray &sensors)
{
    int count = 0;
    for (const QJsonValue &value : sensors) {
        if (value.toObject().value(QStringLiteral("online")).toBool(false)) {
            ++count;
        }
    }

    return count;
}

} // namespace

Widget::Widget(QWidget *parent)
    : QWidget(parent),
      ui(new Ui::Widget),
      getSysTimeTimer(nullptr),
      deviceStatusTimer(nullptr),
      weatherTimer(nullptr),
      waterTimer(nullptr),
      voiceRecordStopTimer(nullptr),
      weatherManager(nullptr),
      assistantManager(nullptr),
      waterManager(nullptr),
      remoteControlServer(nullptr),
      serial(nullptr),
      rootStack(nullptr),
      contentStack(nullptr),
      labelDate(nullptr),
      labelTime(nullptr),
      loginTitleLabel(nullptr),
      loginClockLabel(nullptr),
      serialStatusLabel(nullptr),
      serialDetailLabel(nullptr),
      motionStatusLabel(nullptr),
      ledStatusLabel(nullptr),
      pumpStatusLabel(nullptr),
      lastCommandLabel(nullptr),
      piTempLabel(nullptr),
      memoryUsageLabel(nullptr),
      cpuUsageLabel(nullptr),
      networkSpeedLabel(nullptr),
      wifiIpLabel(nullptr),
      weatherStatusLabel(nullptr),
      weatherTempLabel(nullptr),
      weatherTextLabel(nullptr),
      weatherHumidityLabel(nullptr),
      weatherWindLabel(nullptr),
      weatherRainLabel(nullptr),
      weatherUpdateLabel(nullptr),
      weatherTitleLabel(nullptr),
      voiceStatusLabel(nullptr),
      waterStatusLabel(nullptr),
      waterGatewayLabel(nullptr),
      waterUpdateLabel(nullptr),
      waterOnlineLabel(nullptr),
      waterTempLabel(nullptr),
      waterPhLabel(nullptr),
      waterCodLabel(nullptr),
      waterAmmoniaLabel(nullptr),
      waterNitriteLabel(nullptr),
      waterNitrateLabel(nullptr),
      waterConductivityLabel(nullptr),
      waterTdsLabel(nullptr),
      waterSalinityLabel(nullptr),
      piTempBar(nullptr),
      memoryUsageBar(nullptr),
      cpuUsageBar(nullptr),
      networkSpeedBar(nullptr),
      waterTempBar(nullptr),
      waterPhBar(nullptr),
      waterCodBar(nullptr),
      waterAmmoniaBar(nullptr),
      waterNitriteBar(nullptr),
      waterNitrateBar(nullptr),
      waterConductivityBar(nullptr),
      waterTdsBar(nullptr),
      waterSalinityBar(nullptr),
      operatorLabel(nullptr),
      loginHintLabel(nullptr),
      accountEdit(nullptr),
      passwordEdit(nullptr),
      waterApiEdit(nullptr),
      activeInputEdit(nullptr),
      voiceQuestionView(nullptr),
      voiceAnswerView(nullptr),
      navWaterButton(nullptr),
      navSettingButton(nullptr),
      navRecordButton(nullptr),
      navWeatherButton(nullptr),
      weatherCityButton(nullptr),
      voiceStartButton(nullptr),
      voiceStopButton(nullptr),
      voiceReplayButton(nullptr),
      voiceRecordProcess(nullptr),
      voicePlayProcess(nullptr),
      currentUser(QStringLiteral("\u8bbf\u5ba2\u7528\u6237")),
      cachedWifiIp(QStringLiteral("Disconnected")),
      lastVoiceTranscript(),
      lastVoiceAnswer(),
      lastVoiceTtsText(),
      weatherLocationName(QString::fromUtf8(defaultWeatherCity().name)),
      waterApiUrlValue(QString::fromLatin1(kWaterDefaultApiEndpoint)),
      weatherLatitude(defaultWeatherCity().latitude),
      weatherLongitude(defaultWeatherCity().longitude),
      lastCpuTotal(0),
      lastCpuIdle(0),
      lastNetBytes(0),
      lastNetSampleMs(0),
      lastWifiStatusRefreshMs(0),
      voiceRecordingActive(false),
      voiceSubmitAfterStop(false),
      voiceRequestRunning(false)
{
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("\u9c7c\u5858\u667a\u80fd\u7ba1\u7406\u5e73\u53f0"));
    setFixedSize(1024, 600);

    loadWeatherLocation();
    loadWaterApiUrl();
    buildUi();
    initSerial();
    initPumpGpio();
    startRemoteControlServer();

    getSysTimeTimer = new QTimer(this);
    connect(getSysTimeTimer, &QTimer::timeout, this, &Widget::time_reflash);
    getSysTimeTimer->start(500);
    time_reflash();

    deviceStatusTimer = new QTimer(this);
    connect(deviceStatusTimer, &QTimer::timeout, this, &Widget::updateDeviceStatus);
    deviceStatusTimer->start(1000);
    updateDeviceStatus();

    weatherManager = new QNetworkAccessManager(this);
    connect(weatherManager, &QNetworkAccessManager::finished, this, &Widget::handleWeatherReply);
    assistantManager = new QNetworkAccessManager(this);
    waterManager = new QNetworkAccessManager(this);
    weatherTimer = new QTimer(this);
    connect(weatherTimer, &QTimer::timeout, this, &Widget::requestWeather);
    weatherTimer->start(kWeatherRefreshMs);
    requestWeather();

    waterTimer = new QTimer(this);
    connect(waterTimer, &QTimer::timeout, this, &Widget::requestWaterData);
    waterTimer->start(kWaterRefreshMs);
    requestWaterData();

    voiceRecordStopTimer = new QTimer(this);
    voiceRecordStopTimer->setSingleShot(true);
    connect(voiceRecordStopTimer, &QTimer::timeout, this, [this]() {
        if (voiceRecordingActive) {
            stopVoiceRecording(true);
        }
    });

    voiceRecordProcess = new QProcess(this);
    connect(voiceRecordProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int, QProcess::ExitStatus) {
                const bool shouldSubmit = voiceSubmitAfterStop;
                voiceRecordingActive = false;
                voiceSubmitAfterStop = false;
                if (voiceRecordStopTimer) {
                    voiceRecordStopTimer->stop();
                }
                updateVoiceUiState();
                if (shouldSubmit) {
                    submitVoiceAudio();
                }
            });

    voicePlayProcess = new QProcess(this);
    connect(voicePlayProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int, QProcess::ExitStatus) {
                updateVoiceUiState();
            });

}

void Widget::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    const bool showingLoginPage = rootStack && rootStack->currentIndex() == 0;
    const QPixmap &background = showingLoginPage ? loginBackgroundPixmap : appBackgroundPixmap;
    if (background.isNull()) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawPixmap(rect(), background);
}

void Widget::buildUi()
{
    setObjectName(QStringLiteral("appRoot"));
    loginBackgroundPixmap.load(QStringLiteral(":/pics/fishpond_login_bg_v1.png"));
    appBackgroundPixmap.load(QStringLiteral(":/pics/fishpond_bg_v1.png"));

    setStyleSheet(QStringLiteral(R"(
        QWidget {
            font-family: "Microsoft YaHei", "Noto Sans CJK SC", "WenQuanYi Micro Hei";
            color: #EAFBF4;
        }
        QWidget#appRoot {
            background: #07161A;
        }
        QWidget#loginPage, QWidget#appPage {
            background: transparent;
        }
        QFrame#loginPanel, QFrame#card, QFrame#sideBar {
            border-radius: 22px;
            background: rgba(6, 31, 37, 210);
            border: 1px solid rgba(125, 235, 207, 58);
        }
        QFrame#weatherHero {
            border-radius: 28px;
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 rgba(36, 211, 166, 78), stop:0.52 rgba(43, 140, 255, 78), stop:1 rgba(6, 31, 37, 210));
            border: 1px solid rgba(174, 255, 232, 72);
        }
        QFrame#weatherMetric, QFrame#forecastCard {
            border-radius: 18px;
            background: rgba(255, 255, 255, 24);
            border: 1px solid rgba(174, 255, 232, 46);
        }
        QFrame#waterHero {
            border-radius: 24px;
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 rgba(36, 211, 166, 74), stop:0.5 rgba(43, 140, 255, 70), stop:1 rgba(6, 31, 37, 205));
            border: 1px solid rgba(174, 255, 232, 70);
        }
        QFrame#waterMetric {
            border-radius: 18px;
            background: rgba(255, 255, 255, 23);
            border: 1px solid rgba(174, 255, 232, 42);
        }
        QFrame#sideBar {
            border-radius: 0px;
            background: #061B20;
            border-right: 1px solid rgba(125, 235, 207, 48);
        }
        QLabel#heroTitle {
            font-size: 46px;
            font-weight: 800;
            color: #FFFFFF;
        }
        QLabel#loginClock {
            border-radius: 18px;
            padding: 10px 20px;
            background: rgba(78, 230, 186, 30);
            color: #D6FFF4;
            font-size: 23px;
            font-weight: 800;
        }
        QLabel#pageTitle {
            font-size: 30px;
            font-weight: 800;
            color: #F7FFFA;
        }
        QLabel#sectionTitle {
            font-size: 25px;
            font-weight: 800;
            color: #F7FFFA;
        }
        QLabel#caption {
            font-size: 15px;
            color: #8FCBC0;
        }
        QLabel#valueLabel {
            font-size: 27px;
            font-weight: 800;
            color: #4EE6BA;
        }
        QLabel#timeCardValue {
            font-size: 28px;
            font-weight: 800;
            color: #4EE6BA;
        }
        QLabel#weatherTemp {
            font-size: 68px;
            font-weight: 900;
            color: #FFFFFF;
        }
        QLabel#weatherText {
            font-size: 28px;
            font-weight: 800;
            color: #D6FFF4;
        }
        QLabel#weatherMetricValue {
            font-size: 26px;
            font-weight: 800;
            color: #FFFFFF;
        }
        QLabel#waterMetricValue {
            font-size: 24px;
            font-weight: 900;
            color: #FFFFFF;
        }
        QLabel#waterSummary {
            font-size: 20px;
            font-weight: 800;
            color: #EAFBF4;
        }
        QLabel#forecastText {
            font-size: 17px;
            font-weight: 800;
            color: #EAFBF4;
        }
        QLabel#pillLabel {
            border-radius: 14px;
            padding: 8px 12px;
            background: rgba(78, 230, 186, 30);
            color: #BDFBE8;
            font-size: 16px;
            font-weight: 700;
        }
        QLineEdit {
            min-height: 50px;
            border-radius: 16px;
            padding: 0 18px;
            background: rgba(5, 21, 26, 220);
            border: 1px solid rgba(115, 220, 197, 90);
            color: #FFFFFF;
            font-size: 18px;
        }
        QLineEdit:focus {
            border: 1px solid #4EE6BA;
        }
        QPushButton {
            min-height: 54px;
            border: none;
            border-radius: 18px;
            padding: 0 18px;
            font-size: 20px;
            font-weight: 800;
        }
        QPushButton#primaryButton {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #24D3A6, stop:1 #2B8CFF);
            color: #062027;
        }
        QPushButton#secondaryButton {
            background: rgba(255, 255, 255, 24);
            color: #D8FFF4;
            border: 1px solid rgba(125, 235, 207, 55);
        }
        QPushButton#cityButton {
            min-height: 60px;
            background: rgba(255, 255, 255, 22);
            color: #D8FFF4;
            border: 1px solid rgba(125, 235, 207, 48);
        }
        QPushButton#cityButton[active="true"] {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #24D3A6, stop:1 #2B8CFF);
            color: #062027;
        }
        QPushButton#dangerButton {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #FF6B6B, stop:1 #FFB86B);
            color: #210B0B;
        }
        QPushButton#lightButton {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #FFE08A, stop:1 #35E0B3);
            color: #172018;
        }
        QPushButton#navButton {
            text-align: center;
            padding-left: 18px;
            padding-right: 18px;
            background: transparent;
            color: #91CFC4;
            border-radius: 16px;
        }
        QPushButton#navButton[active="true"] {
            background: rgba(78, 230, 186, 42);
            color: #FFFFFF;
            border: 1px solid rgba(78, 230, 186, 90);
        }
        QPushButton:pressed {
            padding-top: 2px;
            background: #38CFA7;
        }
        QPlainTextEdit {
            border-radius: 18px;
            padding: 12px;
            background: rgba(3, 16, 21, 215);
            border: 1px solid rgba(125, 235, 207, 50);
            color: #C9F8ED;
            font-size: 16px;
        }
        QProgressBar {
            min-height: 12px;
            max-height: 12px;
            border: none;
            border-radius: 6px;
            background: rgba(255, 255, 255, 28);
            text-align: center;
            color: transparent;
        }
        QProgressBar::chunk {
            border-radius: 6px;
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #24D3A6, stop:0.55 #2B8CFF, stop:1 #FFE08A);
        }
    )"));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootStack = new QStackedWidget(this);
    rootLayout->addWidget(rootStack);
    rootStack->addWidget(createLoginPage());
    rootStack->addWidget(createMainShell());
    refreshBackground();
}

QWidget *Widget::createLoginPage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("loginPage"));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 34, 0, 34);
    layout->setSpacing(18);

    loginTitleLabel = new QLabel(QStringLiteral("\u6c34\u8d28\u68c0\u6d4b\u5e73\u53f0"), page);
    loginTitleLabel->setObjectName(QStringLiteral("heroTitle"));
    loginTitleLabel->setAlignment(Qt::AlignCenter);
    loginTitleLabel->setWordWrap(true);

    loginClockLabel = new QLabel(page);
    loginClockLabel->setObjectName(QStringLiteral("loginClock"));
    loginClockLabel->setAlignment(Qt::AlignCenter);
    loginClockLabel->setFixedWidth(430);

    auto *loginPanel = new QFrame(page);
    loginPanel->setObjectName(QStringLiteral("loginPanel"));
    loginPanel->setFixedWidth(430);
    auto *panelLayout = new QVBoxLayout(loginPanel);
    panelLayout->setContentsMargins(34, 30, 34, 30);
    panelLayout->setSpacing(20);

    accountEdit = new QLineEdit(loginPanel);
    accountEdit->setPlaceholderText(QStringLiteral("\u8bf7\u8f93\u5165\u8d26\u53f7"));
    accountEdit->setText(QStringLiteral("demo-example"));
    accountEdit->setAttribute(Qt::WA_AcceptTouchEvents, true);
    accountEdit->setProperty("touchKeyboardEnabled", true);
    accountEdit->setProperty("touchKeyboardTitle", QStringLiteral("\u8f93\u5165\u8d26\u53f7"));
    accountEdit->installEventFilter(this);
    passwordEdit = new QLineEdit(loginPanel);
    passwordEdit->setPlaceholderText(QStringLiteral("\u8bf7\u8f93\u5165\u5bc6\u7801"));
    passwordEdit->setEchoMode(QLineEdit::Password);
    passwordEdit->setText(QStringLiteral("demo-example"));
    passwordEdit->setAttribute(Qt::WA_AcceptTouchEvents, true);
    passwordEdit->setProperty("touchKeyboardEnabled", true);
    passwordEdit->setProperty("touchKeyboardTitle", QStringLiteral("\u8f93\u5165\u5bc6\u7801"));
    passwordEdit->installEventFilter(this);

    auto *loginButton = createActionButton(QStringLiteral("\u767b\u5f55\u7cfb\u7edf"), QStringLiteral("primaryButton"));
    auto *demoButton = createActionButton(QStringLiteral("\u4f53\u9a8c\u6f14\u793a"), QStringLiteral("secondaryButton"));

    connect(loginButton, &QPushButton::clicked, this, &Widget::handleLogin);
    connect(passwordEdit, &QLineEdit::returnPressed, this, &Widget::handleLogin);
    connect(demoButton, &QPushButton::clicked, this, [this]() {
        accountEdit->setText(QStringLiteral("demo-example"));
        passwordEdit->setText(QStringLiteral("demo-example"));
        handleLogin();
    });

    panelLayout->addSpacing(6);
    panelLayout->addWidget(accountEdit);
    panelLayout->addSpacing(14);
    panelLayout->addWidget(passwordEdit);
    panelLayout->addSpacing(18);
    panelLayout->addWidget(loginButton);
    panelLayout->addSpacing(14);
    panelLayout->addWidget(demoButton);

    layout->addStretch(1);
    layout->addWidget(loginTitleLabel, 0, Qt::AlignHCenter);
    layout->addWidget(loginClockLabel, 0, Qt::AlignHCenter);
    layout->addSpacing(8);
    layout->addWidget(loginPanel, 0, Qt::AlignHCenter);
    layout->addStretch(1);
    return page;
}

QWidget *Widget::createMainShell()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("appPage"));

    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *sideBar = new QFrame(page);
    sideBar->setObjectName(QStringLiteral("sideBar"));
    sideBar->setFixedWidth(238);
    auto *sideLayout = new QVBoxLayout(sideBar);
    sideLayout->setContentsMargins(22, 26, 22, 22);
    sideLayout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("\u5bfc\u822a\u83dc\u5355"), sideBar);
    title->setObjectName(QStringLiteral("pageTitle"));
    title->setText(QStringLiteral("\u6c34\u8d28\u667a\u80fd\u68c0\u6d4b"));
    title->setAlignment(Qt::AlignCenter);
    title->setWordWrap(true);

    serialStatusLabel = new QLabel(QStringLiteral("\u4e32\u53e3\u8fde\u63a5\u72b6\u6001\u5c06\u5728\u8fdb\u5165\u7cfb\u7edf\u540e\u81ea\u52a8\u68c0\u6d4b"), sideBar);
    serialStatusLabel->setObjectName(QStringLiteral("pillLabel"));
    serialStatusLabel->setAlignment(Qt::AlignCenter);
    serialStatusLabel->setWordWrap(true);

    navWaterButton = createNavButton(QStringLiteral("\u6c34\u8d28\u6570\u636e"));
    navSettingButton = createNavButton(QStringLiteral("\u8bbe\u5907\u72b6\u6001"));
    navRecordButton = createNavButton(QStringLiteral("\u8bed\u97f3\u52a9\u624b"));
    navWeatherButton = createNavButton(QStringLiteral("\u5929\u6c14\u60c5\u51b5"));

    connect(navWaterButton, &QPushButton::clicked, this, [this]() {
        showContentPage(0, navWaterButton);
    });
    connect(navSettingButton, &QPushButton::clicked, this, [this]() {
        showContentPage(1, navSettingButton);
    });
    connect(navRecordButton, &QPushButton::clicked, this, [this]() {
        showContentPage(2, navRecordButton);
    });
    connect(navWeatherButton, &QPushButton::clicked, this, [this]() {
        showContentPage(3, navWeatherButton);
    });

    auto *logoutButton = createActionButton(QStringLiteral("\u9000\u51fa\u767b\u5f55"), QStringLiteral("secondaryButton"));
    connect(logoutButton, &QPushButton::clicked, this, [this]() {
        rootStack->setCurrentIndex(0);
        refreshBackground();
    });

    sideLayout->addWidget(title);
    sideLayout->addSpacing(6);
    sideLayout->addWidget(serialStatusLabel);
    sideLayout->addSpacing(10);
    sideLayout->addWidget(navWaterButton);
    sideLayout->addWidget(navSettingButton);
    sideLayout->addWidget(navRecordButton);
    sideLayout->addWidget(navWeatherButton);
    sideLayout->addStretch();
    sideLayout->addWidget(logoutButton);

    contentStack = new QStackedWidget(page);
    contentStack->addWidget(createWaterPage());
    contentStack->addWidget(createSettingPage());
    contentStack->addWidget(createVoicePage());
    contentStack->addWidget(createWeatherPage());

    layout->addWidget(sideBar);
    layout->addWidget(contentStack);
    setActiveNav(navWaterButton);
    return page;
}

QWidget *Widget::createWaterPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 20, 28, 20);
    layout->setSpacing(12);

    auto *topRow = new QHBoxLayout();
    auto *title = new QLabel(QStringLiteral("\u6c34\u8d28\u6570\u636e"), page);
    title->setObjectName(QStringLiteral("pageTitle"));
    waterStatusLabel = new QLabel(QStringLiteral("\u7b49\u5f85\u8fde\u63a5"), page);
    waterStatusLabel->setObjectName(QStringLiteral("pillLabel"));
    waterStatusLabel->setAlignment(Qt::AlignCenter);
    topRow->addWidget(title);
    topRow->addStretch();
    topRow->addWidget(waterStatusLabel);

    auto *inputRow = new QHBoxLayout();
    inputRow->setSpacing(10);
    waterApiEdit = new QLineEdit(page);
    waterApiEdit->setText(waterApiUrlValue);
    waterApiEdit->setPlaceholderText(QStringLiteral("http://\u6c34\u8d28\u7f51\u5173IP/api/water/latest"));
    waterApiEdit->setAttribute(Qt::WA_AcceptTouchEvents, true);
    waterApiEdit->setProperty("touchKeyboardEnabled", true);
    waterApiEdit->setProperty("touchKeyboardTitle", QStringLiteral("\u6c34\u8d28\u63a5\u53e3\u5730\u5740"));
    waterApiEdit->installEventFilter(this);
    auto *saveButton = createActionButton(QStringLiteral("\u4fdd\u5b58\u63a5\u53e3"), QStringLiteral("secondaryButton"));
    auto *refreshButton = createActionButton(QStringLiteral("\u5237\u65b0\u6570\u636e"), QStringLiteral("primaryButton"));
    connect(saveButton, &QPushButton::clicked, this, [this]() {
        waterApiUrlValue = normalizedWaterApiUrl(waterApiEdit ? waterApiEdit->text() : QString());
        if (waterApiEdit) {
            waterApiEdit->setText(waterApiUrlValue);
        }
        saveWaterApiUrl();
        requestWaterData();
    });
    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        waterApiUrlValue = normalizedWaterApiUrl(waterApiEdit ? waterApiEdit->text() : QString());
        if (waterApiEdit) {
            waterApiEdit->setText(waterApiUrlValue);
        }
        requestWaterData();
    });
    inputRow->addWidget(waterApiEdit, 1);
    inputRow->addWidget(saveButton);
    inputRow->addWidget(refreshButton);

    auto *hero = new QFrame(page);
    hero->setObjectName(QStringLiteral("waterHero"));
    auto *heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(20, 16, 20, 16);
    heroLayout->setSpacing(12);

    auto makeSummaryCard = [this, hero](const QString &captionText, QLabel **valueTarget, const QString &initialValue) {
        auto *card = new QFrame(hero);
        card->setObjectName(QStringLiteral("waterMetric"));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 12, 14, 12);
        cardLayout->setSpacing(6);
        auto *caption = createCaption(captionText);
        auto *value = new QLabel(initialValue, card);
        value->setObjectName(QStringLiteral("waterMetricValue"));
        value->setAlignment(Qt::AlignCenter);
        value->setWordWrap(true);
        cardLayout->addWidget(caption);
        cardLayout->addWidget(value, 1);
        if (valueTarget) {
            *valueTarget = value;
        }
        return card;
    };

    heroLayout->addWidget(makeSummaryCard(QStringLiteral("\u7f51\u5173"), &waterGatewayLabel, QStringLiteral("--")), 2);
    heroLayout->addWidget(makeSummaryCard(QStringLiteral("\u66f4\u65b0\u65f6\u95f4"), &waterUpdateLabel, QStringLiteral("--:--:--")), 2);
    heroLayout->addWidget(makeSummaryCard(QStringLiteral("\u5728\u7ebf\u63a2\u5934"), &waterOnlineLabel, QStringLiteral("-- / 5")), 1);

    auto *metricGrid = new QGridLayout();
    metricGrid->setSpacing(10);
    auto makeMetricCard = [this, page](const QString &captionText,
                                       QLabel **valueTarget,
                                       QProgressBar **barTarget,
                                       const QString &initialValue) {
        auto *card = new QFrame(page);
        card->setObjectName(QStringLiteral("waterMetric"));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 12, 14, 12);
        cardLayout->setSpacing(7);
        auto *caption = createCaption(captionText);
        auto *value = new QLabel(initialValue, card);
        value->setObjectName(QStringLiteral("waterMetricValue"));
        value->setAlignment(Qt::AlignCenter);
        auto *bar = new QProgressBar(card);
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setTextVisible(false);
        cardLayout->addWidget(caption);
        cardLayout->addWidget(value, 1);
        cardLayout->addWidget(bar);
        if (valueTarget) {
            *valueTarget = value;
        }
        if (barTarget) {
            *barTarget = bar;
        }
        return card;
    };

    metricGrid->addWidget(makeMetricCard(QStringLiteral("\u6c34\u6e29"), &waterTempLabel, &waterTempBar, QStringLiteral("-- \u2103")), 0, 0);
    metricGrid->addWidget(makeMetricCard(QStringLiteral("pH"), &waterPhLabel, &waterPhBar, QStringLiteral("-- pH")), 0, 1);
    metricGrid->addWidget(makeMetricCard(QStringLiteral("COD"), &waterCodLabel, &waterCodBar, QStringLiteral("-- mg/L")), 0, 2);
    metricGrid->addWidget(makeMetricCard(QStringLiteral("\u6c28\u6c2e"), &waterAmmoniaLabel, &waterAmmoniaBar, QStringLiteral("-- ppm")), 1, 0);
    metricGrid->addWidget(makeMetricCard(QStringLiteral("\u4e9a\u785d\u9178\u6839"), &waterNitriteLabel, &waterNitriteBar, QStringLiteral("-- ppm")), 1, 1);
    metricGrid->addWidget(makeMetricCard(QStringLiteral("\u785d\u9178\u6839"), &waterNitrateLabel, &waterNitrateBar, QStringLiteral("-- ppm")), 1, 2);
    metricGrid->addWidget(makeMetricCard(QStringLiteral("\u7535\u5bfc\u7387"), &waterConductivityLabel, &waterConductivityBar, QStringLiteral("-- uS/cm")), 2, 0);
    metricGrid->addWidget(makeMetricCard(QStringLiteral("TDS"), &waterTdsLabel, &waterTdsBar, QStringLiteral("-- ppm")), 2, 1);
    metricGrid->addWidget(makeMetricCard(QStringLiteral("\u76d0\u5ea6"), &waterSalinityLabel, &waterSalinityBar, QStringLiteral("-- PPT")), 2, 2);

    layout->addLayout(topRow);
    layout->addLayout(inputRow);
    layout->addWidget(hero);
    layout->addLayout(metricGrid, 1);
    return page;
}

QWidget *Widget::createControlPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(16);

    auto *topRow = new QHBoxLayout();
    auto *titleBox = new QVBoxLayout();
    auto *title = new QLabel(QStringLiteral("\u4f5c\u4e1a\u63a7\u5236"), page);
    title->setObjectName(QStringLiteral("pageTitle"));
    titleBox->addWidget(title);
    operatorLabel = new QLabel(QStringLiteral("\u5f53\u524d\u64cd\u4f5c\u4eba\uff1a\u7cfb\u7edf\u7528\u6237"), page);
    operatorLabel->setObjectName(QStringLiteral("pillLabel"));
    topRow->addLayout(titleBox);
    topRow->addStretch();
    topRow->addWidget(operatorLabel);

    auto *statusGrid = new QGridLayout();
    statusGrid->setSpacing(14);

    auto makeStatusCard = [this, page](const QString &titleText, QLabel **valueLabel, const QString &initialValue) {
        auto *card = new QFrame(page);
        card->setObjectName(QStringLiteral("card"));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(20, 16, 20, 16);
        cardLayout->setSpacing(8);
        auto *title = createCaption(titleText);
        auto *value = new QLabel(initialValue, card);
        value->setObjectName(QStringLiteral("valueLabel"));
        value->setWordWrap(true);
        cardLayout->addWidget(title);
        cardLayout->addWidget(value);
        *valueLabel = value;
        return card;
    };

    statusGrid->addWidget(makeStatusCard(QStringLiteral("\u4e32\u53e3\u901a\u4fe1"), &serialDetailLabel, QStringLiteral("\u7b49\u5f85\u68c0\u6d4b")), 0, 0);
    statusGrid->addWidget(makeStatusCard(QStringLiteral("\u8fd0\u52a8\u72b6\u6001"), &motionStatusLabel, QStringLiteral("\u5df2\u505c\u6b62")), 0, 1);
    statusGrid->addWidget(makeStatusCard(QStringLiteral("\u8865\u5149\u72b6\u6001"), &ledStatusLabel, QStringLiteral("\u5df2\u5173\u95ed")), 0, 2);
    statusGrid->addWidget(makeStatusCard(QStringLiteral("\u6700\u8fd1\u6307\u4ee4"), &lastCommandLabel, QStringLiteral("\u6682\u65e0\u6307\u4ee4")), 0, 3);

    auto *mainRow = new QHBoxLayout();
    mainRow->setSpacing(16);

    auto *motorCard = new QFrame(page);
    motorCard->setObjectName(QStringLiteral("card"));
    auto *motorLayout = new QVBoxLayout(motorCard);
    motorLayout->setContentsMargins(24, 22, 24, 22);
    motorLayout->setSpacing(16);
    auto *motorTitle = new QLabel(QStringLiteral("\u6e14\u7f51\u63a7\u5236"), motorCard);
    motorTitle->setObjectName(QStringLiteral("sectionTitle"));
    motorTitle->setAlignment(Qt::AlignCenter);
    auto *forwardButton = createActionButton(QStringLiteral("\u6e14\u7f51\u524d\u8fdb"), QStringLiteral("primaryButton"));
    auto *stopButton = createActionButton(QStringLiteral("\u6e14\u7f51\u505c\u6b62"), QStringLiteral("dangerButton"));
    auto *reverseButton = createActionButton(QStringLiteral("\u6e14\u7f51\u540e\u9000"), QStringLiteral("secondaryButton"));
    forwardButton->setMinimumHeight(82);
    stopButton->setMinimumHeight(82);
    reverseButton->setMinimumHeight(82);
    connect(forwardButton, &QPushButton::clicked, this, &Widget::handleMotorForward);
    connect(stopButton, &QPushButton::clicked, this, &Widget::handleMotorStop);
    connect(reverseButton, &QPushButton::clicked, this, &Widget::handleMotorReverse);
    motorLayout->addStretch();
    motorLayout->addWidget(motorTitle);
    motorLayout->addSpacing(14);
    motorLayout->addWidget(forwardButton);
    motorLayout->addSpacing(4);
    motorLayout->addWidget(stopButton);
    motorLayout->addSpacing(4);
    motorLayout->addWidget(reverseButton);
    motorLayout->addStretch();

    auto *rightColumn = new QVBoxLayout();
    rightColumn->setSpacing(16);

    auto *lightCard = new QFrame(page);
    lightCard->setObjectName(QStringLiteral("card"));
    auto *lightLayout = new QVBoxLayout(lightCard);
    lightLayout->setContentsMargins(24, 22, 24, 22);
    lightLayout->setSpacing(16);
    auto *lightTitle = new QLabel(QStringLiteral("\u706f\u5149\u63a7\u5236"), lightCard);
    lightTitle->setObjectName(QStringLiteral("sectionTitle"));
    lightTitle->setAlignment(Qt::AlignCenter);
    auto *ledOpenButton = createActionButton(QStringLiteral("\u6253\u5f00\u8865\u5149"), QStringLiteral("lightButton"));
    auto *ledCloseButton = createActionButton(QStringLiteral("\u5173\u95ed\u8865\u5149"), QStringLiteral("secondaryButton"));
    ledOpenButton->setMinimumHeight(66);
    ledCloseButton->setMinimumHeight(66);
    connect(ledOpenButton, &QPushButton::clicked, this, &Widget::handleLedOpen);
    connect(ledCloseButton, &QPushButton::clicked, this, &Widget::handleLedClose);
    lightLayout->addStretch();
    lightLayout->addWidget(lightTitle);
    lightLayout->addSpacing(12);
    lightLayout->addWidget(ledOpenButton);
    lightLayout->addSpacing(4);
    lightLayout->addWidget(ledCloseButton);
    lightLayout->addStretch();

    auto *pumpCard = new QFrame(page);
    pumpCard->setObjectName(QStringLiteral("card"));
    auto *pumpLayout = new QVBoxLayout(pumpCard);
    pumpLayout->setContentsMargins(24, 22, 24, 22);
    pumpLayout->setSpacing(16);
    auto *pumpTitle = new QLabel(QStringLiteral("\u6c34\u6cf5\u63a7\u5236"), pumpCard);
    pumpTitle->setObjectName(QStringLiteral("sectionTitle"));
    pumpTitle->setAlignment(Qt::AlignCenter);
    auto *pumpOpenButton = createActionButton(QStringLiteral("\u6c34\u6cf5\u6253\u5f00"), QStringLiteral("primaryButton"));
    auto *pumpCloseButton = createActionButton(QStringLiteral("\u6c34\u6cf5\u5173\u95ed"), QStringLiteral("secondaryButton"));
    pumpOpenButton->setMinimumHeight(66);
    pumpCloseButton->setMinimumHeight(66);
    connect(pumpOpenButton, &QPushButton::clicked, this, &Widget::handlePumpOpen);
    connect(pumpCloseButton, &QPushButton::clicked, this, &Widget::handlePumpClose);
    pumpLayout->addStretch();
    pumpLayout->addWidget(pumpTitle);
    pumpLayout->addSpacing(12);
    pumpLayout->addWidget(pumpOpenButton);
    pumpLayout->addSpacing(4);
    pumpLayout->addWidget(pumpCloseButton);
    pumpLayout->addStretch();

    auto *flowCard = new QFrame(page);
    flowCard->setObjectName(QStringLiteral("card"));
    auto *flowLayout = new QVBoxLayout(flowCard);
    flowLayout->setContentsMargins(24, 20, 24, 20);
    flowLayout->setSpacing(8);
    auto *flowTitle = new QLabel(QStringLiteral("\u7cfb\u7edf\u65f6\u95f4"), flowCard);
    flowTitle->setObjectName(QStringLiteral("sectionTitle"));
    flowTitle->setAlignment(Qt::AlignCenter);
    labelDate = new QLabel(flowCard);
    labelDate->setObjectName(QStringLiteral("timeCardValue"));
    labelDate->setAlignment(Qt::AlignCenter);
    labelTime = new QLabel(flowCard);
    labelTime->setObjectName(QStringLiteral("timeCardValue"));
    labelTime->setAlignment(Qt::AlignCenter);
    flowLayout->addWidget(flowTitle);
    flowLayout->addStretch();
    flowLayout->addWidget(labelDate);
    flowLayout->addWidget(labelTime);
    flowLayout->addStretch();

    auto *localControlRow = new QHBoxLayout();
    localControlRow->setSpacing(16);
    localControlRow->addWidget(lightCard);
    localControlRow->addWidget(pumpCard);

    rightColumn->addLayout(localControlRow);
    rightColumn->addWidget(flowCard);

    mainRow->addWidget(motorCard, 3);
    mainRow->addLayout(rightColumn, 2);

    layout->addLayout(topRow);
    layout->addLayout(statusGrid);
    layout->addLayout(mainRow);
    return page;
}

QWidget *Widget::createSettingPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(16);

    auto *title = new QLabel(QStringLiteral("\u8bbe\u5907\u72b6\u6001"), page);
    title->setObjectName(QStringLiteral("pageTitle"));

    auto *grid = new QGridLayout();
    grid->setSpacing(14);

    auto makeCard = [this, page](const QString &titleText,
                                 const QString &valueText,
                                 QLabel **valueTarget,
                                 QProgressBar **barTarget) {
        auto *card = new QFrame(page);
        card->setObjectName(QStringLiteral("card"));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(20, 17, 20, 17);
        cardLayout->setSpacing(10);
        auto *value = new QLabel(valueText, card);
        value->setObjectName(QStringLiteral("valueLabel"));
        value->setWordWrap(true);
        if (!titleText.isEmpty()) {
            auto *caption = createCaption(titleText);
            cardLayout->addWidget(caption);
        }
        cardLayout->addWidget(value);
        if (barTarget) {
            auto *bar = new QProgressBar(card);
            bar->setRange(0, 100);
            bar->setValue(0);
            bar->setTextVisible(false);
            cardLayout->addWidget(bar);
            *barTarget = bar;
        } else {
            cardLayout->addStretch();
        }
        if (valueTarget) {
            *valueTarget = value;
        }
        return card;
    };

    grid->addWidget(makeCard(QStringLiteral("\u4e32\u53e3\u8bbe\u5907"), QStringLiteral("/dev/serial0"), nullptr, nullptr), 0, 0);
    grid->addWidget(makeCard(QStringLiteral("\u6ce2\u7279\u7387"), QStringLiteral("115200"), nullptr, nullptr), 0, 1);
    grid->addWidget(makeCard(QStringLiteral("CPU \u6e29\u5ea6"), QStringLiteral("\u8bfb\u53d6\u4e2d"), &piTempLabel, &piTempBar), 0, 2);
    grid->addWidget(makeCard(QStringLiteral("CPU \u5360\u7528"), QStringLiteral("\u8bfb\u53d6\u4e2d"), &cpuUsageLabel, &cpuUsageBar), 1, 0);
    grid->addWidget(makeCard(QStringLiteral("\u5185\u5b58\u5360\u7528"), QStringLiteral("\u8bfb\u53d6\u4e2d"), &memoryUsageLabel, &memoryUsageBar), 1, 1);
    grid->addWidget(makeCard(QStringLiteral("\u7f51\u7edc\u901f\u7387"), QStringLiteral("\u8bfb\u53d6\u4e2d"), &networkSpeedLabel, &networkSpeedBar), 1, 2);
    grid->addWidget(makeCard(QString(),
                             QStringLiteral("\u5f53\u524d WiFi / IP \u4fe1\u606f"),
                             &wifiIpLabel,
                             nullptr),
                    2,
                    0,
                    1,
                    3);

    if (wifiIpLabel) {
        wifiIpLabel->setStyleSheet(QStringLiteral("font-size:23px; color:#4EE6BA;"));
        wifiIpLabel->setAlignment(Qt::AlignCenter);
    }

    auto *buttonRow = new QHBoxLayout();
    auto *retryButton = createActionButton(QStringLiteral("\u91cd\u8fde\u4e32\u53e3"), QStringLiteral("secondaryButton"));
    auto *refreshButton = createActionButton(QStringLiteral("\u5237\u65b0\u72b6\u6001"), QStringLiteral("secondaryButton"));
    connect(retryButton, &QPushButton::clicked, this, [this]() {
        if (serial && serial->isOpen()) {
            serial->close();
        }
        ensureSerialOpen();
    });
    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        updateWifiSummary(true);
        updateDeviceStatus();
    });
    buttonRow->addWidget(retryButton);
    buttonRow->addWidget(refreshButton);
    buttonRow->addStretch();

    layout->addWidget(title);
    layout->addLayout(grid);
    layout->addLayout(buttonRow);
    layout->addStretch();
    return page;
}

QWidget *Widget::createVoicePage()
{
    auto *voicePage = new QWidget(this);
    auto *layout = new QVBoxLayout(voicePage);
    layout->setContentsMargins(24, 18, 24, 18);
    layout->setSpacing(12);

    auto *topRow = new QHBoxLayout();
    auto *titleBox = new QVBoxLayout();
    auto *title = new QLabel(QStringLiteral("\u8bed\u97f3\u52a9\u624b"), voicePage);
    title->setObjectName(QStringLiteral("pageTitle"));
    titleBox->addWidget(title);
    voiceStatusLabel = new QLabel(QStringLiteral("\u5f85\u547d"), voicePage);
    voiceStatusLabel->setObjectName(QStringLiteral("pillLabel"));
    topRow->addLayout(titleBox);
    topRow->addStretch();
    topRow->addWidget(voiceStatusLabel);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(10);
    voiceStartButton = createActionButton(QStringLiteral("\u5f00\u59cb\u8bf4\u8bdd"), QStringLiteral("primaryButton"));
    voiceStopButton = createActionButton(QStringLiteral("\u7ed3\u675f\u5e76\u63d0\u95ee"), QStringLiteral("secondaryButton"));
    voiceReplayButton = createActionButton(QStringLiteral("\u91cd\u65b0\u64ad\u62a5"), QStringLiteral("secondaryButton"));
    auto *clearButton = createActionButton(QStringLiteral("\u6e05\u7a7a\u5185\u5bb9"), QStringLiteral("secondaryButton"));
    connect(voiceStartButton, &QPushButton::clicked, this, [this]() {
        startVoiceRecording();
    });
    connect(voiceStopButton, &QPushButton::clicked, this, [this]() {
        stopVoiceRecording(true);
    });
    connect(voiceReplayButton, &QPushButton::clicked, this, [this]() {
        playVoiceReply(lastVoiceTtsText);
    });
    connect(clearButton, &QPushButton::clicked, this, &Widget::clearVoiceConversation);
    buttonRow->addWidget(voiceStartButton);
    buttonRow->addWidget(voiceStopButton);
    buttonRow->addWidget(voiceReplayButton);
    buttonRow->addStretch();
    buttonRow->addWidget(clearButton);

    auto makeVoiceCard = [this, voicePage](const QString &titleText, QPlainTextEdit **target) {
        auto *card = new QFrame(voicePage);
        card->setObjectName(QStringLiteral("card"));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(18, 16, 18, 16);
        cardLayout->setSpacing(8);
        auto *caption = new QLabel(titleText, card);
        caption->setObjectName(QStringLiteral("sectionTitle"));
        auto *view = new QPlainTextEdit(card);
        view->setReadOnly(true);
        view->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        view->setMinimumHeight(128);
        view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        cardLayout->addWidget(caption);
        cardLayout->addWidget(view);
        cardLayout->setStretch(1, 1);
        *target = view;
        return card;
    };

    auto *contentRow = new QHBoxLayout();
    contentRow->setSpacing(12);
    contentRow->addWidget(makeVoiceCard(QStringLiteral("\u63d0\u95ee\u5185\u5bb9"), &voiceQuestionView), 1);
    contentRow->addWidget(makeVoiceCard(QStringLiteral("\u52a9\u624b\u56de\u590d"), &voiceAnswerView), 1);

    auto *quickCard = new QFrame(voicePage);
    quickCard->setObjectName(QStringLiteral("card"));
    quickCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *quickLayout = new QVBoxLayout(quickCard);
    quickLayout->setContentsMargins(18, 16, 18, 16);
    quickLayout->setSpacing(10);
    auto *quickTitle = new QLabel(QStringLiteral("\u5e38\u89c1\u95ee\u9898"), quickCard);
    quickTitle->setObjectName(QStringLiteral("sectionTitle"));
    auto *quickGrid = new QGridLayout();
    quickGrid->setHorizontalSpacing(10);
    quickGrid->setVerticalSpacing(10);
    quickGrid->setColumnStretch(0, 1);
    quickGrid->setColumnStretch(1, 1);
    voiceQuickQuestionButtons.clear();

    const QStringList quickQuestionLabels = {
        QStringLiteral("\u4f60\u662f\u8c01\uff0c\n\u4f60\u80fd\u505a\u4ec0\u4e48"),
        QStringLiteral("\u7b2c\u4e00\u6b21\u4f7f\u7528\u65f6\uff0c\n\u6211\u5e94\u8be5\u5148\u505a\u4ec0\u4e48"),
        QStringLiteral("\u753b\u9762\u3001\u6570\u636e\u6216\u8fde\u63a5\u5f02\u5e38\uff0c\n\u6211\u8be5\u600e\u4e48\u529e"),
        QStringLiteral("\u65e5\u5e38\u7ba1\u7406\u4e2d\uff0c\n\u6211\u5e94\u91cd\u70b9\u5173\u6ce8\u4ec0\u4e48")
    };
    const QStringList quickQuestions = {
        QStringLiteral("\u4f60\u662f\u8c01\uff0c\u4f60\u80fd\u505a\u4ec0\u4e48"),
        QStringLiteral("\u7b2c\u4e00\u6b21\u4f7f\u7528\u65f6\uff0c\u6211\u5e94\u8be5\u5148\u505a\u4ec0\u4e48"),
        QStringLiteral("\u753b\u9762\u3001\u6570\u636e\u6216\u8fde\u63a5\u5f02\u5e38\uff0c\u6211\u8be5\u600e\u4e48\u529e"),
        QStringLiteral("\u65e5\u5e38\u7ba1\u7406\u4e2d\uff0c\u6211\u5e94\u91cd\u70b9\u5173\u6ce8\u4ec0\u4e48")
    };

    for (int index = 0; index < quickQuestions.size(); ++index) {
        const QString questionText = quickQuestions.at(index);
        auto *button = createActionButton(quickQuestionLabels.at(index), QStringLiteral("secondaryButton"));
        button->setMinimumHeight(72);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setStyleSheet(QStringLiteral("font-size:18px; padding:8px 12px; text-align:center;"));
        connect(button, &QPushButton::clicked, this, [this, questionText]() {
            submitQuickQuestion(questionText);
        });
        voiceQuickQuestionButtons.append(button);
        quickGrid->addWidget(button, index / 2, index % 2);
    }

    quickLayout->addWidget(quickTitle);
    quickLayout->addLayout(quickGrid);

    if (voiceQuestionView) {
        voiceQuestionView->setPlainText(QStringLiteral("\u7b49\u5f85\u63d0\u95ee..."));
    }
    if (voiceAnswerView) {
        voiceAnswerView->setPlainText(QStringLiteral("\u7b49\u5f85\u63d0\u95ee..."));
    }

    layout->addLayout(topRow);
    layout->addLayout(buttonRow);
    layout->addLayout(contentRow, 1);
    layout->addWidget(quickCard);

    updateVoiceUiState();
    return voicePage;
}
QWidget *Widget::createWeatherPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(16);

    auto *topRow = new QHBoxLayout();
    weatherTitleLabel = new QLabel(page);
    weatherTitleLabel->setObjectName(QStringLiteral("pageTitle"));
    weatherStatusLabel = new QLabel(QStringLiteral("\u7b49\u5f85\u5237\u65b0"), page);
    weatherStatusLabel->setObjectName(QStringLiteral("pillLabel"));
    weatherCityButton = createActionButton(QStringLiteral("\u5207\u6362\u57ce\u5e02\uff1a%1").arg(weatherLocationName), QStringLiteral("secondaryButton"));
    auto *refreshButton = createActionButton(QStringLiteral("\u5237\u65b0\u5929\u6c14"), QStringLiteral("secondaryButton"));
    connect(weatherCityButton, &QPushButton::clicked, this, &Widget::showWeatherCityDialog);
    connect(refreshButton, &QPushButton::clicked, this, [this]() {
        requestWeather();
    });

    topRow->addWidget(weatherTitleLabel);
    topRow->addStretch();
    topRow->addWidget(weatherStatusLabel);
    topRow->addWidget(weatherCityButton);
    topRow->addWidget(refreshButton);

    auto *hero = new QFrame(page);
    hero->setObjectName(QStringLiteral("weatherHero"));
    auto *heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(30, 26, 30, 26);
    heroLayout->setSpacing(26);

    auto *leftBox = new QVBoxLayout();
    leftBox->setSpacing(10);
    weatherTextLabel = new QLabel(QStringLiteral("\u7b49\u5f85\u5929\u6c14\u6570\u636e"), hero);
    weatherTextLabel->setObjectName(QStringLiteral("weatherText"));
    weatherTextLabel->setAlignment(Qt::AlignCenter);
    weatherTempLabel = new QLabel(QStringLiteral("-- \u2103"), hero);
    weatherTempLabel->setObjectName(QStringLiteral("weatherTemp"));
    weatherTempLabel->setAlignment(Qt::AlignCenter);
    weatherUpdateLabel = createCaption(QStringLiteral("%1  \u66f4\u65b0\u65f6\u95f4\uff1a--:--").arg(weatherLocationName));
    weatherUpdateLabel->setAlignment(Qt::AlignCenter);
    leftBox->addStretch();
    leftBox->addWidget(weatherTextLabel);
    leftBox->addWidget(weatherTempLabel);
    leftBox->addWidget(weatherUpdateLabel);
    leftBox->addStretch();

    auto *metricGrid = new QGridLayout();
    metricGrid->setSpacing(14);
    auto makeMetricCard = [this, hero](const QString &name, QLabel **valueLabel, const QString &initialValue) {
        auto *card = new QFrame(hero);
        card->setObjectName(QStringLiteral("weatherMetric"));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(18, 16, 18, 16);
        cardLayout->setSpacing(8);
        auto *nameLabel = createCaption(name);
        auto *value = new QLabel(initialValue, card);
        value->setObjectName(QStringLiteral("weatherMetricValue"));
        value->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(nameLabel);
        cardLayout->addStretch();
        cardLayout->addWidget(value);
        cardLayout->addStretch();
        if (valueLabel) {
            *valueLabel = value;
        }
        return card;
    };

    metricGrid->addWidget(makeMetricCard(QStringLiteral("\u6e7f\u5ea6"), &weatherHumidityLabel, QStringLiteral("-- %")), 0, 0);
    metricGrid->addWidget(makeMetricCard(QStringLiteral("\u98ce\u901f"), &weatherWindLabel, QStringLiteral("--")), 0, 1);
    metricGrid->addWidget(makeMetricCard(QStringLiteral("\u964d\u6c34"), &weatherRainLabel, QStringLiteral("--")), 1, 0, 1, 2);

    heroLayout->addLayout(leftBox, 5);
    heroLayout->addLayout(metricGrid, 4);

    auto *forecastRow = new QHBoxLayout();
    forecastRow->setSpacing(12);
    weatherHourlyLabels.clear();
    for (int i = 0; i < 4; ++i) {
        auto *card = new QFrame(page);
        card->setObjectName(QStringLiteral("forecastCard"));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 14, 16, 14);
        auto *label = new QLabel(QStringLiteral("--:--\n-- \u2103\n\u964d\u6c34 --%"), card);
        label->setObjectName(QStringLiteral("forecastText"));
        label->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(label);
        weatherHourlyLabels.append(label);
        forecastRow->addWidget(card);
    }

    layout->addLayout(topRow);
    layout->addWidget(hero, 1);
    layout->addLayout(forecastRow);
    updateWeatherLocationUi();
    return page;
}

QPushButton *Widget::createActionButton(const QString &text, const QString &objectName)
{
    auto *button = new QPushButton(text);
    button->setObjectName(objectName);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

QPushButton *Widget::createNavButton(const QString &text)
{
    auto *button = createActionButton(text, QStringLiteral("navButton"));
    button->setProperty("active", false);
    return button;
}

QLabel *Widget::createCaption(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("caption"));
    return label;
}

QString Widget::assistantChatUrl() const
{
    return QString::fromLatin1(kAssistantChatEndpoint);
}

QString Widget::voiceApiUrl() const
{
    return QString::fromLatin1(kVoiceApiEndpoint);
}

QString Widget::voiceAudioDevice() const
{
    return QString::fromLatin1(kVoiceAudioDevice);
}

QString Widget::voiceNaturalTtsProgram() const
{
    QStringList candidates;

    const QString configuredPath = qEnvironmentVariable("SMP_EDGE_TTS").trimmed();
    if (!configuredPath.isEmpty()) {
        candidates << configuredPath;
    }

    const QString inferredHome = inferredUserHomeFromAppPath();
    if (!inferredHome.isEmpty()) {
        candidates << QDir(inferredHome).filePath(QString::fromLatin1(kVoiceNaturalTtsRelativePath));
    }

    candidates << QDir::home().filePath(QString::fromLatin1(kVoiceNaturalTtsRelativePath))
               << QStandardPaths::findExecutable(QStringLiteral("edge-tts"));

    return firstExecutablePath(candidates);
}

QString Widget::voiceInputFilePath() const
{
    return QDir::temp().filePath(QStringLiteral("fishpond_voice_input.wav"));
}

QString Widget::voiceOutputFilePath() const
{
    return QDir::temp().filePath(QStringLiteral("fishpond_voice_reply.wav"));
}

QString Widget::buildVoiceContext() const
{
    const QString userText = currentUser.trimmed().isEmpty() ? QStringLiteral("未登录") : currentUser.trimmed();
    const QString wifiText = cachedWifiIp.trimmed().isEmpty() ? QStringLiteral("未连接") : cachedWifiIp.trimmed();
    const QString serialText = serialDetailLabel ? serialDetailLabel->text().trimmed() : QStringLiteral("暂无状态");
    return QStringLiteral("当前是鱼塘智能管理系统。当前使用者：%1。当前网络状态：%2。当前设备状态：%3。回答时请站在使用者角度，优先说明怎么用、怎么看和怎么排查；除非用户明确追问实现方式，否则不要主动提到底层硬件、开发框架或接口名称。")
        .arg(userText, wifiText, serialText);
}

void Widget::updateVoiceUiState()
{
    if (voiceStartButton) {
        voiceStartButton->setEnabled(!voiceRecordingActive && !voiceRequestRunning);
    }
    if (voiceStopButton) {
        voiceStopButton->setEnabled(voiceRecordingActive);
    }
    if (voiceReplayButton) {
        voiceReplayButton->setEnabled(!lastVoiceTtsText.trimmed().isEmpty() &&
                                      !voiceRecordingActive &&
                                      !voiceRequestRunning);
    }
    for (QPushButton *button : voiceQuickQuestionButtons) {
        if (button) {
            button->setEnabled(!voiceRecordingActive && !voiceRequestRunning);
        }
    }

    if (!voiceStatusLabel) {
        return;
    }

    QString statusText = voiceStatusLabel->text().trimmed();
    if (voiceRecordingActive) {
        statusText = QStringLiteral("\u5f55\u97f3\u4e2d");
    } else if (voiceRequestRunning) {
        statusText = QStringLiteral("AI\u601d\u8003\u4e2d...");
    } else if (voicePlayProcess && voicePlayProcess->state() != QProcess::NotRunning) {
        statusText = QStringLiteral("\u64ad\u62a5\u4e2d");
    } else if (statusText.isEmpty() ||
               statusText == QStringLiteral("\u5f55\u97f3\u4e2d") ||
               statusText == QStringLiteral("AI\u601d\u8003\u4e2d...") ||
               statusText == QStringLiteral("\u64ad\u62a5\u4e2d")) {
        statusText = QStringLiteral("\u5f85\u547d");
    }

    voiceStatusLabel->setText(statusText);
}

void Widget::clearVoiceConversation()
{
    stopVoicePlayback();
    lastVoiceTranscript.clear();
    lastVoiceAnswer.clear();
    lastVoiceTtsText.clear();
    if (voiceQuestionView) {
        voiceQuestionView->setPlainText(QStringLiteral("\u7b49\u5f85\u63d0\u95ee..."));
    }
    if (voiceAnswerView) {
        voiceAnswerView->setPlainText(QStringLiteral("\u7b49\u5f85\u63d0\u95ee..."));
    }
    if (voiceStatusLabel) {
        voiceStatusLabel->setText(QStringLiteral("\u5f85\u547d"));
    }
    updateVoiceUiState();
}

void Widget::stopVoicePlayback()
{
    if (voicePlayProcess && voicePlayProcess->state() != QProcess::NotRunning) {
        voicePlayProcess->kill();
        voicePlayProcess->waitForFinished(500);
    }
    updateVoiceUiState();
}

void Widget::submitQuickQuestion(const QString &question)
{
    const QString normalizedQuestion = question.trimmed();
    if (normalizedQuestion.isEmpty()) {
        return;
    }

    if (voiceRequestRunning) {
        if (voiceStatusLabel) {
            voiceStatusLabel->setText(QStringLiteral("AI\u601d\u8003\u4e2d..."));
        }
        updateVoiceUiState();
        return;
    }

    if (voiceRecordingActive) {
        if (voiceStatusLabel) {
            voiceStatusLabel->setText(QStringLiteral("\u8bf7\u5148\u7ed3\u675f\u5f53\u524d\u5f55\u97f3"));
        }
        updateVoiceUiState();
        return;
    }

    stopVoicePlayback();
    if (voiceQuestionView) {
        voiceQuestionView->setPlainText(normalizedQuestion);
    }
    if (voiceAnswerView) {
        voiceAnswerView->setPlainText(QStringLiteral("AI\u601d\u8003\u4e2d..."));
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("question"), normalizedQuestion);
    payload.insert(QStringLiteral("responseStyle"), QStringLiteral("spoken"));
    payload.insert(QStringLiteral("userName"), currentUser);
    payload.insert(QStringLiteral("context"), buildVoiceContext());

    QNetworkRequest request{QUrl(assistantChatUrl())};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("fish-pond-voice-ui/1.0"));

    voiceRequestRunning = true;
    if (voiceStatusLabel) {
        voiceStatusLabel->setText(QStringLiteral("AI\u601d\u8003\u4e2d..."));
    }
    updateVoiceUiState();

    QNetworkReply *reply = assistantManager->post(
        request,
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, normalizedQuestion]() {
        handleQuickQuestionReply(reply, normalizedQuestion);
    });
}

void Widget::startVoiceRecording()
{
    if (voiceRequestRunning) {
        if (voiceStatusLabel) {
            voiceStatusLabel->setText(QStringLiteral("AI\u601d\u8003\u4e2d..."));
        }
        updateVoiceUiState();
        return;
    }

    if (voiceRecordingActive) {
        return;
    }

    stopVoicePlayback();
    QFile::remove(voiceInputFilePath());

    if (voiceQuestionView) {
        voiceQuestionView->setPlainText(QStringLiteral("\u6b63\u5728\u5f55\u97f3\uff0c\u8bf7\u5f00\u59cb\u63d0\u95ee..."));
    }
    if (voiceAnswerView) {
        voiceAnswerView->setPlainText(QStringLiteral("AI\u601d\u8003\u4e2d..."));
    }

    QStringList arguments;
    arguments << QStringLiteral("-D") << voiceAudioDevice()
              << QStringLiteral("-f") << QStringLiteral("S16_LE")
              << QStringLiteral("-r") << QStringLiteral("16000")
              << QStringLiteral("-c") << QStringLiteral("1")
              << QStringLiteral("-t") << QStringLiteral("wav")
              << voiceInputFilePath();

    voiceRecordProcess->start(QStringLiteral("arecord"), arguments);
    if (!voiceRecordProcess->waitForStarted(2000)) {
        if (voiceStatusLabel) {
            voiceStatusLabel->setText(QStringLiteral("\u5f55\u97f3\u542f\u52a8\u5931\u8d25"));
        }
        updateVoiceUiState();
        return;
    }

    voiceRecordingActive = true;
    voiceSubmitAfterStop = false;
    if (voiceRecordStopTimer) {
        voiceRecordStopTimer->start(kVoiceRecordMaxMs);
    }
    if (voiceStatusLabel) {
        voiceStatusLabel->setText(QStringLiteral("\u5f55\u97f3\u4e2d"));
    }
    updateVoiceUiState();
}

void Widget::stopVoiceRecording(bool submitAfterStop)
{
    if (!voiceRecordingActive || !voiceRecordProcess) {
        return;
    }

    voiceSubmitAfterStop = submitAfterStop;
    if (voiceStatusLabel) {
        voiceStatusLabel->setText(QStringLiteral("AI\u601d\u8003\u4e2d..."));
    }
    voiceRecordProcess->terminate();
    QTimer::singleShot(1500, this, [this]() {
        if (voiceRecordProcess && voiceRecordProcess->state() != QProcess::NotRunning) {
            voiceRecordProcess->kill();
        }
    });
}

void Widget::submitVoiceAudio()
{
    QFileInfo audioInfo(voiceInputFilePath());
    if (!audioInfo.exists() || audioInfo.size() <= 0) {
        if (voiceStatusLabel) {
            voiceStatusLabel->setText(QStringLiteral("\u5f55\u97f3\u6587\u4ef6\u65e0\u6548"));
        }
        if (voiceAnswerView) {
            voiceAnswerView->setPlainText(QStringLiteral("\u5f55\u97f3\u6587\u4ef6\u65e0\u6548"));
        }
        updateVoiceUiState();
        return;
    }

    auto *audioFile = new QFile(voiceInputFilePath());
    if (!audioFile->open(QIODevice::ReadOnly)) {
        delete audioFile;
        if (voiceStatusLabel) {
            voiceStatusLabel->setText(QStringLiteral("\u65e0\u6cd5\u8bfb\u53d6\u5f55\u97f3\u6587\u4ef6"));
        }
        if (voiceAnswerView) {
            voiceAnswerView->setPlainText(QStringLiteral("\u65e0\u6cd5\u8bfb\u53d6\u5f55\u97f3\u6587\u4ef6"));
        }
        updateVoiceUiState();
        return;
    }

    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart audioPart;
    audioPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QVariant(QStringLiteral("form-data; name=\"audio\"; filename=\"voice.wav\"")));
    audioPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("audio/wav")));
    audioPart.setBodyDevice(audioFile);
    audioFile->setParent(multiPart);
    multiPart->append(audioPart);

    auto appendFormPart = [multiPart](const QString &name, const QString &value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"%1\"").arg(name)));
        part.setBody(value.toUtf8());
        multiPart->append(part);
    };

    appendFormPart(QStringLiteral("responseStyle"), QStringLiteral("spoken"));
    appendFormPart(QStringLiteral("userName"), currentUser);
    appendFormPart(QStringLiteral("context"), buildVoiceContext());

    QNetworkRequest request{QUrl(voiceApiUrl())};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("fish-pond-voice-ui/1.0"));

    voiceRequestRunning = true;
    if (voiceStatusLabel) {
        voiceStatusLabel->setText(QStringLiteral("AI\u601d\u8003\u4e2d..."));
    }
    if (voiceAnswerView) {
        voiceAnswerView->setPlainText(QStringLiteral("AI\u601d\u8003\u4e2d..."));
    }
    updateVoiceUiState();

    QNetworkReply *reply = assistantManager->post(request, multiPart);
    multiPart->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleVoiceReply(reply);
    });
}

void Widget::handleQuickQuestionReply(QNetworkReply *reply, const QString &question)
{
    const QScopedPointer<QNetworkReply, QScopedPointerDeleteLater> replyGuard(reply);
    voiceRequestRunning = false;

    const QByteArray body = reply->readAll();
    const QJsonDocument document = QJsonDocument::fromJson(body);
    const QJsonObject object = document.isObject() ? document.object() : QJsonObject();

    if (reply->error() != QNetworkReply::NoError) {
        const QString message = object.value(QStringLiteral("message")).toString(reply->errorString());
        if (voiceStatusLabel) {
            voiceStatusLabel->setText(QStringLiteral("\u670d\u52a1\u5f02\u5e38"));
        }
        if (voiceAnswerView) {
            voiceAnswerView->setPlainText(message);
        }
        updateVoiceUiState();
        return;
    }

    if (!object.value(QStringLiteral("ok")).toBool(false)) {
        const QString message = object.value(QStringLiteral("message")).toString(QStringLiteral("\u670d\u52a1\u5f02\u5e38"));
        if (voiceStatusLabel) {
            voiceStatusLabel->setText(QStringLiteral("\u670d\u52a1\u5f02\u5e38"));
        }
        if (voiceAnswerView) {
            voiceAnswerView->setPlainText(message);
        }
        updateVoiceUiState();
        return;
    }

    applyAssistantAnswer(
        question,
        object.value(QStringLiteral("answer")).toString(),
        object.value(QStringLiteral("ttsText")).toString());
}

void Widget::handleVoiceReply(QNetworkReply *reply)
{
    const QScopedPointer<QNetworkReply, QScopedPointerDeleteLater> replyGuard(reply);
    voiceRequestRunning = false;

    const QByteArray body = reply->readAll();
    const QJsonDocument document = QJsonDocument::fromJson(body);
    const QJsonObject object = document.isObject() ? document.object() : QJsonObject();

    if (reply->error() != QNetworkReply::NoError) {
        const QString message = object.value(QStringLiteral("message")).toString(reply->errorString());
        if (voiceStatusLabel) {
            voiceStatusLabel->setText(QStringLiteral("\u670d\u52a1\u5f02\u5e38"));
        }
        if (voiceAnswerView) {
            voiceAnswerView->setPlainText(message);
        }
        updateVoiceUiState();
        return;
    }

    if (!object.value(QStringLiteral("ok")).toBool(false)) {
        const QString message = object.value(QStringLiteral("message")).toString(QStringLiteral("\u670d\u52a1\u5f02\u5e38"));
        if (voiceStatusLabel) {
            voiceStatusLabel->setText(QStringLiteral("\u670d\u52a1\u5f02\u5e38"));
        }
        if (voiceAnswerView) {
            voiceAnswerView->setPlainText(message);
        }
        updateVoiceUiState();
        return;
    }

    applyAssistantAnswer(
        object.value(QStringLiteral("question")).toString(
            object.value(QStringLiteral("transcript")).toString()),
        object.value(QStringLiteral("answer")).toString(),
        object.value(QStringLiteral("ttsText")).toString());
}

void Widget::applyAssistantAnswer(const QString &question,
                                  const QString &answer,
                                  const QString &ttsText)
{
    lastVoiceTranscript = question.trimmed();
    lastVoiceAnswer = answer.trimmed();
    lastVoiceTtsText = ttsText.trimmed().isEmpty() ? lastVoiceAnswer : ttsText.trimmed();

    if (voiceQuestionView) {
        voiceQuestionView->setPlainText(lastVoiceTranscript);
    }
    if (voiceAnswerView) {
        voiceAnswerView->setPlainText(lastVoiceAnswer);
    }
    if (voiceStatusLabel) {
        voiceStatusLabel->setText(QStringLiteral("\u56de\u7b54\u5b8c\u6210"));
    }

    updateVoiceUiState();
    playVoiceReply(lastVoiceTtsText);
}

void Widget::playVoiceReply(const QString &text)
{
    const QString spokenText = text.trimmed();
    if (spokenText.isEmpty()) {
        return;
    }

    stopVoicePlayback();

    const QString edgeTtsProgram = voiceNaturalTtsProgram();
    const QString mpvProgram = firstExecutablePath(
        QStringList()
        << qEnvironmentVariable("SMP_MPV").trimmed()
        << QStandardPaths::findExecutable(QStringLiteral("mpv"))
        << QStringLiteral("/usr/bin/mpv")
        << QStringLiteral("/usr/local/bin/mpv"));
    const QString naturalAudioPath = QDir::temp().filePath(QStringLiteral("fishpond_voice_reply_edge.mp3"));
    if (!edgeTtsProgram.isEmpty() && !mpvProgram.isEmpty()) {
        QFile::remove(naturalAudioPath);

        QProcess synthProcess;
        QStringList synthArguments;
        synthArguments << QStringLiteral("--voice") << QString::fromLatin1(kVoiceEdgeVoice)
                       << QStringLiteral("--rate=-8%")
                       << QStringLiteral("--text") << spokenText
                       << QStringLiteral("--write-media") << naturalAudioPath;
        synthProcess.start(edgeTtsProgram, synthArguments);
        const bool naturalReady = synthProcess.waitForStarted(2000) &&
                                  synthProcess.waitForFinished(20000) &&
                                  synthProcess.exitStatus() == QProcess::NormalExit &&
                                  synthProcess.exitCode() == 0 &&
                                  QFileInfo(naturalAudioPath).exists() &&
                                  QFileInfo(naturalAudioPath).size() > 0;

        if (naturalReady) {
            QStringList playArguments;
            playArguments << QStringLiteral("--no-video")
                          << QStringLiteral("--really-quiet")
                          << QStringLiteral("--audio-device=alsa/%1").arg(voiceAudioDevice())
                          << naturalAudioPath;
            voicePlayProcess->start(mpvProgram, playArguments);
        }

        if (naturalReady && voicePlayProcess->waitForStarted(2500)) {
            if (voiceStatusLabel) {
                voiceStatusLabel->setText(QStringLiteral("\u64ad\u62a5\u4e2d"));
            }
            updateVoiceUiState();
            return;
        }
    }

    QFile::remove(voiceOutputFilePath());

    auto synthesize = [this, &spokenText](const QString &program, const QStringList &arguments) -> bool {
        QProcess process;
        process.start(program, arguments);
        if (!process.waitForStarted(2000)) {
            return false;
        }
        process.write(spokenText.toUtf8());
        process.closeWriteChannel();
        return process.waitForFinished(12000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };

    bool synthesized = synthesize(QStringLiteral("espeak-ng"),
                                  QStringList() << QStringLiteral("-v") << QStringLiteral("cmn")
                                                << QStringLiteral("-s") << QStringLiteral("155")
                                                << QStringLiteral("-w") << voiceOutputFilePath());
    if (!synthesized) {
        synthesized = synthesize(QStringLiteral("espeak"),
                                 QStringList() << QStringLiteral("-v") << QStringLiteral("zh")
                                               << QStringLiteral("-s") << QStringLiteral("155")
                                               << QStringLiteral("-w") << voiceOutputFilePath());
    }

    if (!synthesized || !QFile::exists(voiceOutputFilePath())) {
        if (voiceStatusLabel) {
            voiceStatusLabel->setText(QStringLiteral("\u64ad\u62a5\u4e0d\u53ef\u7528"));
        }
        updateVoiceUiState();
        return;
    }

    QStringList arguments;
    arguments << QStringLiteral("-D") << voiceAudioDevice() << voiceOutputFilePath();
    voicePlayProcess->start(QStringLiteral("aplay"), arguments);
    if (!voicePlayProcess->waitForStarted(2000)) {
        if (voiceStatusLabel) {
            voiceStatusLabel->setText(QStringLiteral("\u64ad\u62a5\u542f\u52a8\u5931\u8d25"));
        }
        updateVoiceUiState();
        return;
    }

    if (voiceStatusLabel) {
        voiceStatusLabel->setText(QStringLiteral("\u64ad\u62a5\u4e2d"));
    }
    updateVoiceUiState();
}
bool Widget::eventFilter(QObject *watched, QEvent *event)
{
    auto *edit = qobject_cast<QLineEdit *>(watched);
    if (edit &&
        edit->property("touchKeyboardEnabled").toBool() &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::TouchBegin)) {
        activeInputEdit = edit;
        QTimer::singleShot(120, this, [this, edit]() {
            showTouchKeyboard(edit);
        });
    }

    return QWidget::eventFilter(watched, event);
}

void Widget::showTouchKeyboard(QLineEdit *targetEdit)
{
    if (!targetEdit) {
        return;
    }

    QDialog keyboard(this);
    keyboard.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    keyboard.setModal(true);
    keyboard.setFixedSize(820, 340);
    keyboard.move(mapToGlobal(QPoint((width() - keyboard.width()) / 2, height() - keyboard.height() - 24)));
    keyboard.setStyleSheet(QStringLiteral(R"(
        QDialog {
            background: #071E24;
            border: 1px solid rgba(125, 235, 207, 90);
            border-radius: 22px;
        }
        QLabel {
            color: #EAFBF4;
            font-size: 20px;
            font-weight: 800;
        }
        QLineEdit {
            min-height: 48px;
            border-radius: 14px;
            padding: 0 16px;
            background: #031015;
            border: 1px solid rgba(115, 220, 197, 110);
            color: #FFFFFF;
            font-size: 20px;
        }
        QPushButton {
            min-height: 34px;
            border: none;
            border-radius: 10px;
            padding: 0 8px;
            background: rgba(255, 255, 255, 25);
            color: #D8FFF4;
            font-size: 16px;
            font-weight: 800;
        }
        QPushButton#confirmKey {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #24D3A6, stop:1 #2B8CFF);
            color: #062027;
        }
        QPushButton#cancelKey {
            background: rgba(255, 107, 107, 65);
            color: #FFECEC;
        }
        QPushButton#toolKey {
            background: rgba(78, 230, 186, 35);
            color: #CFFFF1;
        }
    )"));

    auto *layout = new QVBoxLayout(&keyboard);
    layout->setContentsMargins(20, 16, 20, 18);
    layout->setSpacing(10);

    const QString customTitle = targetEdit->property("touchKeyboardTitle").toString().trimmed();
    const bool passwordMode = targetEdit->echoMode() == QLineEdit::Password;
    auto *title = new QLabel(customTitle.isEmpty()
                                 ? (passwordMode ? QStringLiteral("\u8f93\u5165\u5bc6\u7801")
                                                 : QStringLiteral("\u8f93\u5165\u5185\u5bb9"))
                                 : customTitle,
                             &keyboard);
    auto *preview = new QLineEdit(&keyboard);
    preview->setText(targetEdit->text());
    preview->setEchoMode(targetEdit->echoMode());
    preview->setFocus();
    preview->selectAll();

    layout->addWidget(title);
    layout->addWidget(preview);

    auto addRow = [&](const QStringList &keys) {
        auto *row = new QHBoxLayout();
        row->setSpacing(7);
        for (const QString &key : keys) {
            auto *button = new QPushButton(key, &keyboard);
            button->setFocusPolicy(Qt::NoFocus);
            connect(button, &QPushButton::clicked, this, [preview, key]() {
                preview->insert(key);
            });
            row->addWidget(button);
        }
        layout->addLayout(row);
    };

    addRow(QStringList() << QStringLiteral("1") << QStringLiteral("2") << QStringLiteral("3")
                         << QStringLiteral("4") << QStringLiteral("5") << QStringLiteral("6")
                         << QStringLiteral("7") << QStringLiteral("8") << QStringLiteral("9")
                         << QStringLiteral("0"));
    addRow(QStringList() << QStringLiteral("q") << QStringLiteral("w") << QStringLiteral("e")
                         << QStringLiteral("r") << QStringLiteral("t") << QStringLiteral("y")
                         << QStringLiteral("u") << QStringLiteral("i") << QStringLiteral("o")
                         << QStringLiteral("p"));
    addRow(QStringList() << QStringLiteral("a") << QStringLiteral("s") << QStringLiteral("d")
                         << QStringLiteral("f") << QStringLiteral("g") << QStringLiteral("h")
                         << QStringLiteral("j") << QStringLiteral("k") << QStringLiteral("l"));
    addRow(QStringList() << QStringLiteral("z") << QStringLiteral("x") << QStringLiteral("c")
                         << QStringLiteral("v") << QStringLiteral("b") << QStringLiteral("n")
                         << QStringLiteral("m") << QStringLiteral("_") << QStringLiteral("-")
                         << QStringLiteral(".") << QStringLiteral(":") << QStringLiteral("/"));

    auto *toolRow = new QHBoxLayout();
    toolRow->setSpacing(8);

    auto *clearButton = new QPushButton(QStringLiteral("\u6e05\u7a7a"), &keyboard);
    clearButton->setObjectName(QStringLiteral("toolKey"));
    auto *backButton = new QPushButton(QStringLiteral("\u9000\u683c"), &keyboard);
    backButton->setObjectName(QStringLiteral("toolKey"));
    auto *spaceButton = new QPushButton(QStringLiteral("\u7a7a\u683c"), &keyboard);
    spaceButton->setObjectName(QStringLiteral("toolKey"));
    auto *cancelButton = new QPushButton(QStringLiteral("\u53d6\u6d88"), &keyboard);
    cancelButton->setObjectName(QStringLiteral("cancelKey"));
    auto *okButton = new QPushButton(QStringLiteral("\u786e\u5b9a"), &keyboard);
    okButton->setObjectName(QStringLiteral("confirmKey"));

    const QList<QPushButton *> keyboardToolButtons = { clearButton, backButton, spaceButton, cancelButton, okButton };
    for (QPushButton *button : keyboardToolButtons) {
        button->setFocusPolicy(Qt::NoFocus);
    }

    connect(clearButton, &QPushButton::clicked, preview, &QLineEdit::clear);
    connect(backButton, &QPushButton::clicked, preview, &QLineEdit::backspace);
    connect(spaceButton, &QPushButton::clicked, preview, [preview]() {
        preview->insert(QStringLiteral(" "));
    });
    connect(cancelButton, &QPushButton::clicked, &keyboard, &QDialog::reject);
    connect(okButton, &QPushButton::clicked, &keyboard, &QDialog::accept);

    toolRow->addWidget(clearButton);
    toolRow->addWidget(backButton);
    toolRow->addWidget(spaceButton);
    toolRow->addStretch();
    toolRow->addWidget(cancelButton);
    toolRow->addWidget(okButton);
    layout->addLayout(toolRow);

    if (keyboard.exec() == QDialog::Accepted) {
        targetEdit->setText(preview->text());
    }
}

void Widget::handleLogin()
{
    const QString account = accountEdit ? accountEdit->text().trimmed() : QString();
    const QString password = passwordEdit ? passwordEdit->text() : QString();

    if (account.isEmpty() || password.isEmpty()) {
        return;
    }

    currentUser = account;
    if (operatorLabel) {
        operatorLabel->setText(QStringLiteral("\u5f53\u524d\u64cd\u4f5c\u4eba\uff1a%1").arg(currentUser));
    }

    rootStack->setCurrentIndex(1);
    refreshBackground();
    showContentPage(0, navWaterButton);
}

void Widget::showContentPage(int index, QPushButton *activeButton)
{
    if (contentStack) {
        contentStack->setCurrentIndex(index);
    }
    setActiveNav(activeButton);
}

void Widget::refreshBackground()
{
    update();
}

void Widget::setActiveNav(QPushButton *activeButton)
{
    const QList<QPushButton *> buttons = {
        navWaterButton,
        navSettingButton,
        navRecordButton,
        navWeatherButton
    };
    for (QPushButton *button : buttons) {
        if (!button) {
            continue;
        }
        button->setProperty("active", button == activeButton);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    }
}


void Widget::updateDeviceStatus()
{
    const QString tempText = readCpuTemperatureText();
    const QString cpuText = readCpuUsageText();
    const QString memoryText = readMemoryUsageText();
    const QString networkText = readNetworkSpeedText();
    updateWifiSummary(false);
    const QString wifiIpText = cachedWifiIp;

    if (piTempLabel) {
        piTempLabel->setText(tempText);
    }
    if (cpuUsageLabel) {
        cpuUsageLabel->setText(cpuText);
    }
    if (memoryUsageLabel) {
        memoryUsageLabel->setText(memoryText);
    }
    if (networkSpeedLabel) {
        networkSpeedLabel->setText(networkText);
    }
    if (wifiIpLabel) {
        wifiIpLabel->setText(QStringLiteral("WiFi / IP\uff1a%1").arg(wifiIpText));
    }

    bool ok = false;
    const double tempValue = numberBefore(tempText, QLatin1Char(' '), &ok);
    if (piTempBar) {
        piTempBar->setValue(ok ? boundedProgress(tempValue * 100.0 / 90.0) : 0);
    }

    const double cpuValue = numberBefore(cpuText, QLatin1Char('%'), &ok);
    if (cpuUsageBar) {
        cpuUsageBar->setValue(ok ? boundedProgress(cpuValue) : 0);
    }

    const double memoryValue = numberBefore(memoryText, QLatin1Char('%'), &ok);
    if (memoryUsageBar) {
        memoryUsageBar->setValue(ok ? boundedProgress(memoryValue) : 0);
    }

    double networkKb = numberBefore(networkText, QLatin1Char(' '), &ok);
    if (ok && networkText.contains(QStringLiteral("MB/s"))) {
        networkKb *= 1024.0;
    }
    if (networkSpeedBar) {
        networkSpeedBar->setValue(ok ? boundedProgress(networkKb * 100.0 / 2048.0) : 0);
    }
}

void Widget::updateWifiSummary(bool force)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (force || lastWifiStatusRefreshMs <= 0 || nowMs - lastWifiStatusRefreshMs >= kWifiStatusRefreshMs) {
        cachedWifiIp = readWifiIpText();
        lastWifiStatusRefreshMs = nowMs;
    }

    if (cachedWifiIp.trimmed().isEmpty()) {
        cachedWifiIp = QStringLiteral("Disconnected");
    }
}

QString Widget::readCpuTemperatureText() const
{
    QFile file(QStringLiteral("/sys/class/thermal/thermal_zone0/temp"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("\u65e0\u6cd5\u8bfb\u53d6");
    }

    bool ok = false;
    const double tempMilli = QString::fromLatin1(file.readAll()).trimmed().toDouble(&ok);
    if (!ok) {
        return QStringLiteral("\u8bfb\u53d6\u5f02\u5e38");
    }

    return QStringLiteral("%1 \u2103").arg(tempMilli / 1000.0, 0, 'f', 1);
}

QString Widget::readMemoryUsageText() const
{
    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("\u65e0\u6cd5\u8bfb\u53d6");
    }

    quint64 memTotalKb = 0;
    quint64 memAvailableKb = 0;
    const QStringList lines = QString::fromLatin1(file.readAll()).split(QLatin1Char('\n'));

    for (const QString &line : lines) {
        const QString normalized = line.simplified();
        const QStringList parts = normalized.split(QLatin1Char(' '));
        if (parts.size() < 2) {
            continue;
        }

        if (parts.at(0) == QStringLiteral("MemTotal:")) {
            memTotalKb = parts.at(1).toULongLong();
        } else if (parts.at(0) == QStringLiteral("MemAvailable:")) {
            memAvailableKb = parts.at(1).toULongLong();
        }
    }

    if (memTotalKb == 0 || memAvailableKb == 0 || memAvailableKb > memTotalKb) {
        return QStringLiteral("\u8bfb\u53d6\u5f02\u5e38");
    }

    const quint64 usedKb = memTotalKb - memAvailableKb;
    const double usedPercent = usedKb * 100.0 / memTotalKb;
    return QStringLiteral("%1% / %2 MB")
        .arg(usedPercent, 0, 'f', 1)
        .arg(memTotalKb / 1024);
}

QString Widget::readCpuUsageText()
{
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("\u65e0\u6cd5\u8bfb\u53d6");
    }

    const QString firstLine = QString::fromLatin1(file.readLine()).simplified();
    const QStringList parts = firstLine.split(QLatin1Char(' '));
    if (parts.size() < 8 || parts.first() != QStringLiteral("cpu")) {
        return QStringLiteral("\u8bfb\u53d6\u5f02\u5e38");
    }

    quint64 total = 0;
    for (int i = 1; i < parts.size(); ++i) {
        total += parts.at(i).toULongLong();
    }

    const quint64 idle = parts.at(4).toULongLong() + parts.at(5).toULongLong();
    if (lastCpuTotal == 0 || total <= lastCpuTotal) {
        lastCpuTotal = total;
        lastCpuIdle = idle;
        return QStringLiteral("\u8bfb\u53d6\u4e2d");
    }

    const quint64 totalDiff = total - lastCpuTotal;
    const quint64 idleDiff = idle >= lastCpuIdle ? idle - lastCpuIdle : 0;
    lastCpuTotal = total;
    lastCpuIdle = idle;

    if (totalDiff == 0 || idleDiff > totalDiff) {
        return QStringLiteral("\u8bfb\u53d6\u5f02\u5e38");
    }

    const double usage = (totalDiff - idleDiff) * 100.0 / totalDiff;
    return QStringLiteral("%1%").arg(usage, 0, 'f', 1);
}

QString Widget::readNetworkSpeedText()
{
    quint64 totalBytes = 0;
    if (!readNetworkBytes(&totalBytes)) {
        return QStringLiteral("\u65e0\u6cd5\u8bfb\u53d6");
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (lastNetSampleMs == 0 || totalBytes < lastNetBytes) {
        lastNetBytes = totalBytes;
        lastNetSampleMs = nowMs;
        return QStringLiteral("\u8bfb\u53d6\u4e2d");
    }

    const double elapsedSec = (nowMs - lastNetSampleMs) / 1000.0;
    if (elapsedSec <= 0.0) {
        return QStringLiteral("\u8bfb\u53d6\u4e2d");
    }

    const double bytesPerSec = (totalBytes - lastNetBytes) / elapsedSec;
    lastNetBytes = totalBytes;
    lastNetSampleMs = nowMs;

    if (bytesPerSec >= 1024.0 * 1024.0) {
        return QStringLiteral("%1 MB/s").arg(bytesPerSec / 1024.0 / 1024.0, 0, 'f', 2);
    }

    return QStringLiteral("%1 KB/s").arg(bytesPerSec / 1024.0, 0, 'f', 1);
}

QString Widget::readWifiInterfaceName() const
{
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        if (iface.name() == QStringLiteral("wlan0")) {
            return iface.name();
        }
    }

    for (const QNetworkInterface &iface : interfaces) {
        if (iface.name().startsWith(QStringLiteral("wl"))) {
            return iface.name();
        }
    }

    return QStringLiteral("wlan0");
}

bool Widget::runProcess(const QString &program,
                        const QStringList &arguments,
                        QString *standardOutput,
                        QString *standardError,
                        int timeoutMs) const
{
    if (standardOutput) {
        standardOutput->clear();
    }
    if (standardError) {
        standardError->clear();
    }

    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(3000)) {
        if (standardError) {
            *standardError = process.errorString();
        }
        return false;
    }

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        if (standardOutput) {
            *standardOutput = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
        }
        if (standardError) {
            *standardError = QStringLiteral("%1 \u6267\u884c\u8d85\u65f6").arg(program);
        }
        return false;
    }

    if (standardOutput) {
        *standardOutput = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    }
    if (standardError) {
        *standardError = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (standardError->isEmpty() &&
            (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)) {
            *standardError = process.errorString();
        }
    }

    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

QString Widget::readWifiIpText() const
{
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    const QString preferredName = readWifiInterfaceName();

    auto readInterfaceIp = [](const QNetworkInterface &iface) -> QString {
        const QNetworkInterface::InterfaceFlags flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) ||
            !(flags & QNetworkInterface::IsRunning) ||
            (flags & QNetworkInterface::IsLoopBack)) {
            return QString();
        }

        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            const QHostAddress address = entry.ip();
            if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()) {
                return address.toString();
            }
        }

        return QString();
    };

    for (const QNetworkInterface &iface : interfaces) {
        if (iface.name() == preferredName) {
            const QString ip = readInterfaceIp(iface);
            if (!ip.isEmpty()) {
                return ip;
            }
        }
    }

    for (const QNetworkInterface &iface : interfaces) {
        if (iface.name().startsWith(QStringLiteral("wl"))) {
            const QString ip = readInterfaceIp(iface);
            if (!ip.isEmpty()) {
                return ip;
            }
        }
    }

    return QStringLiteral("WiFi 未连接");
}

bool Widget::readNetworkBytes(quint64 *totalBytes) const
{
    if (!totalBytes) {
        return false;
    }

    QFile file(QStringLiteral("/proc/net/dev"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    quint64 total = 0;
    const QStringList lines = QString::fromLatin1(file.readAll()).split(QLatin1Char('\n'));

    for (const QString &line : lines) {
        if (!line.contains(QLatin1Char(':'))) {
            continue;
        }

        const QString iface = line.section(QLatin1Char(':'), 0, 0).trimmed();
        if (iface == QStringLiteral("lo")) {
            continue;
        }

        const QStringList parts = line.section(QLatin1Char(':'), 1).simplified().split(QLatin1Char(' '));
        if (parts.size() < 16) {
            continue;
        }

        total += parts.at(0).toULongLong();
        total += parts.at(8).toULongLong();
    }

    *totalBytes = total;
    return true;
}

void Widget::loadWeatherLocation()
{
    QSettings settings(QStringLiteral("FishPond"), QStringLiteral("FishPondControl"));
    const QString savedName = settings.value(QStringLiteral("weather/locationName")).toString();
    const WeatherCity *city = savedName.isEmpty() ? &defaultWeatherCity() : findWeatherCity(savedName);
    if (!city) {
        city = &defaultWeatherCity();
    }

    applyWeatherLocation(QString::fromUtf8(city->name), city->latitude, city->longitude, false);
}

void Widget::saveWeatherLocation()
{
    QSettings settings(QStringLiteral("FishPond"), QStringLiteral("FishPondControl"));
    settings.setValue(QStringLiteral("weather/locationName"), weatherLocationName);
    settings.setValue(QStringLiteral("weather/latitude"), weatherLatitude);
    settings.setValue(QStringLiteral("weather/longitude"), weatherLongitude);
    settings.sync();
}

void Widget::applyWeatherLocation(const QString &name, double latitude, double longitude, bool save)
{
    weatherLocationName = name;
    weatherLatitude = latitude;
    weatherLongitude = longitude;
    updateWeatherLocationUi();

    if (save) {
        saveWeatherLocation();
    }
}

void Widget::updateWeatherLocationUi()
{
    if (weatherTitleLabel) {
        weatherTitleLabel->setText(QStringLiteral("天气情况  %1").arg(weatherLocationName));
    }
    if (weatherCityButton) {
        weatherCityButton->setText(QStringLiteral("切换城市：%1").arg(weatherLocationName));
    }
    if (weatherUpdateLabel) {
        const QString separator = QStringLiteral("  更新时间：");
        QString updateText = QStringLiteral("--:--");
        const QString currentText = weatherUpdateLabel->text();
        const int index = currentText.indexOf(separator);
        if (index >= 0) {
            const QString tail = currentText.mid(index + separator.size()).trimmed();
            if (!tail.isEmpty()) {
                updateText = tail;
            }
        }

        weatherUpdateLabel->setText(QStringLiteral("%1  更新时间：%2").arg(weatherLocationName, updateText));
    }
}

void Widget::showWeatherCityDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("\u9009\u62e9\u5929\u6c14\u57ce\u5e02"));
    dialog.setModal(true);
    dialog.setFixedSize(660, 430);
    dialog.setStyleSheet(styleSheet() + QStringLiteral(R"(
        QDialog {
            background: #061B20;
        }
        QLabel#dialogTitle {
            font-size: 28px;
            font-weight: 900;
            color: #F7FFFA;
        }
    )"));

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(18);

    auto *title = new QLabel(QStringLiteral("\u9009\u62e9\u5929\u6c14\u57ce\u5e02"), &dialog);
    title->setObjectName(QStringLiteral("dialogTitle"));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto *grid = new QGridLayout();
    grid->setSpacing(14);
    for (int i = 0; i < static_cast<int>(sizeof(kWeatherCities) / sizeof(kWeatherCities[0])); ++i) {
        const WeatherCity city = kWeatherCities[i];
        auto *button = createActionButton(QString::fromUtf8(city.name), QStringLiteral("cityButton"));
        button->setProperty("active", QString::fromUtf8(city.name) == weatherLocationName);
        connect(button, &QPushButton::clicked, &dialog, [this, &dialog, city]() {
            applyWeatherLocation(QString::fromUtf8(city.name), city.latitude, city.longitude, true);
            if (weatherTextLabel) {
                weatherTextLabel->setText(QStringLiteral("\u7b49\u5f85\u5929\u6c14\u6570\u636e"));
            }
            if (weatherTempLabel) {
                weatherTempLabel->setText(QStringLiteral("-- \u2103"));
            }
            if (weatherHumidityLabel) {
                weatherHumidityLabel->setText(QStringLiteral("-- %"));
            }
            if (weatherWindLabel) {
                weatherWindLabel->setText(QStringLiteral("--"));
            }
            if (weatherRainLabel) {
                weatherRainLabel->setText(QStringLiteral("--"));
            }
            for (QLabel *label : weatherHourlyLabels) {
                if (label) {
                    label->setText(QStringLiteral("--:--\n-- \u2103\n\u964d\u6c34 --%"));
                }
            }
            requestWeather();
            dialog.accept();
        });
        grid->addWidget(button, i / 3, i % 3);
    }
    layout->addLayout(grid);

    auto *closeButton = createActionButton(QStringLiteral("\u5173\u95ed"), QStringLiteral("secondaryButton"));
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    layout->addWidget(closeButton);
    dialog.exec();
}

void Widget::requestWeather()
{
    if (!weatherManager) {
        return;
    }

    if (weatherStatusLabel) {
        weatherStatusLabel->setText(QStringLiteral("\u6b63\u5728\u83b7\u53d6\u5929\u6c14..."));
    }

    QUrl url(QStringLiteral("https://api.open-meteo.com/v1/forecast"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("latitude"), QString::number(weatherLatitude, 'f', 4));
    query.addQueryItem(QStringLiteral("longitude"), QString::number(weatherLongitude, 'f', 4));
    query.addQueryItem(QStringLiteral("current"),
                       QStringLiteral("temperature_2m,relative_humidity_2m,precipitation,weather_code,wind_speed_10m,wind_direction_10m"));
    query.addQueryItem(QStringLiteral("hourly"), QStringLiteral("temperature_2m,precipitation_probability,weather_code"));
    query.addQueryItem(QStringLiteral("forecast_days"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("timezone"), QStringLiteral("Asia/Shanghai"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("fish-pond-control/1.0"));
    request.setAttribute(QNetworkRequest::User, weatherLocationName);
    weatherManager->get(request);
}

void Widget::handleWeatherReply(QNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    const QScopedPointer<QNetworkReply, QScopedPointerDeleteLater> replyGuard(reply);
    const QString replyLocation = reply->request().attribute(QNetworkRequest::User).toString();
    if (!replyLocation.isEmpty() && replyLocation != weatherLocationName) {
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        setWeatherUnavailable(QStringLiteral("\u5929\u6c14\u670d\u52a1\u8bf7\u6c42\u5931\u8d25"));
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
    if (!document.isObject()) {
        setWeatherUnavailable(QStringLiteral("\u5929\u6c14\u6570\u636e\u89e3\u6790\u5931\u8d25"));
        return;
    }

    const QJsonObject root = document.object();
    const QJsonObject current = root.value(QStringLiteral("current")).toObject();
    if (current.isEmpty()) {
        setWeatherUnavailable(QStringLiteral("\u5f53\u524d\u5929\u6c14\u6570\u636e\u4e3a\u7a7a"));
        return;
    }

    const double temp = current.value(QStringLiteral("temperature_2m")).toDouble();
    const double humidity = current.value(QStringLiteral("relative_humidity_2m")).toDouble();
    const double precipitation = current.value(QStringLiteral("precipitation")).toDouble();
    const int code = current.value(QStringLiteral("weather_code")).toInt();
    const double windSpeed = current.value(QStringLiteral("wind_speed_10m")).toDouble();
    const double windDirection = current.value(QStringLiteral("wind_direction_10m")).toDouble();
    const QString currentTime = current.value(QStringLiteral("time")).toString();

    if (weatherTempLabel) {
        weatherTempLabel->setText(QStringLiteral("%1 \u2103").arg(temp, 0, 'f', 1));
    }
    if (weatherTextLabel) {
        weatherTextLabel->setText(weatherCodeText(code));
    }
    if (weatherHumidityLabel) {
        weatherHumidityLabel->setText(QStringLiteral("%1 %").arg(humidity, 0, 'f', 0));
    }
    if (weatherWindLabel) {
        weatherWindLabel->setText(QStringLiteral("%1 %2 km/h")
                                      .arg(windDirectionText(windDirection))
                                      .arg(windSpeed, 0, 'f', 1));
    }
    if (weatherRainLabel) {
        weatherRainLabel->setText(QStringLiteral("%1 mm").arg(precipitation, 0, 'f', 1));
    }
    if (weatherUpdateLabel) {
        const QDateTime updateTime = QDateTime::fromString(currentTime, Qt::ISODate);
        const QString updateText = updateTime.isValid()
            ? updateTime.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
            : currentTime;
        weatherUpdateLabel->setText(QStringLiteral("%1  \u66f4\u65b0\u65f6\u95f4\uff1a%2")
                                        .arg(weatherLocationName, updateText));
    }
    if (weatherStatusLabel) {
        weatherStatusLabel->setText(QStringLiteral("\u5929\u6c14\u5df2\u66f4\u65b0"));
    }

    const QJsonObject hourly = root.value(QStringLiteral("hourly")).toObject();
    const QJsonArray times = hourly.value(QStringLiteral("time")).toArray();
    const QJsonArray temps = hourly.value(QStringLiteral("temperature_2m")).toArray();
    const QJsonArray rainRates = hourly.value(QStringLiteral("precipitation_probability")).toArray();
    const QJsonArray codes = hourly.value(QStringLiteral("weather_code")).toArray();

    int startIndex = 0;
    for (int i = 0; i < times.size(); ++i) {
        if (times.at(i).toString() >= currentTime) {
            startIndex = i;
            break;
        }
    }

    for (int i = 0; i < weatherHourlyLabels.size(); ++i) {
        QLabel *label = weatherHourlyLabels.at(i);
        const int index = startIndex + i * 2;
        if (!label || index >= times.size() || index >= temps.size()) {
            continue;
        }

        const QDateTime time = QDateTime::fromString(times.at(index).toString(), Qt::ISODate);
        const QString hourText = time.isValid() ? time.toString(QStringLiteral("HH:mm")) : times.at(index).toString();
        const double itemTemp = temps.at(index).toDouble();
        const int rainProbability = index < rainRates.size() ? rainRates.at(index).toInt() : 0;
        const int itemCode = index < codes.size() ? codes.at(index).toInt() : code;
        label->setText(QStringLiteral("%1\n%2 \u2103\n\u964d\u6c34 %3%")
                           .arg(hourText)
                           .arg(itemTemp, 0, 'f', 1)
                           .arg(rainProbability));
        label->setToolTip(weatherCodeText(itemCode));
    }

}

void Widget::setWeatherUnavailable(const QString &statusText)
{
    if (weatherStatusLabel) {
        weatherStatusLabel->setText(statusText);
    }
    if (weatherTextLabel) {
        weatherTextLabel->setText(QStringLiteral("\u6682\u65e0\u5929\u6c14\u6570\u636e"));
    }
    if (weatherTempLabel) {
        weatherTempLabel->setText(QStringLiteral("-- \u2103"));
    }
}

void Widget::loadWaterApiUrl()
{
    QSettings settings(QStringLiteral("FishPond"), QStringLiteral("FishPondControl"));
    waterApiUrlValue = normalizedWaterApiUrl(
        settings.value(QStringLiteral("water/apiUrl"),
                       QString::fromLatin1(kWaterDefaultApiEndpoint)).toString());
}

void Widget::saveWaterApiUrl()
{
    QSettings settings(QStringLiteral("FishPond"), QStringLiteral("FishPondControl"));
    settings.setValue(QStringLiteral("water/apiUrl"), waterApiUrlValue);
    settings.sync();
}

QString Widget::normalizedWaterApiUrl(const QString &rawText) const
{
    QString text = rawText.trimmed();
    if (text.isEmpty()) {
        text = QString::fromLatin1(kWaterDefaultApiEndpoint);
    }

    if (!text.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) &&
        !text.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        text.prepend(QStringLiteral("http://"));
    }

    while (text.endsWith(QLatin1Char('/'))) {
        text.chop(1);
    }

    if (!text.endsWith(QStringLiteral("/api/water/latest"), Qt::CaseInsensitive)) {
        text += QStringLiteral("/api/water/latest");
    }

    return text;
}

void Widget::requestWaterData()
{
    if (!waterManager) {
        return;
    }

    waterApiUrlValue = normalizedWaterApiUrl(waterApiEdit ? waterApiEdit->text() : waterApiUrlValue);
    if (waterApiEdit && waterApiEdit->text() != waterApiUrlValue) {
        waterApiEdit->setText(waterApiUrlValue);
    }

    const QUrl url(waterApiUrlValue);
    if (!url.isValid() || url.host().isEmpty()) {
        setWaterUnavailable(QStringLiteral("\u6c34\u8d28\u63a5\u53e3\u5730\u5740\u65e0\u6548"));
        return;
    }

    if (waterStatusLabel) {
        waterStatusLabel->setText(QStringLiteral("\u6b63\u5728\u8fde\u63a5"));
        waterStatusLabel->setStyleSheet(QStringLiteral("color:#BDFBE8;"));
    }
    if (waterGatewayLabel) {
        waterGatewayLabel->setText(url.host());
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("fish-pond-qt-water/1.0"));
    request.setAttribute(QNetworkRequest::User, waterApiUrlValue);
    QNetworkReply *reply = waterManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleWaterReply(reply);
    });
}

void Widget::handleWaterReply(QNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    const QScopedPointer<QNetworkReply, QScopedPointerDeleteLater> replyGuard(reply);
    const QString replyUrl = reply->request().attribute(QNetworkRequest::User).toString();
    if (!replyUrl.isEmpty() && replyUrl != waterApiUrlValue) {
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        setWaterUnavailable(QStringLiteral("\u6c34\u8d28\u7f51\u5173\u8fde\u63a5\u5931\u8d25"));
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
    if (!document.isObject()) {
        setWaterUnavailable(QStringLiteral("\u6c34\u8d28\u6570\u636e\u89e3\u6790\u5931\u8d25"));
        return;
    }

    const QJsonObject root = document.object();
    const QJsonObject data = root.value(QStringLiteral("data")).isObject()
        ? root.value(QStringLiteral("data")).toObject()
        : root;
    const QJsonObject metrics = data.value(QStringLiteral("metrics")).toObject();
    const QJsonObject gateway = data.value(QStringLiteral("gateway")).toObject();
    const QJsonArray sensors = data.value(QStringLiteral("sensors")).toArray();
    const QJsonObject codSensor = sensorObjectByAddress(sensors, QStringLiteral("01"));
    const QJsonObject phEcSensor = sensorObjectByAddress(sensors, QStringLiteral("02"));
    const QJsonObject ammoniaSensor = sensorObjectByAddress(sensors, QStringLiteral("03"));
    const QJsonObject nitriteSensor = sensorObjectByAddress(sensors, QStringLiteral("04"));
    const QJsonObject nitrateSensor = sensorObjectByAddress(sensors, QStringLiteral("05"));

    double waterTemp = 0.0;
    double ph = 0.0;
    double cod = 0.0;
    double ammonia = 0.0;
    double nitrite = 0.0;
    double nitrate = 0.0;
    double conductivity = 0.0;
    double tds = 0.0;
    double salinity = 0.0;

    bool hasWaterTemp = jsonObjectNumber(metrics, {QStringLiteral("waterTemp"), QStringLiteral("temperature"), QStringLiteral("temp")}, &waterTemp);
    if (!hasWaterTemp) {
        hasWaterTemp = jsonObjectNumber(phEcSensor, {QStringLiteral("temperatureC"), QStringLiteral("temperature"), QStringLiteral("tempC"), QStringLiteral("temp")}, &waterTemp);
    }
    const bool hasPh = jsonObjectNumber(metrics, {QStringLiteral("ph"), QStringLiteral("pH")}, &ph) ||
                       jsonObjectNumber(phEcSensor, {QStringLiteral("ph"), QStringLiteral("pH"), QStringLiteral("value")}, &ph);
    const bool hasCod = jsonObjectNumber(metrics, {QStringLiteral("cod"), QStringLiteral("codMgL")}, &cod) ||
                        jsonObjectNumber(codSensor, {QStringLiteral("cod"), QStringLiteral("codMgL"), QStringLiteral("value")}, &cod);
    const bool hasAmmonia = jsonObjectNumber(metrics, {QStringLiteral("ammonia"), QStringLiteral("nh4"), QStringLiteral("ammoniaPpm")}, &ammonia) ||
                            jsonObjectNumber(ammoniaSensor, {QStringLiteral("concentrationPpm"), QStringLiteral("value"), QStringLiteral("ammonia")}, &ammonia);
    const bool hasNitrite = jsonObjectNumber(metrics, {QStringLiteral("nitrite"), QStringLiteral("no2"), QStringLiteral("nitritePpm")}, &nitrite) ||
                            jsonObjectNumber(nitriteSensor, {QStringLiteral("concentrationPpm"), QStringLiteral("value"), QStringLiteral("nitrite")}, &nitrite);
    const bool hasNitrate = jsonObjectNumber(metrics, {QStringLiteral("nitrate"), QStringLiteral("no3"), QStringLiteral("nitratePpm")}, &nitrate) ||
                            jsonObjectNumber(nitrateSensor, {QStringLiteral("concentrationPpm"), QStringLiteral("value"), QStringLiteral("nitrate")}, &nitrate);
    const bool hasConductivity = jsonObjectNumber(metrics, {QStringLiteral("conductivity"), QStringLiteral("ec"), QStringLiteral("ecUsCm")}, &conductivity) ||
                                 jsonObjectNumber(phEcSensor, {QStringLiteral("conductivity"), QStringLiteral("ec"), QStringLiteral("ecUsCm")}, &conductivity);
    bool hasTds = jsonObjectNumber(metrics, {QStringLiteral("tds"), QStringLiteral("tdsPpm")}, &tds) ||
                  jsonObjectNumber(phEcSensor, {QStringLiteral("tds"), QStringLiteral("tdsPpm")}, &tds);
    bool hasSalinity = jsonObjectNumber(metrics, {QStringLiteral("salinity"), QStringLiteral("salinityPpt")}, &salinity) ||
                       jsonObjectNumber(phEcSensor, {QStringLiteral("salinity"), QStringLiteral("salinityPpt")}, &salinity);

    if (!hasTds && hasConductivity) {
        tds = conductivity * 0.5;
        hasTds = true;
    }
    if (!hasSalinity && hasTds) {
        salinity = tds / 1000.0;
        hasSalinity = true;
    }

    auto applyMetric = [this](bool hasValue,
                              QLabel *label,
                              QProgressBar *bar,
                              double value,
                              const QString &unit,
                              int digits,
                              double maxValue) {
        if (hasValue) {
            updateWaterMetric(label, bar, value, unit, digits, maxValue);
            return;
        }

        if (label) {
            label->setText(QStringLiteral("-- %1").arg(unit));
        }
        if (bar) {
            bar->setValue(0);
        }
    };

    applyMetric(hasWaterTemp, waterTempLabel, waterTempBar, waterTemp, QStringLiteral("\u2103"), 1, 40.0);
    applyMetric(hasPh, waterPhLabel, waterPhBar, ph, QStringLiteral("pH"), 2, 14.0);
    applyMetric(hasCod, waterCodLabel, waterCodBar, cod, QStringLiteral("mg/L"), 2, 50.0);
    applyMetric(hasAmmonia, waterAmmoniaLabel, waterAmmoniaBar, ammonia, QStringLiteral("ppm"), 3, 1.0);
    applyMetric(hasNitrite, waterNitriteLabel, waterNitriteBar, nitrite, QStringLiteral("ppm"), 3, 0.3);
    applyMetric(hasNitrate, waterNitrateLabel, waterNitrateBar, nitrate, QStringLiteral("ppm"), 2, 50.0);
    applyMetric(hasConductivity, waterConductivityLabel, waterConductivityBar, conductivity, QStringLiteral("uS/cm"), 0, 2500.0);
    applyMetric(hasTds, waterTdsLabel, waterTdsBar, tds, QStringLiteral("ppm"), 0, 1200.0);
    applyMetric(hasSalinity, waterSalinityLabel, waterSalinityBar, salinity, QStringLiteral("PPT"), 2, 2.0);

    int validMetricCount = 0;
    auto countMetric = [&validMetricCount](bool hasValue) {
        if (hasValue) {
            ++validMetricCount;
        }
    };
    countMetric(hasWaterTemp);
    countMetric(hasPh);
    countMetric(hasCod);
    countMetric(hasAmmonia);
    countMetric(hasNitrite);
    countMetric(hasNitrate);
    countMetric(hasConductivity);
    countMetric(hasTds);
    countMetric(hasSalinity);

    double onlineFromGateway = 0.0;
    int onlineCount = jsonObjectNumber(gateway, {QStringLiteral("onlineSensors")}, &onlineFromGateway)
        ? static_cast<int>(onlineFromGateway)
        : onlineSensorCount(sensors);

    const QString deviceIp = data.value(QStringLiteral("deviceIp")).toString(
        gateway.value(QStringLiteral("ip")).toString(QUrl(waterApiUrlValue).host()));
    const QString updateTime = data.value(QStringLiteral("updateTime")).toString(
        data.value(QStringLiteral("timestamp")).toString(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));

    if (waterGatewayLabel) {
        waterGatewayLabel->setText(deviceIp.isEmpty() ? QUrl(waterApiUrlValue).host() : deviceIp);
    }
    if (waterUpdateLabel) {
        waterUpdateLabel->setText(updateTime.isEmpty() ? QStringLiteral("--:--:--") : updateTime);
    }
    if (waterOnlineLabel) {
        waterOnlineLabel->setText(QStringLiteral("%1 / 5").arg(onlineCount));
    }
    if (waterStatusLabel) {
        if (validMetricCount > 0) {
            waterStatusLabel->setText(QStringLiteral("\u5728\u7ebf"));
            waterStatusLabel->setStyleSheet(QStringLiteral("color:#4EE6BA;"));
        } else {
            waterStatusLabel->setText(QStringLiteral("\u79bb\u7ebf"));
            waterStatusLabel->setStyleSheet(QStringLiteral("color:#FFB86B;"));
        }
    }
}

void Widget::setWaterUnavailable(const QString &statusText)
{
    Q_UNUSED(statusText);
    if (waterStatusLabel) {
        waterStatusLabel->setText(QStringLiteral("离线"));
        waterStatusLabel->setStyleSheet(QStringLiteral("color:#FFB86B;"));
    }
    if (waterGatewayLabel) {
        waterGatewayLabel->setText(QUrl(waterApiUrlValue).host());
    }
}

void Widget::updateWaterMetric(QLabel *label,
                               QProgressBar *bar,
                               double value,
                               const QString &unit,
                               int digits,
                               double maxValue)
{
    if (label) {
        label->setText(QStringLiteral("%1 %2").arg(value, 0, 'f', digits).arg(unit));
    }

    if (bar) {
        const double percent = maxValue > 0.0 ? value * 100.0 / maxValue : 0.0;
        bar->setValue(qBound(0, static_cast<int>(percent + 0.5), 100));
    }
}

QString Widget::weatherCodeText(int code) const
{
    if (code == 0) {
        return QStringLiteral("\u6674");
    }
    if (code == 1 || code == 2) {
        return QStringLiteral("\u591a\u4e91");
    }
    if (code == 3) {
        return QStringLiteral("\u9634");
    }
    if (code == 45 || code == 48) {
        return QStringLiteral("\u96fe");
    }
    if ((code >= 51 && code <= 57) || (code >= 61 && code <= 67)) {
        return QStringLiteral("\u5c0f\u96e8");
    }
    if (code >= 71 && code <= 77) {
        return QStringLiteral("\u964d\u96ea");
    }
    if (code >= 80 && code <= 82) {
        return QStringLiteral("\u9635\u96e8");
    }
    if (code >= 95) {
        return QStringLiteral("\u96f7\u66b4");
    }
    return QStringLiteral("\u672a\u77e5");
}

QString Widget::windDirectionText(double degree) const
{
    const QStringList directions = {
        QStringLiteral("\u5317\u98ce"),
        QStringLiteral("\u4e1c\u5317\u98ce"),
        QStringLiteral("\u4e1c\u98ce"),
        QStringLiteral("\u4e1c\u5357\u98ce"),
        QStringLiteral("\u5357\u98ce"),
        QStringLiteral("\u897f\u5357\u98ce"),
        QStringLiteral("\u897f\u98ce"),
        QStringLiteral("\u897f\u5317\u98ce")
    };

    int index = static_cast<int>((degree + 22.5) / 45.0) % directions.size();
    return directions.at(index);
}

void Widget::updateSerialStatus(const QString &detail)
{
    const bool ok = serial && serial->isOpen();
    Q_UNUSED(detail);
    const QString text = ok ? QStringLiteral("\u4e32\u53e3\u8fde\u63a5\u6b63\u5e38")
                            : QStringLiteral("\u4e32\u53e3\u8fde\u63a5\u5f02\u5e38");
    const QString style = ok
        ? QStringLiteral("color:#BDFBE8; background:rgba(78,230,186,36);")
        : QStringLiteral("color:#FFD0D0; background:rgba(255,107,107,36);");

    if (serialStatusLabel) {
        serialStatusLabel->setText(text);
        serialStatusLabel->setStyleSheet(style);
    }
    if (serialDetailLabel) {
        serialDetailLabel->setText(ok ? QStringLiteral("\u5df2\u5c31\u7eea") : QStringLiteral("\u672a\u5c31\u7eea"));
        serialDetailLabel->setStyleSheet(ok ? QStringLiteral("color:#4EE6BA;") : QStringLiteral("color:#FF8A8A;"));
    }
}

bool Widget::ensureSerialOpen()
{
    if (!serial) {
        serial = new QSerialPort(this);
    }

    if (serial->isOpen()) {
        updateSerialStatus(QStringLiteral("%1").arg(kPiSerialPort));
        return true;
    }

    serial->setPortName(QString::fromLatin1(kPiSerialPort));
    serial->setBaudRate(kPiUartBaud);
    serial->setDataBits(QSerialPort::Data8);
    serial->setParity(QSerialPort::NoParity);
    serial->setStopBits(QSerialPort::OneStop);
    serial->setFlowControl(QSerialPort::NoFlowControl);

    const bool ok = serial->open(QIODevice::WriteOnly);
    updateSerialStatus(ok ? QStringLiteral("%1").arg(kPiSerialPort) : serial->errorString());
    return ok;
}

void Widget::initSerial()
{
    ensureSerialOpen();
}

void Widget::initPumpGpio()
{
    setPumpRelay(false, false);
}

bool Widget::setPumpRelay(bool open, bool updateLastCommand)
{
    QString error;
    const bool ok = writePumpGpioValue(open, &error);

    if (pumpStatusLabel) {
        if (ok) {
            pumpStatusLabel->setText(open ? QStringLiteral("\u5df2\u6253\u5f00")
                                          : QStringLiteral("\u5df2\u5173\u95ed"));
            pumpStatusLabel->setStyleSheet(open ? QStringLiteral("color:#FFE08A;")
                                                : QStringLiteral("color:#4EE6BA;"));
        } else {
            pumpStatusLabel->setText(open ? QStringLiteral("\u6253\u5f00\u5931\u8d25")
                                          : QStringLiteral("\u5df2\u5173\u95ed"));
            pumpStatusLabel->setStyleSheet(open ? QStringLiteral("color:#FF8A8A;")
                                                : QStringLiteral("color:#4EE6BA;"));
        }
    }

    if (updateLastCommand && lastCommandLabel) {
        if (ok) {
            lastCommandLabel->setText(open ? QStringLiteral("\u6c34\u6cf5\u6253\u5f00")
                                           : QStringLiteral("\u6c34\u6cf5\u5173\u95ed"));
        } else {
            lastCommandLabel->setText(QStringLiteral("\u6c34\u6cf5\u6307\u4ee4\u5931\u8d25"));
        }
    }

    Q_UNUSED(error);
    return ok;
}

bool Widget::writePumpGpioValue(bool open, QString *error)
{
    QString sysfsError;
    if (writePumpGpioSysfs(open, &sysfsError)) {
        return true;
    }

    QString commandError;
    if (writePumpGpioCommand(open, &commandError)) {
        return true;
    }

    if (error) {
        *error = commandError.isEmpty() ? sysfsError : commandError;
    }
    return false;
}

bool Widget::writePumpGpioSysfs(bool open, QString *error)
{
    const QString gpioDir = pumpGpioDirectory();
    if (!QDir(gpioDir).exists()) {
        QString exportError;
        if (!writeTextFile(QStringLiteral("/sys/class/gpio/export"),
                           QByteArray::number(kPumpGpioBcm),
                           &exportError) &&
            !QDir(gpioDir).exists()) {
            if (error) {
                *error = exportError;
            }
            return false;
        }
    }

    if (!QDir(gpioDir).exists()) {
        if (error) {
            *error = QStringLiteral("\u672a\u627e\u5230 GPIO%1").arg(kPumpGpioBcm);
        }
        return false;
    }

    const QByteArray direction = open ? QByteArrayLiteral("out")
                                      : QByteArrayLiteral("high");
    if (!writeTextFile(gpioDir + QStringLiteral("/direction"), direction, error)) {
        return false;
    }

    const QByteArray value = open ? QByteArrayLiteral("0")
                                  : QByteArrayLiteral("1");
    return writeTextFile(gpioDir + QStringLiteral("/value"), value, error);
}

bool Widget::writePumpGpioCommand(bool open, QString *error) const
{
    const QString pin = QString::number(kPumpGpioBcm);
    const QString level = open ? QStringLiteral("dl") : QStringLiteral("dh");
    QStringList failures;
    QString output;
    QString commandError;

    const QString pinctrl = QStandardPaths::findExecutable(QStringLiteral("pinctrl"));
    if (!pinctrl.isEmpty()) {
        if (runProcess(pinctrl,
                       {QStringLiteral("set"), pin, QStringLiteral("op"), level},
                       &output,
                       &commandError,
                       3000)) {
            return true;
        }
        failures.append(commandError);
    }

    const QString raspiGpio = QStandardPaths::findExecutable(QStringLiteral("raspi-gpio"));
    if (!raspiGpio.isEmpty()) {
        if (runProcess(raspiGpio,
                       {QStringLiteral("set"), pin, QStringLiteral("op"), level},
                       &output,
                       &commandError,
                       3000)) {
            return true;
        }
        failures.append(commandError);
    }

    const QString gpio = QStandardPaths::findExecutable(QStringLiteral("gpio"));
    if (!gpio.isEmpty()) {
        const QString value = open ? QStringLiteral("0") : QStringLiteral("1");
        const bool modeOk = runProcess(gpio,
                                       {QStringLiteral("-g"), QStringLiteral("mode"), pin, QStringLiteral("out")},
                                       &output,
                                       &commandError,
                                       3000);
        if (modeOk &&
            runProcess(gpio,
                       {QStringLiteral("-g"), QStringLiteral("write"), pin, value},
                       &output,
                       &commandError,
                       3000)) {
            return true;
        }
        failures.append(commandError);
    }

    if (error) {
        failures.removeAll(QString());
        *error = failures.isEmpty()
            ? QStringLiteral("\u672a\u627e\u5230\u53ef\u7528\u7684 GPIO \u63a7\u5236\u547d\u4ee4")
            : failures.join(QStringLiteral("; "));
    }
    return false;
}

bool Widget::sendPacket(uint8_t cmd, const QString &actionName)
{
    if (!ensureSerialOpen()) {
        if (lastCommandLabel) {
            lastCommandLabel->setText(QStringLiteral("\u6307\u4ee4\u53d1\u9001\u5931\u8d25"));
        }
        return false;
    }

    QByteArray packet;
    packet.append(static_cast<char>(kFrameHead));
    packet.append(static_cast<char>(cmd));
    packet.append(static_cast<char>(kFrameTail));

    const qint64 written = serial->write(packet);
    const bool ok = written == packet.size() && serial->waitForBytesWritten(100);

    if (ok) {
        if (lastCommandLabel) {
            lastCommandLabel->setText(actionName);
        }
    } else {
        updateSerialStatus(serial->errorString());
        if (lastCommandLabel) {
            lastCommandLabel->setText(QStringLiteral("\u6307\u4ee4\u53d1\u9001\u5931\u8d25"));
        }
    }

    return ok;
}

void Widget::startRemoteControlServer()
{
    if (!remoteControlServer) {
        remoteControlServer = new QTcpServer(this);
        connect(remoteControlServer, &QTcpServer::newConnection,
                this, &Widget::handleRemoteControlConnection);
    }

    if (remoteControlServer->isListening()) {
        return;
    }

    if (remoteControlServer->listen(QHostAddress::AnyIPv4, kRemoteControlPort)) {
    } else {
    }
}

void Widget::handleRemoteControlConnection()
{
    while (remoteControlServer && remoteControlServer->hasPendingConnections()) {
        QTcpSocket *socket = remoteControlServer->nextPendingConnection();
        socket->setParent(this);

        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            handleRemoteControlRequest(socket);
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void Widget::handleRemoteControlRequest(QTcpSocket *socket)
{
    if (!socket || !socket->bytesAvailable()) {
        return;
    }

    QByteArray request = socket->property("requestBuffer").toByteArray();
    request.append(socket->readAll());
    if (!request.contains("\r\n\r\n")) {
        socket->setProperty("requestBuffer", request);
        return;
    }

    const QList<QByteArray> lines = request.split('\n');
    const QByteArray firstLine = lines.value(0).trimmed();
    const QList<QByteArray> parts = firstLine.split(' ');

    QJsonObject response;
    if (parts.size() < 2) {
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("message"), QStringLiteral("\u8bf7\u6c42\u683c\u5f0f\u4e0d\u6b63\u786e"));
        writeHttpJson(socket, 400, response);
        return;
    }

    const QString method = QString::fromLatin1(parts.at(0)).toUpper();
    if (method == QStringLiteral("OPTIONS")) {
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("message"), QStringLiteral("ok"));
        writeHttpJson(socket, 204, response);
        return;
    }

    if (method != QStringLiteral("GET")) {
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("message"), QStringLiteral("\u4ec5\u652f\u6301 GET \u8bf7\u6c42"));
        writeHttpJson(socket, 400, response);
        return;
    }

    const QString target = QUrl::fromPercentEncoding(parts.at(1));
    const QUrl url(QStringLiteral("http://127.0.0.1%1").arg(target));
    const QUrlQuery query(url);
    const QString path = url.path();

    if (path == QStringLiteral("/status")) {
        const bool serialReady = ensureSerialOpen();
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("message"), QStringLiteral("online"));
        response.insert(QStringLiteral("serial"), serialReady);
        response.insert(QStringLiteral("serialError"), serialReady || !serial ? QString() : serial->errorString());
        writeHttpJson(socket, 200, response);
        return;
    }

    QString cmd;
    if (path == QStringLiteral("/control")) {
        cmd = query.queryItemValue(QStringLiteral("cmd")).trimmed().toLower();
    } else if (path.startsWith(QStringLiteral("/control/"))) {
        cmd = path.section(QLatin1Char('/'), -1).trimmed().toLower();
    }

    if (cmd.isEmpty()) {
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("message"), QStringLiteral("\u7f3a\u5c11 cmd \u63a7\u5236\u53c2\u6570"));
        writeHttpJson(socket, 400, response);
        return;
    }

    QString message;
    const bool ok = executeRemoteControl(cmd, &message);
    response.insert(QStringLiteral("ok"), ok);
    response.insert(QStringLiteral("cmd"), cmd);
    response.insert(QStringLiteral("message"), message);
    writeHttpJson(socket, ok ? 200 : 400, response);
}

bool Widget::executeRemoteControl(const QString &cmd, QString *message)
{
    const QString normalized = cmd.trimmed().toLower();
    bool ok = false;
    QString actionName;

    if (normalized == QStringLiteral("front") || normalized == QStringLiteral("forward")) {
        actionName = QStringLiteral("\u6e14\u7f51\u524d\u8fdb");
        ok = sendPacket(kCmdForward, actionName);
        if (ok && motionStatusLabel) {
            motionStatusLabel->setText(QStringLiteral("\u6b63\u5728\u524d\u8fdb"));
            motionStatusLabel->setStyleSheet(QStringLiteral("color:#4EE6BA;"));
        }
    } else if (normalized == QStringLiteral("reverse") || normalized == QStringLiteral("back")) {
        actionName = QStringLiteral("\u6e14\u7f51\u540e\u9000");
        ok = sendPacket(kCmdReverse, actionName);
        if (ok && motionStatusLabel) {
            motionStatusLabel->setText(QStringLiteral("\u6b63\u5728\u540e\u9000"));
            motionStatusLabel->setStyleSheet(QStringLiteral("color:#7DB8FF;"));
        }
    } else if (normalized == QStringLiteral("stop")) {
        actionName = QStringLiteral("\u6e14\u7f51\u505c\u6b62");
        ok = sendPacket(kCmdStop, actionName);
        if (ok && motionStatusLabel) {
            motionStatusLabel->setText(QStringLiteral("\u5df2\u505c\u6b62"));
            motionStatusLabel->setStyleSheet(QStringLiteral("color:#FFB86B;"));
        }
    } else if (normalized == QStringLiteral("light_on") || normalized == QStringLiteral("led_on")) {
        actionName = QStringLiteral("\u6253\u5f00\u8865\u5149");
        ok = sendPacket(kCmdLedOn, actionName);
        if (ok && ledStatusLabel) {
            ledStatusLabel->setText(QStringLiteral("\u5df2\u6253\u5f00"));
            ledStatusLabel->setStyleSheet(QStringLiteral("color:#FFE08A;"));
        }
    } else if (normalized == QStringLiteral("light_off") || normalized == QStringLiteral("led_off")) {
        actionName = QStringLiteral("\u5173\u95ed\u8865\u5149");
        ok = sendPacket(kCmdLedOff, actionName);
        if (ok && ledStatusLabel) {
            ledStatusLabel->setText(QStringLiteral("\u5df2\u5173\u95ed"));
            ledStatusLabel->setStyleSheet(QStringLiteral("color:#4EE6BA;"));
        }
    } else {
        if (message) {
            *message = QStringLiteral("\u4e0d\u652f\u6301\u7684\u63a7\u5236\u6307\u4ee4");
        }
        return false;
    }

    if (message) {
        *message = ok ? QStringLiteral("\u6307\u4ee4\u5df2\u53d1\u9001\uff1a%1").arg(actionName)
                      : QStringLiteral("\u6307\u4ee4\u53d1\u9001\u5931\u8d25\uff1a%1").arg(actionName);
    }
    return ok;
}

void Widget::time_reflash()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QString date = now.toString(QStringLiteral("yyyy-MM-dd"));
    const QString time = now.toString(QStringLiteral("HH:mm:ss"));

    if (labelDate) {
        labelDate->setText(date);
    }
    if (labelTime) {
        labelTime->setText(time);
    }
    if (loginClockLabel) {
        loginClockLabel->setText(QStringLiteral("%1  %2").arg(date, time));
    }
}

void Widget::handleLedOpen()
{
    if (sendPacket(kCmdLedOn, QStringLiteral("\u6253\u5f00\u8865\u5149"))) {
        if (ledStatusLabel) {
            ledStatusLabel->setText(QStringLiteral("\u5df2\u6253\u5f00"));
            ledStatusLabel->setStyleSheet(QStringLiteral("color:#FFE08A;"));
        }
    }
}

void Widget::handleLedClose()
{
    if (sendPacket(kCmdLedOff, QStringLiteral("\u5173\u95ed\u8865\u5149"))) {
        if (ledStatusLabel) {
            ledStatusLabel->setText(QStringLiteral("\u5df2\u5173\u95ed"));
            ledStatusLabel->setStyleSheet(QStringLiteral("color:#4EE6BA;"));
        }
    }
}

void Widget::handleMotorForward()
{
    if (sendPacket(kCmdForward, QStringLiteral("\u6e14\u7f51\u524d\u8fdb"))) {
        if (motionStatusLabel) {
            motionStatusLabel->setText(QStringLiteral("\u6b63\u5728\u524d\u8fdb"));
            motionStatusLabel->setStyleSheet(QStringLiteral("color:#4EE6BA;"));
        }
    }
}

void Widget::handleMotorReverse()
{
    if (sendPacket(kCmdReverse, QStringLiteral("\u6e14\u7f51\u540e\u9000"))) {
        if (motionStatusLabel) {
            motionStatusLabel->setText(QStringLiteral("\u6b63\u5728\u540e\u9000"));
            motionStatusLabel->setStyleSheet(QStringLiteral("color:#7DB8FF;"));
        }
    }
}

void Widget::handleMotorStop()
{
    if (sendPacket(kCmdStop, QStringLiteral("\u6e14\u7f51\u505c\u6b62"))) {
        if (motionStatusLabel) {
            motionStatusLabel->setText(QStringLiteral("\u5df2\u505c\u6b62"));
            motionStatusLabel->setStyleSheet(QStringLiteral("color:#FFB86B;"));
        }
    }
}

void Widget::handlePumpOpen()
{
    setPumpRelay(true);
}

void Widget::handlePumpClose()
{
    setPumpRelay(false);
}

Widget::~Widget()
{
    setPumpRelay(false, false);

    if (remoteControlServer && remoteControlServer->isListening()) {
        remoteControlServer->close();
    }
    if (serial && serial->isOpen()) {
        serial->close();
    }

    delete ui;
}
