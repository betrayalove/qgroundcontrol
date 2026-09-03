#include "VideoSettings.h"
#include "VideoManager.h"

#include "QGCLoggingCategory.h"

#include <algorithm>
#include <climits>

#include <QtCore/QSettings>
#include <QtCore/QStringList>
#include <QtCore/QVariantList>

QGC_LOGGING_CATEGORY(VideoSettingsLog, "Settings.VideoSettings")

namespace {

constexpr const char* kAdditionalVideoSourcesGroup = "AdditionalVideoSources";
constexpr const char* kAdditionalVideoSourceIdsKey = "ids";
constexpr const char* kAdditionalVideoSourceNameKey = "name";
constexpr const char* kAdditionalVideoSourceTypeKey = "videoSource";
constexpr const char* kAdditionalVideoSourceUriKey = "uri";
constexpr const char* kAdditionalVideoSourceStreamEnabledKey = "streamEnabled";
constexpr const char* kAdditionalVideoSourceLowLatencyKey = "lowLatencyMode";
constexpr const char* kAdditionalVideoSourceRtpJitterLatencyKey = "rtpJitterLatencyMs";
constexpr const char* kAdditionalVideoSourceRtspAutoReconnectKey = "rtspAutoReconnect";

} // namespace

#ifdef QGC_GST_STREAMING
#include "GStreamer.h"
static constexpr bool kGstEnabled = true;
#else
static constexpr bool kGstEnabled = false;
#endif
#include "UVCReceiver.h"

DECLARE_SETTINGGROUP(Video, "Video")
{
    // Setup enum values for videoSource settings into meta data
    QVariantList videoSourceList;
    videoSourceList.append(videoSourceRTSP);
    videoSourceList.append(videoSourceUDPH264);
    videoSourceList.append(videoSourceUDPH265);
    videoSourceList.append(videoSourceTCP);
    videoSourceList.append(videoSourceMPEGTS);
    videoSourceList.append(videoSource3DRSolo);
    videoSourceList.append(videoSourceParrotDiscovery);
    videoSourceList.append(videoSourceYuneecMantisG);

#ifdef QGC_HERELINK_AIRUNIT_VIDEO
    videoSourceList.append(videoSourceHerelinkAirUnit);
#else
    videoSourceList.append(videoSourceHerelinkHotspot);
#endif
    QStringList uvcDevices = UVCReceiver::getDeviceNameList();
    for (const QString& device : uvcDevices) {
        videoSourceList.append(device);
    }
    if (videoSourceList.count() == 0) {
        _noVideo = true;
        videoSourceList.append(videoSourceNoVideo);
        setUserVisible(false);
    } else {
        videoSourceList.insert(0, videoDisabled);
    }

    // make translated strings
    QStringList videoSourceCookedList;
    for (const QVariant& videoSource: videoSourceList) {
        videoSourceCookedList.append( VideoSettings::tr(videoSource.toString().toStdString().c_str()) );
    }

    _nameToMetaDataMap[videoSourceName]->setEnumInfo(videoSourceCookedList, videoSourceList);

    _setForceVideoDecodeList();

    // Migrate legacy gpuZeroCopyEnabled (pre-rename) into the new force-CPU semantics.
    {
        QSettings settings;
        settings.beginGroup(settingsGroup);
        const bool hasLegacy = settings.contains(QStringLiteral("gpuZeroCopyEnabled"));
        const bool hasNew    = settings.contains(forceCpuVideoPathName);
        if (hasLegacy) {
            if (!hasNew) {
                const bool gpuZeroCopy = settings.value(QStringLiteral("gpuZeroCopyEnabled")).toBool();
                forceCpuVideoPath()->setRawValue(!gpuZeroCopy);
            }
            settings.remove(QStringLiteral("gpuZeroCopyEnabled"));
        }
        settings.endGroup();
    }

    // Set default value for videoSource
    _setDefaults();
    _loadAdditionalVideoSources();
}

AdditionalVideoSourceSettings::AdditionalVideoSourceSettings(int sourceId, QObject* parent)
    : QObject(parent)
    , _sourceId(sourceId)
    , _name(VideoSettings::tr("Video Source %1").arg(sourceId))
    , _videoSource(VideoSettings::videoSourceUDPH264)
    , _uri(QStringLiteral("0.0.0.0:%1").arg(5600 + sourceId))
{
    _load();
}

QString AdditionalVideoSourceSettings::receiverName() const
{
    return QStringLiteral("additionalVideoSource_%1").arg(_sourceId);
}

void AdditionalVideoSourceSettings::setName(const QString& name)
{
    if (name == _name) {
        return;
    }

    _name = name;
    _save();
    emit nameChanged();
    emit dirtyChanged(true);
}

void AdditionalVideoSourceSettings::setVideoSource(const QString& videoSource)
{
    if (videoSource == _videoSource) {
        return;
    }

    _videoSource = videoSource;
    _save();
    emit videoSourceChanged();
    emit dirtyChanged(true);
}

void AdditionalVideoSourceSettings::setUri(const QString& uri)
{
    if (uri == _uri) {
        return;
    }

    _uri = uri;
    _save();
    emit uriChanged();
    emit dirtyChanged(true);
}

void AdditionalVideoSourceSettings::setStreamEnabled(bool streamEnabled)
{
    if (streamEnabled == _streamEnabled) {
        return;
    }

    _streamEnabled = streamEnabled;
    _save();
    emit streamEnabledChanged();
    emit dirtyChanged(true);
}

void AdditionalVideoSourceSettings::setLowLatencyMode(bool lowLatencyMode)
{
    if (lowLatencyMode == _lowLatencyMode) {
        return;
    }

    _lowLatencyMode = lowLatencyMode;
    _save();
    emit lowLatencyModeChanged();
    emit dirtyChanged(true);
}

void AdditionalVideoSourceSettings::setRtpJitterLatencyMs(int rtpJitterLatencyMs)
{
    rtpJitterLatencyMs = std::max(0, rtpJitterLatencyMs);
    if (rtpJitterLatencyMs == _rtpJitterLatencyMs) {
        return;
    }

    _rtpJitterLatencyMs = rtpJitterLatencyMs;
    _save();
    emit rtpJitterLatencyMsChanged();
    emit dirtyChanged(true);
}

void AdditionalVideoSourceSettings::setRtspAutoReconnect(bool rtspAutoReconnect)
{
    if (rtspAutoReconnect == _rtspAutoReconnect) {
        return;
    }

    _rtspAutoReconnect = rtspAutoReconnect;
    _save();
    emit rtspAutoReconnectChanged();
    emit dirtyChanged(true);
}

void AdditionalVideoSourceSettings::_load()
{
    QSettings settings;
    settings.beginGroup(VideoSettings::settingsGroup);
    settings.beginGroup(kAdditionalVideoSourcesGroup);
    settings.beginGroup(QString::number(_sourceId));

    _name = settings.value(kAdditionalVideoSourceNameKey, _name).toString();
    _videoSource = settings.value(kAdditionalVideoSourceTypeKey, _videoSource).toString();
    _uri = settings.value(kAdditionalVideoSourceUriKey, _uri).toString();
    _streamEnabled = settings.value(kAdditionalVideoSourceStreamEnabledKey, _streamEnabled).toBool();
    _lowLatencyMode = settings.value(kAdditionalVideoSourceLowLatencyKey, _lowLatencyMode).toBool();
    _rtpJitterLatencyMs = std::max(0, settings.value(kAdditionalVideoSourceRtpJitterLatencyKey, _rtpJitterLatencyMs).toInt());
    _rtspAutoReconnect = settings.value(kAdditionalVideoSourceRtspAutoReconnectKey, _rtspAutoReconnect).toBool();
}

void AdditionalVideoSourceSettings::_save()
{
    QSettings settings;
    settings.beginGroup(VideoSettings::settingsGroup);
    settings.beginGroup(kAdditionalVideoSourcesGroup);
    settings.beginGroup(QString::number(_sourceId));

    settings.setValue(kAdditionalVideoSourceNameKey, _name);
    settings.setValue(kAdditionalVideoSourceTypeKey, _videoSource);
    settings.setValue(kAdditionalVideoSourceUriKey, _uri);
    settings.setValue(kAdditionalVideoSourceStreamEnabledKey, _streamEnabled);
    settings.setValue(kAdditionalVideoSourceLowLatencyKey, _lowLatencyMode);
    settings.setValue(kAdditionalVideoSourceRtpJitterLatencyKey, _rtpJitterLatencyMs);
    settings.setValue(kAdditionalVideoSourceRtspAutoReconnectKey, _rtspAutoReconnect);
}

void AdditionalVideoSourceSettings::removeSettings()
{
    QSettings settings;
    settings.beginGroup(VideoSettings::settingsGroup);
    settings.beginGroup(kAdditionalVideoSourcesGroup);
    settings.remove(QString::number(_sourceId));
}

QStringList VideoSettings::additionalVideoSourceTypes()
{
    QStringList sourceTypes;
    for (const QVariant& videoSource : videoSource()->enumValues()) {
        const QString source = videoSource.toString();
        if ((source != videoDisabled) && (source != videoSourceNoVideo)) {
            sourceTypes.append(source);
        }
    }

    return sourceTypes;
}

QStringList VideoSettings::additionalVideoSourceTypeNames()
{
    QStringList sourceNames;
    const QStringList sourceTypes = additionalVideoSourceTypes();
    sourceNames.reserve(sourceTypes.count());
    for (const QString& source : sourceTypes) {
        sourceNames.append(tr(source.toStdString().c_str()));
    }

    return sourceNames;
}

bool VideoSettings::additionalVideoSourceIsUvc(const QString& videoSource) const
{
    return UVCReceiver::enabled() && UVCReceiver::deviceExists(videoSource);
}

bool VideoSettings::additionalVideoSourceUsesUri(const QString& videoSource) const
{
    return (videoSource == videoSourceUDPH264) ||
           (videoSource == videoSourceUDPH265) ||
           (videoSource == videoSourceRTSP) ||
           (videoSource == videoSourceTCP) ||
           (videoSource == videoSourceMPEGTS);
}

void VideoSettings::addAdditionalVideoSource()
{
    AdditionalVideoSourceSettings* source = new AdditionalVideoSourceSettings(_nextAdditionalVideoSourceId++, this);
    source->setName(tr("Video Source %1").arg(_additionalVideoSources.count() + 1));
    source->setLowLatencyMode(lowLatencyMode()->rawValue().toBool());
    source->setRtpJitterLatencyMs(static_cast<int>(std::min(rtpJitterLatencyMs()->rawValue().toUInt(), static_cast<uint>(INT_MAX))));
    source->setRtspAutoReconnect(rtspAutoReconnect()->rawValue().toBool());

    _additionalVideoSources.append(source);
    _saveAdditionalVideoSourceIds();
    emit additionalVideoSourcesChanged();
}

void VideoSettings::removeAdditionalVideoSource(int index)
{
    if (index < 0 || index >= _additionalVideoSources.count()) {
        return;
    }

    AdditionalVideoSourceSettings* source = qobject_cast<AdditionalVideoSourceSettings*>(_additionalVideoSources.removeAt(index));
    if (source) {
        source->removeSettings();
        source->deleteLater();
    }

    _saveAdditionalVideoSourceIds();
    emit additionalVideoSourcesChanged();
}

void VideoSettings::_loadAdditionalVideoSources()
{
    QSettings settings;
    settings.beginGroup(settingsGroup);
    settings.beginGroup(kAdditionalVideoSourcesGroup);

    const QStringList sourceIds = settings.value(kAdditionalVideoSourceIdsKey).toStringList();
    for (const QString& sourceIdString : sourceIds) {
        bool ok = false;
        const int sourceId = sourceIdString.toInt(&ok);
        if (!ok || sourceId < 1) {
            continue;
        }

        _nextAdditionalVideoSourceId = std::max(_nextAdditionalVideoSourceId, sourceId + 1);
        _additionalVideoSources.append(new AdditionalVideoSourceSettings(sourceId, this));
    }
}

void VideoSettings::_saveAdditionalVideoSourceIds() const
{
    QStringList sourceIds;
    sourceIds.reserve(_additionalVideoSources.count());
    for (int i = 0; i < _additionalVideoSources.count(); ++i) {
        const AdditionalVideoSourceSettings* source = qobject_cast<const AdditionalVideoSourceSettings*>(_additionalVideoSources[i]);
        if (source) {
            sourceIds.append(QString::number(source->sourceId()));
        }
    }

    QSettings settings;
    settings.beginGroup(settingsGroup);
    settings.beginGroup(kAdditionalVideoSourcesGroup);
    settings.setValue(kAdditionalVideoSourceIdsKey, sourceIds);
}

void VideoSettings::_setDefaults()
{
    if (_noVideo) {
        _nameToMetaDataMap[videoSourceName]->setRawDefaultValue(videoSourceNoVideo);
    } else {
        _nameToMetaDataMap[videoSourceName]->setRawDefaultValue(videoDisabled);
    }
}

DECLARE_SETTINGSFACT(VideoSettings, aspectRatio)
DECLARE_SETTINGSFACT(VideoSettings, videoFit)
DECLARE_SETTINGSFACT(VideoSettings, gridLines)
DECLARE_SETTINGSFACT(VideoSettings, showRecControl)
DECLARE_SETTINGSFACT(VideoSettings, recordingFormat)
DECLARE_SETTINGSFACT(VideoSettings, maxVideoSize)
DECLARE_SETTINGSFACT(VideoSettings, enableStorageLimit)
DECLARE_SETTINGSFACT(VideoSettings, streamEnabled)
DECLARE_SETTINGSFACT(VideoSettings, disableWhenDisarmed)

DECLARE_SETTINGSFACT_NO_FUNC(VideoSettings, videoSource)
{
    if (!_videoSourceFact) {
        _videoSourceFact = _createSettingsFact(videoSourceName);
        //-- Check for sources no longer available
        if(!_videoSourceFact->enumValues().contains(_videoSourceFact->rawValue().toString())) {
            if (_noVideo) {
                _videoSourceFact->setRawValue(videoSourceNoVideo);
            } else {
                _videoSourceFact->setRawValue(videoDisabled);
            }
        }
        connect(_videoSourceFact, &Fact::valueChanged, this, &VideoSettings::_configChanged);
    }
    return _videoSourceFact;
}

DECLARE_SETTINGSFACT_NO_FUNC(VideoSettings, forceVideoDecoder)
{
    if (!_forceVideoDecoderFact) {
        _forceVideoDecoderFact = _createSettingsFact(forceVideoDecoderName);

        _forceVideoDecoderFact->setUserVisible(kGstEnabled);

        connect(_forceVideoDecoderFact, &Fact::valueChanged, this, &VideoSettings::_configChanged);
    }
    return _forceVideoDecoderFact;
}

DECLARE_SETTINGSFACT_NO_FUNC(VideoSettings, lowLatencyMode)
{
    if (!_lowLatencyModeFact) {
        _lowLatencyModeFact = _createSettingsFact(lowLatencyModeName);

        _lowLatencyModeFact->setUserVisible(kGstEnabled);

        connect(_lowLatencyModeFact, &Fact::valueChanged, this, &VideoSettings::_configChanged);
    }
    return _lowLatencyModeFact;
}

DECLARE_SETTINGSFACT_NO_FUNC(VideoSettings, rtpJitterLatencyMs)
{
    if (!_rtpJitterLatencyMsFact) {
        _rtpJitterLatencyMsFact = _createSettingsFact(rtpJitterLatencyMsName);
        _rtpJitterLatencyMsFact->setUserVisible(kGstEnabled);
        connect(_rtpJitterLatencyMsFact, &Fact::valueChanged, this, &VideoSettings::_configChanged);
    }
    return _rtpJitterLatencyMsFact;
}

DECLARE_SETTINGSFACT_NO_FUNC(VideoSettings, rtspAutoReconnect)
{
    if (!_rtspAutoReconnectFact) {
        _rtspAutoReconnectFact = _createSettingsFact(rtspAutoReconnectName);
        _rtspAutoReconnectFact->setUserVisible(kGstEnabled);
        connect(_rtspAutoReconnectFact, &Fact::valueChanged, this, &VideoSettings::_configChanged);
    }
    return _rtspAutoReconnectFact;
}

DECLARE_SETTINGSFACT_NO_FUNC(VideoSettings, forceCpuVideoPath)
{
    if (!_forceCpuVideoPathFact) {
        _forceCpuVideoPathFact = _createSettingsFact(forceCpuVideoPathName);

#if defined(QGC_HAS_ANY_GPU_PATH)
        _forceCpuVideoPathFact->setUserVisible(kGstEnabled);
#else
        _forceCpuVideoPathFact->setUserVisible(false);
#endif
    }
    return _forceCpuVideoPathFact;
}

// videoConversionElement / disablePixelAspectRatio are read by VideoBackend::createSink()
// into a VideoSinkConfig and passed as construct-only bin properties — no env-var indirection.
DECLARE_SETTINGSFACT_NO_FUNC(VideoSettings, videoConversionElement)
{
    if (!_videoConversionElementFact) {
        _videoConversionElementFact = _createSettingsFact(videoConversionElementName);
        _videoConversionElementFact->setUserVisible(kGstEnabled);
    }
    return _videoConversionElementFact;
}

DECLARE_SETTINGSFACT_NO_FUNC(VideoSettings, disablePixelAspectRatio)
{
    if (!_disablePixelAspectRatioFact) {
        _disablePixelAspectRatioFact = _createSettingsFact(disablePixelAspectRatioName);
        _disablePixelAspectRatioFact->setUserVisible(kGstEnabled);
    }
    return _disablePixelAspectRatioFact;
}


DECLARE_SETTINGSFACT_NO_FUNC(VideoSettings, rtspTimeout)
{
    if (!_rtspTimeoutFact) {
        _rtspTimeoutFact = _createSettingsFact(rtspTimeoutName);

        _rtspTimeoutFact->setUserVisible(kGstEnabled);

        connect(_rtspTimeoutFact, &Fact::valueChanged, this, &VideoSettings::_configChanged);
    }
    return _rtspTimeoutFact;
}

DECLARE_SETTINGSFACT_NO_FUNC(VideoSettings, udpUrl)
{
    if (!_udpUrlFact) {
        _udpUrlFact = _createSettingsFact(udpUrlName);
        connect(_udpUrlFact, &Fact::valueChanged, this, &VideoSettings::_configChanged);
    }
    return _udpUrlFact;
}

DECLARE_SETTINGSFACT_NO_FUNC(VideoSettings, rtspUrl)
{
    if (!_rtspUrlFact) {
        _rtspUrlFact = _createSettingsFact(rtspUrlName);
        connect(_rtspUrlFact, &Fact::valueChanged, this, &VideoSettings::_configChanged);
    }
    return _rtspUrlFact;
}

DECLARE_SETTINGSFACT_NO_FUNC(VideoSettings, tcpUrl)
{
    if (!_tcpUrlFact) {
        _tcpUrlFact = _createSettingsFact(tcpUrlName);
        connect(_tcpUrlFact, &Fact::valueChanged, this, &VideoSettings::_configChanged);
    }
    return _tcpUrlFact;
}

bool VideoSettings::streamConfigured(void)
{
    //-- First, check if it's autoconfigured
    if(VideoManager::instance()->autoStreamConfigured()) {
        qCDebug(VideoSettingsLog) << "Stream auto configured";
        return true;
    }
    //-- Check if it's disabled
    QString vSource = videoSource()->rawValue().toString();
    if(vSource == videoSourceNoVideo || vSource == videoDisabled) {
        return false;
    }
    //-- If UDP, check for URL
    if(vSource == videoSourceUDPH264 || vSource == videoSourceUDPH265) {
        qCDebug(VideoSettingsLog) << "Testing configuration for UDP Stream:" << udpUrl()->rawValue().toString();
        return !udpUrl()->rawValue().toString().isEmpty();
    }
    //-- If RTSP, check for URL
    if(vSource == videoSourceRTSP) {
        qCDebug(VideoSettingsLog) << "Testing configuration for RTSP Stream:" << rtspUrl()->rawValue().toString();
        return !rtspUrl()->rawValue().toString().isEmpty();
    }
    //-- If TCP, check for URL
    if(vSource == videoSourceTCP) {
        qCDebug(VideoSettingsLog) << "Testing configuration for TCP Stream:" << tcpUrl()->rawValue().toString();
        return !tcpUrl()->rawValue().toString().isEmpty();
    }
    //-- If MPEG-TS, check for URL
    if(vSource == videoSourceMPEGTS) {
        qCDebug(VideoSettingsLog) << "Testing configuration for MPEG-TS Stream:" << udpUrl()->rawValue().toString();
        return !udpUrl()->rawValue().toString().isEmpty();
    }
    //-- If Herelink Air unit, good to go
    if(vSource == videoSourceHerelinkAirUnit) {
        qCDebug(VideoSettingsLog) << "Stream configured for Herelink Air Unit";
        return true;
    }
    //-- If Herelink Hotspot, good to go
    if(vSource == videoSourceHerelinkHotspot) {
        qCDebug(VideoSettingsLog) << "Stream configured for Herelink Hotspot";
        return true;
    }
    if (UVCReceiver::enabled() && UVCReceiver::deviceExists(vSource)) {
        qCDebug(VideoSettingsLog) << "Stream configured for UVC";
        return true;
    }
    return false;
}

void VideoSettings::_configChanged(QVariant)
{
    emit streamConfiguredChanged(streamConfigured());
}

void VideoSettings::_setForceVideoDecodeList()
{
#ifdef QGC_GST_STREAMING
    static const QList<GStreamer::VideoDecoderOptions> removeForceVideoDecodeList{
#if defined(Q_OS_ANDROID)
    GStreamer::VideoDecoderOptions::ForceVideoDecoderDirectX3D,
    GStreamer::VideoDecoderOptions::ForceVideoDecoderVideoToolbox,
    GStreamer::VideoDecoderOptions::ForceVideoDecoderVAAPI,
    GStreamer::VideoDecoderOptions::ForceVideoDecoderNVIDIA,
    GStreamer::VideoDecoderOptions::ForceVideoDecoderIntel,
#elif defined(Q_OS_LINUX)
    GStreamer::VideoDecoderOptions::ForceVideoDecoderDirectX3D,
    GStreamer::VideoDecoderOptions::ForceVideoDecoderVideoToolbox,
#elif defined(Q_OS_WIN)
    GStreamer::VideoDecoderOptions::ForceVideoDecoderVideoToolbox,
    GStreamer::VideoDecoderOptions::ForceVideoDecoderVulkan,
#elif defined(Q_OS_MACOS)
    GStreamer::VideoDecoderOptions::ForceVideoDecoderDirectX3D,
    GStreamer::VideoDecoderOptions::ForceVideoDecoderVAAPI,
#elif defined(Q_OS_IOS)
    GStreamer::VideoDecoderOptions::ForceVideoDecoderDirectX3D,
    GStreamer::VideoDecoderOptions::ForceVideoDecoderVAAPI,
    GStreamer::VideoDecoderOptions::ForceVideoDecoderNVIDIA,
    GStreamer::VideoDecoderOptions::ForceVideoDecoderIntel,
#endif
    };

    for (const auto &value : removeForceVideoDecodeList) {
        _nameToMetaDataMap[forceVideoDecoderName]->removeEnumInfo(value);
    }
#endif
}

void VideoSettings::pruneUnavailableDecoders()
{
#ifdef QGC_GST_STREAMING
    static const QList<GStreamer::VideoDecoderOptions> hardwareFamilies{
        GStreamer::VideoDecoderOptions::ForceVideoDecoderNVIDIA,
        GStreamer::VideoDecoderOptions::ForceVideoDecoderVAAPI,
        GStreamer::VideoDecoderOptions::ForceVideoDecoderDirectX3D,
        GStreamer::VideoDecoderOptions::ForceVideoDecoderVideoToolbox,
        GStreamer::VideoDecoderOptions::ForceVideoDecoderIntel,
        GStreamer::VideoDecoderOptions::ForceVideoDecoderVulkan,
    };

    const QList<GStreamer::VideoDecoderOptions> available = GStreamer::availableDecoderFamilies();
    const auto metaIt = _nameToMetaDataMap.constFind(forceVideoDecoderName);
    if (metaIt == _nameToMetaDataMap.constEnd() || !metaIt.value()) {
        return;
    }
    FactMetaData* const metaData = metaIt.value();
    bool pruned = false;
    for (const auto family : hardwareFamilies) {
        // removeEnumInfo() qWarns on an absent value, so skip families not in the enum; values are
        // stored as QVariant(int), so match that representation.
        const QVariant familyValue = static_cast<int>(family);
        if (!available.contains(family) && metaData->enumValues().contains(familyValue)) {
            metaData->removeEnumInfo(familyValue);
            pruned = true;
        }
    }

    Fact* const fact = forceVideoDecoder();
    if (pruned) {
        // Backend init is async — refresh any live FactComboBox bound to this fact.
        emit fact->enumsChanged();
    }
    if (!metaData->enumValues().contains(fact->rawValue())) {
        fact->setRawValue(GStreamer::VideoDecoderOptions::ForceVideoDecoderDefault);
    }
#endif
}
