#pragma once

#include <QtCore/QStringList>
#include <QtQmlIntegration/QtQmlIntegration>

#include "QmlObjectListModel.h"
#include "SettingsGroup.h"

class AdditionalVideoSourceSettings : public QObject
{
    Q_OBJECT

public:
    AdditionalVideoSourceSettings(int sourceId, QObject* parent = nullptr);

    Q_PROPERTY(int sourceId READ sourceId CONSTANT)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(QString videoSource READ videoSource WRITE setVideoSource NOTIFY videoSourceChanged)
    Q_PROPERTY(QString uri READ uri WRITE setUri NOTIFY uriChanged)
    Q_PROPERTY(bool lowLatencyMode READ lowLatencyMode WRITE setLowLatencyMode NOTIFY lowLatencyModeChanged)
    Q_PROPERTY(int rtpJitterLatencyMs READ rtpJitterLatencyMs WRITE setRtpJitterLatencyMs NOTIFY rtpJitterLatencyMsChanged)
    Q_PROPERTY(bool rtspAutoReconnect READ rtspAutoReconnect WRITE setRtspAutoReconnect NOTIFY rtspAutoReconnectChanged)
    Q_PROPERTY(QString receiverName READ receiverName CONSTANT)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)

    int sourceId() const { return _sourceId; }
    QString name() const { return _name; }
    QString videoSource() const { return _videoSource; }
    QString uri() const { return _uri; }
    bool lowLatencyMode() const { return _lowLatencyMode; }
    int rtpJitterLatencyMs() const { return _rtpJitterLatencyMs; }
    bool rtspAutoReconnect() const { return _rtspAutoReconnect; }
    QString receiverName() const;
    bool dirty() const { return false; }

    void setName(const QString& name);
    void setVideoSource(const QString& videoSource);
    void setUri(const QString& uri);
    void setLowLatencyMode(bool lowLatencyMode);
    void setRtpJitterLatencyMs(int rtpJitterLatencyMs);
    void setRtspAutoReconnect(bool rtspAutoReconnect);
    void removeSettings();

signals:
    void nameChanged();
    void videoSourceChanged();
    void uriChanged();
    void lowLatencyModeChanged();
    void rtpJitterLatencyMsChanged();
    void rtspAutoReconnectChanged();
    void dirtyChanged(bool dirty);

private:
    void _load();
    void _save();

    int _sourceId = 0;
    QString _name;
    QString _videoSource;
    QString _uri;
    bool _lowLatencyMode = false;
    int _rtpJitterLatencyMs = 80;
    bool _rtspAutoReconnect = true;
};

class VideoSettings : public SettingsGroup
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
public:
    VideoSettings(QObject* parent = nullptr);
    DEFINE_SETTING_NAME_GROUP()

    DEFINE_SETTINGFACT(videoSource)
    DEFINE_SETTINGFACT(udpUrl)
    DEFINE_SETTINGFACT(tcpUrl)
    DEFINE_SETTINGFACT(rtspUrl)
    DEFINE_SETTINGFACT(aspectRatio)
    DEFINE_SETTINGFACT(videoFit)
    DEFINE_SETTINGFACT(gridLines)
    DEFINE_SETTINGFACT(showRecControl)
    DEFINE_SETTINGFACT(recordingFormat)
    DEFINE_SETTINGFACT(maxVideoSize)
    DEFINE_SETTINGFACT(enableStorageLimit)
    DEFINE_SETTINGFACT(rtspTimeout)
    DEFINE_SETTINGFACT(streamEnabled)
    DEFINE_SETTINGFACT(disableWhenDisarmed)
    DEFINE_SETTINGFACT(lowLatencyMode)
    DEFINE_SETTINGFACT(rtpJitterLatencyMs)
    DEFINE_SETTINGFACT(rtspAutoReconnect)
    DEFINE_SETTINGFACT(forceVideoDecoder)
    DEFINE_SETTINGFACT(forceCpuVideoPath)
    DEFINE_SETTINGFACT(videoConversionElement)
    DEFINE_SETTINGFACT(disablePixelAspectRatio)

    Q_PROPERTY(bool     streamConfigured        READ streamConfigured       NOTIFY streamConfiguredChanged)
    Q_PROPERTY(QString  rtspVideoSource         READ rtspVideoSource        CONSTANT)
    Q_PROPERTY(QString  udp264VideoSource       READ udp264VideoSource      CONSTANT)
    Q_PROPERTY(QString  udp265VideoSource       READ udp265VideoSource      CONSTANT)
    Q_PROPERTY(QString  tcpVideoSource          READ tcpVideoSource         CONSTANT)
    Q_PROPERTY(QString  mpegtsVideoSource       READ mpegtsVideoSource      CONSTANT)
    Q_PROPERTY(QString  disabledVideoSource     READ disabledVideoSource    CONSTANT)
    Q_PROPERTY(QStringList additionalVideoSourceTypes READ additionalVideoSourceTypes CONSTANT)
    Q_PROPERTY(QStringList additionalVideoSourceTypeNames READ additionalVideoSourceTypeNames CONSTANT)
    Q_PROPERTY(QmlObjectListModel* additionalVideoSources READ additionalVideoSources NOTIFY additionalVideoSourcesChanged)

    bool     streamConfigured       ();
    QString  rtspVideoSource        () { return videoSourceRTSP; }
    QString  udp264VideoSource      () { return videoSourceUDPH264; }
    QString  udp265VideoSource      () { return videoSourceUDPH265; }
    QString  tcpVideoSource         () { return videoSourceTCP; }
    QString  mpegtsVideoSource      () { return videoSourceMPEGTS; }
    QString  disabledVideoSource    () { return videoDisabled; }
    QStringList additionalVideoSourceTypes();
    QStringList additionalVideoSourceTypeNames();
    QmlObjectListModel* additionalVideoSources() { return &_additionalVideoSources; }

    Q_INVOKABLE void addAdditionalVideoSource();
    Q_INVOKABLE void removeAdditionalVideoSource(int index);
    Q_INVOKABLE bool additionalVideoSourceIsUvc(const QString& videoSource) const;
    Q_INVOKABLE bool additionalVideoSourceUsesUri(const QString& videoSource) const;

    /// Remove hardware forced-decoder options absent from the running GStreamer registry, and
    /// reset the active choice to Default if it was pruned. Call after the video backend has
    /// initialized (the registry is empty until then).
    void pruneUnavailableDecoders();

    static constexpr const char* videoSourceNoVideo           = QT_TRANSLATE_NOOP("VideoSettings", "No Video Available");
    static constexpr const char* videoDisabled                = QT_TRANSLATE_NOOP("VideoSettings", "Video Stream Disabled");
    static constexpr const char* videoSourceRTSP              = QT_TRANSLATE_NOOP("VideoSettings", "RTSP Video Stream");
    static constexpr const char* videoSourceUDPH264           = QT_TRANSLATE_NOOP("VideoSettings", "UDP h.264 Video Stream");
    static constexpr const char* videoSourceUDPH265           = QT_TRANSLATE_NOOP("VideoSettings", "UDP h.265 Video Stream");
    static constexpr const char* videoSourceTCP               = QT_TRANSLATE_NOOP("VideoSettings", "TCP-MPEG2 Video Stream");
    static constexpr const char* videoSourceMPEGTS            = QT_TRANSLATE_NOOP("VideoSettings", "MPEG-TS Video Stream");
    static constexpr const char* videoSource3DRSolo           = QT_TRANSLATE_NOOP("VideoSettings", "3DR Solo (requires restart)");
    static constexpr const char* videoSourceParrotDiscovery   = QT_TRANSLATE_NOOP("VideoSettings", "Parrot Discovery");
    static constexpr const char* videoSourceYuneecMantisG     = QT_TRANSLATE_NOOP("VideoSettings", "Yuneec Mantis G");
    static constexpr const char* videoSourceHerelinkAirUnit   = QT_TRANSLATE_NOOP("VideoSettings", "Herelink AirUnit");
    static constexpr const char* videoSourceHerelinkHotspot   = QT_TRANSLATE_NOOP("VideoSettings", "Herelink Hotspot");

signals:
    void streamConfiguredChanged    (bool configured);
    void additionalVideoSourcesChanged();

private slots:
    void _configChanged             (QVariant value);

private:
    void _setDefaults               ();
    void _setForceVideoDecodeList();
    void _loadAdditionalVideoSources();
    void _saveAdditionalVideoSourceIds() const;

private:
    bool _noVideo = false;
    QmlObjectListModel _additionalVideoSources;
    int _nextAdditionalVideoSourceId = 1;

};
