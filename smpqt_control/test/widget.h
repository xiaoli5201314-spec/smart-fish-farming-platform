#ifndef WIDGET_H
#define WIDGET_H

#include <QSerialPort>
#include <QPixmap>
#include <QString>
#include <QList>
#include <QTimer>
#include <QtGlobal>
#include <QWidget>

#include <cstdint>

class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QObject;
class QEvent;
class QPaintEvent;
class QPlainTextEdit;
class QProgressBar;
class QProcess;
class QPushButton;
class QStackedWidget;
class QTcpServer;
class QTcpSocket;

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget();

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void time_reflash();
    void handleLedOpen();
    void handleLedClose();
    void handleMotorForward();
    void handleMotorReverse();
    void handleMotorStop();
    void handlePumpOpen();
    void handlePumpClose();

private:
    Ui::Widget *ui;
    QTimer *getSysTimeTimer;
    QTimer *deviceStatusTimer;
    QTimer *weatherTimer;
    QTimer *waterTimer;
    QTimer *voiceRecordStopTimer;
    QNetworkAccessManager *weatherManager;
    QNetworkAccessManager *assistantManager;
    QNetworkAccessManager *waterManager;
    QTcpServer *remoteControlServer;
    QSerialPort *serial;

    QStackedWidget *rootStack;
    QStackedWidget *contentStack;
    QLabel *labelDate;
    QLabel *labelTime;
    QLabel *loginTitleLabel;
    QLabel *loginClockLabel;
    QLabel *serialStatusLabel;
    QLabel *serialDetailLabel;
    QLabel *motionStatusLabel;
    QLabel *ledStatusLabel;
    QLabel *pumpStatusLabel;
    QLabel *lastCommandLabel;
    QLabel *piTempLabel;
    QLabel *memoryUsageLabel;
    QLabel *cpuUsageLabel;
    QLabel *networkSpeedLabel;
    QLabel *wifiIpLabel;
    QLabel *weatherStatusLabel;
    QLabel *weatherTempLabel;
    QLabel *weatherTextLabel;
    QLabel *weatherHumidityLabel;
    QLabel *weatherWindLabel;
    QLabel *weatherRainLabel;
    QLabel *weatherUpdateLabel;
    QLabel *weatherTitleLabel;
    QLabel *voiceStatusLabel;
    QLabel *waterStatusLabel;
    QLabel *waterGatewayLabel;
    QLabel *waterUpdateLabel;
    QLabel *waterOnlineLabel;
    QLabel *waterTempLabel;
    QLabel *waterPhLabel;
    QLabel *waterCodLabel;
    QLabel *waterAmmoniaLabel;
    QLabel *waterNitriteLabel;
    QLabel *waterNitrateLabel;
    QLabel *waterConductivityLabel;
    QLabel *waterTdsLabel;
    QLabel *waterSalinityLabel;
    QProgressBar *piTempBar;
    QProgressBar *memoryUsageBar;
    QProgressBar *cpuUsageBar;
    QProgressBar *networkSpeedBar;
    QProgressBar *waterTempBar;
    QProgressBar *waterPhBar;
    QProgressBar *waterCodBar;
    QProgressBar *waterAmmoniaBar;
    QProgressBar *waterNitriteBar;
    QProgressBar *waterNitrateBar;
    QProgressBar *waterConductivityBar;
    QProgressBar *waterTdsBar;
    QProgressBar *waterSalinityBar;
    QLabel *operatorLabel;
    QLabel *loginHintLabel;
    QLineEdit *accountEdit;
    QLineEdit *passwordEdit;
    QLineEdit *waterApiEdit;
    QLineEdit *activeInputEdit;
    QPlainTextEdit *voiceQuestionView;
    QPlainTextEdit *voiceAnswerView;
    QPushButton *navWaterButton;
    QPushButton *navSettingButton;
    QPushButton *navRecordButton;
    QPushButton *navWeatherButton;
    QPushButton *weatherCityButton;
    QPushButton *voiceStartButton;
    QPushButton *voiceStopButton;
    QPushButton *voiceReplayButton;
    QList<QPushButton *> voiceQuickQuestionButtons;
    QProcess *voiceRecordProcess;
    QProcess *voicePlayProcess;
    QList<QLabel *> weatherHourlyLabels;
    QPixmap loginBackgroundPixmap;
    QPixmap appBackgroundPixmap;

    QString currentUser;
    QString cachedWifiIp;
    QString lastVoiceTranscript;
    QString lastVoiceAnswer;
    QString lastVoiceTtsText;
    QString weatherLocationName;
    QString waterApiUrlValue;
    double weatherLatitude;
    double weatherLongitude;
    quint64 lastCpuTotal;
    quint64 lastCpuIdle;
    quint64 lastNetBytes;
    qint64 lastNetSampleMs;
    qint64 lastWifiStatusRefreshMs;
    bool voiceRecordingActive;
    bool voiceSubmitAfterStop;
    bool voiceRequestRunning;

    void buildUi();
    QWidget *createLoginPage();
    QWidget *createMainShell();
    QWidget *createWaterPage();
    QWidget *createControlPage();
    QWidget *createSettingPage();
    QWidget *createVoicePage();
    QWidget *createWeatherPage();

    QPushButton *createActionButton(const QString &text, const QString &objectName);
    QPushButton *createNavButton(const QString &text);
    QLabel *createCaption(const QString &text);

    void showTouchKeyboard(QLineEdit *targetEdit);
    void handleLogin();
    void showContentPage(int index, QPushButton *activeButton);
    void setActiveNav(QPushButton *activeButton);
    void updateDeviceStatus();
    void updateWifiSummary(bool force);
    void loadWeatherLocation();
    void saveWeatherLocation();
    void applyWeatherLocation(const QString &name, double latitude, double longitude, bool save);
    void updateWeatherLocationUi();
    void showWeatherCityDialog();
    void requestWeather();
    void handleWeatherReply(QNetworkReply *reply);
    void setWeatherUnavailable(const QString &statusText);
    void loadWaterApiUrl();
    void saveWaterApiUrl();
    QString normalizedWaterApiUrl(const QString &rawText) const;
    void requestWaterData();
    void handleWaterReply(QNetworkReply *reply);
    void setWaterUnavailable(const QString &statusText);
    void updateWaterMetric(QLabel *label,
                           QProgressBar *bar,
                           double value,
                           const QString &unit,
                           int digits,
                           double maxValue);
    void refreshBackground();
    QString weatherCodeText(int code) const;
    QString windDirectionText(double degree) const;
    QString assistantChatUrl() const;
    QString voiceApiUrl() const;
    QString voiceAudioDevice() const;
    QString voiceNaturalTtsProgram() const;
    QString voiceInputFilePath() const;
    QString voiceOutputFilePath() const;
    QString buildVoiceContext() const;
    QString readCpuTemperatureText() const;
    QString readMemoryUsageText() const;
    QString readCpuUsageText();
    QString readNetworkSpeedText();
    QString readWifiInterfaceName() const;
    QString readWifiIpText() const;
    bool readNetworkBytes(quint64 *totalBytes) const;
    bool runProcess(const QString &program,
                    const QStringList &arguments,
                    QString *standardOutput,
                    QString *standardError,
                    int timeoutMs = 12000) const;
    void updateSerialStatus(const QString &detail = QString());
    void updateVoiceUiState();
    void clearVoiceConversation();
    void submitQuickQuestion(const QString &question);
    void startVoiceRecording();
    void stopVoiceRecording(bool submitAfterStop);
    void submitVoiceAudio();
    void handleQuickQuestionReply(QNetworkReply *reply, const QString &question);
    void handleVoiceReply(QNetworkReply *reply);
    void applyAssistantAnswer(const QString &question,
                              const QString &answer,
                              const QString &ttsText);
    void playVoiceReply(const QString &text);
    void stopVoicePlayback();
    bool ensureSerialOpen();
    bool sendPacket(uint8_t cmd, const QString &actionName);
    void initSerial();
    void initPumpGpio();
    bool setPumpRelay(bool open, bool updateLastCommand = true);
    bool writePumpGpioValue(bool open, QString *error);
    bool writePumpGpioSysfs(bool open, QString *error);
    bool writePumpGpioCommand(bool open, QString *error) const;
    void startRemoteControlServer();
    void handleRemoteControlConnection();
    void handleRemoteControlRequest(QTcpSocket *socket);
    bool executeRemoteControl(const QString &cmd, QString *message);
};

#endif // WIDGET_H
