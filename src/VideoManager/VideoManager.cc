#include "VideoManager.h"
#include "AppSettings.h"
#include "MavlinkCameraControlInterface.h"
#include "MultiVehicleManager.h"
#include "AppMessages.h"
#include "QGCApplication.h"
#include "QGCCameraManager.h"
#include "QGCCorePlugin.h"
#include "QGCLoggingCategory.h"
#include "QGCVideoStreamInfo.h"
#include "SettingsManager.h"
#include "SubtitleWriter.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"
#include "VideoReceiver.h"
#include "VideoSettings.h"
#include "UVCReceiver.h"
#include "VideoBackend.h"

#include <algorithm>
#include <climits>

#include <QtConcurrent/QtConcurrent>
#include <QtCore/QApplicationStatic>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFutureWatcher>
#include <QtCore/QRunnable>
#include <QtCore/QTimer>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

QGC_LOGGING_CATEGORY(VideoManagerLog, "Video.VideoManager")

static constexpr const char *kFileExtension[VideoReceiver::FILE_FORMAT_MAX + 1] = {
    "mkv",
    "mov",
    "mp4"
};
static constexpr const char *kPrimaryVideoReceiverName = "videoContent";
static constexpr const char *kThermalVideoReceiverName = "thermalVideo";
static constexpr const char *kAdditionalVideoReceiverPrefix = "additionalVideoContent_";
static constexpr const char *kAdditionalVideoSourceReceiverPrefix = "additionalVideoSource_";

Q_APPLICATION_STATIC(VideoManager, _videoManagerInstance);

VideoManager::VideoManager(QObject *parent)
    : QObject(parent)
    , _subtitleWriter(new SubtitleWriter(this))
    , _videoSettings(SettingsManager::instance()->videoSettings())
{
    qCDebug(VideoManagerLog) << this;

    (void) qRegisterMetaType<VideoReceiver::STATUS>("STATUS");

    if (VideoBackend::needsAsyncInit()) {
        _backendDisabledForTests = VideoBackend::disabledForUnitTests();
        if (_backendDisabledForTests) {
            qCInfo(VideoManagerLog) << "Skipping video backend initialization for unit tests";
        }
    }
}

VideoManager::~VideoManager()
{
    qCDebug(VideoManagerLog) << this;
}

VideoManager *VideoManager::instance()
{
    return _videoManagerInstance();
}

void VideoManager::startVideoBackendInit()
{
    if (!VideoBackend::needsAsyncInit()) return;

    if (_backendDisabledForTests) {
        _initState.store(InitState::BackendReady);
        qCInfo(VideoManagerLog) << "video initialization disabled for unit tests";
        return;
    }

    // CAS-gate NotStarted -> Pending: init() (GUI thread) and waitForVideoBackendReady() (other threads)
    // both enter here; without it both launch VideoBackend::initialize() -> double-init SIGABRT.
    // The mutex holds back waiters until _backendInitFuture is assigned.
    QMutexLocker lock(&_initFutureMutex);
    InitState expected = InitState::NotStarted;
    if (!_initState.compare_exchange_strong(expected, InitState::Pending)) {
        qCWarning(VideoManagerLog) << "video init already started";
        return;
    }

    const VideoBackend::EnvPrepResult envResult = VideoBackend::prepareEnvironment();
    // Snapshot argv + env result here on the GUI thread; QCoreApplication::arguments() is not thread-safe.
    _backendInitFuture = QtConcurrent::run(&VideoBackend::initialize, QCoreApplication::arguments(), envResult);

    _backendInitFuture.then(this, [this](bool success) {
        _onBackendInitComplete(success);
    }).onCanceled(this, [this] {
        _onBackendInitComplete(false);
    });
}

bool VideoManager::waitForVideoBackendReady(std::chrono::milliseconds timeout)
{
    if (!VideoBackend::needsAsyncInit()) return true;

    if (_backendDisabledForTests) {
        return true;
    }

    if (_initState.load() == InitState::NotStarted) {
        startVideoBackendInit();
    }

    switch (_initState.load()) {
    case InitState::Failed:
        return false;
    case InitState::BackendReady:
    case InitState::Running:
        return true;
    case InitState::NotStarted:
    case InitState::Pending:
    case InitState::QmlReady:
        break;
    }

    QFuture<bool> future;
    {
        QMutexLocker lock(&_initFutureMutex);
        future = _backendInitFuture;
    }
    if (!future.isValid()) {
        qCCritical(VideoManagerLog) << "waitForVideoBackendReady: no valid future";
        return false;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QFutureWatcher<bool> watcher;
    (void) connect(&watcher, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::quit);
    (void) connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    watcher.setFuture(future);
    if (!watcher.isFinished()) {
        timer.start(timeout);
        loop.exec();
    }

    if (!watcher.isFinished()) {
        qCCritical(VideoManagerLog) << "Timed out waiting for video init";
        return false;
    }

    const bool success = watcher.result();
    if (_initState.load() == InitState::Pending || _initState.load() == InitState::QmlReady) {
        _onBackendInitComplete(success);
    }
    return _initState.load() != InitState::Failed;
}

void VideoManager::init(QQuickWindow *mainWindow)
{
    if (_initialized) {
        qCDebug(VideoManagerLog) << "Video Manager already initialized";
        return;
    }

    if (!mainWindow) {
        qCCritical(VideoManagerLog) << "Failed To Init Video Manager - mainWindow is NULL";
        return;
    }
    _mainWindow = mainWindow;

    VideoBackend::onMainWindowReady(mainWindow);

    (void) connect(_videoSettings->videoSource(), &Fact::rawValueChanged, this, &VideoManager::_videoSourceChanged);
    (void) connect(_videoSettings->udpUrl(), &Fact::rawValueChanged, this, &VideoManager::_videoSourceChanged);
    (void) connect(_videoSettings->rtspUrl(), &Fact::rawValueChanged, this, &VideoManager::_videoSourceChanged);
    (void) connect(_videoSettings->tcpUrl(), &Fact::rawValueChanged, this, &VideoManager::_videoSourceChanged);
    (void) connect(_videoSettings->aspectRatio(), &Fact::rawValueChanged, this, &VideoManager::aspectRatioChanged);
    (void) connect(_videoSettings->lowLatencyMode(), &Fact::rawValueChanged, this, [this](const QVariant &value) {
        Q_UNUSED(value);
        _restartAllVideos();
    });
    // rtpJitterLatencyMs needs a pipeline restart; route through _videoSourceChanged so _updateSettings
    // pushes the new value to each receiver and restarts exactly once (no double restart).
    (void) connect(_videoSettings->rtpJitterLatencyMs(), &Fact::rawValueChanged, this, [this](const QVariant &value) {
        Q_UNUSED(value);
        _videoSourceChanged();
    });
    // autoReconnect is a live setting — push without restart so an in-flight reconnect
    // can be cancelled mid-backoff.
    (void) connect(_videoSettings->rtspAutoReconnect(), &Fact::rawValueChanged, this, [this](const QVariant &value) {
        const bool enabled = value.toBool();
        for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
            if (!_isAdditionalVideoSourceReceiver(receiver)) {
                receiver->setAutoReconnect(enabled);
            }
        }
    });
    VideoBackend::bindDebugLevelFact(SettingsManager::instance()->appSettings()->gstDebugLevel(), this);
    (void) connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged, this, &VideoManager::_setActiveVehicle);

    (void) connect(this, &VideoManager::autoStreamConfiguredChanged, this, &VideoManager::_videoSourceChanged);

    if (VideoBackend::needsAsyncInit() && _initState.load() == InitState::NotStarted) {
        startVideoBackendInit();
    }

    _mainWindow->scheduleRenderJob(
        QRunnable::create([this] {
            QMetaObject::invokeMethod(this, &VideoManager::_initAfterQmlIsReady, Qt::QueuedConnection);
        }),
        QQuickWindow::AfterSynchronizingStage);

    _initialized = true;
}

void VideoManager::_initAfterQmlIsReady()
{
    if (!_mainWindow) {
        qCCritical(VideoManagerLog) << "_initAfterQmlIsReady called with NULL mainWindow";
        return;
    }

    qCDebug(VideoManagerLog) << "_initAfterQmlIsReady";

    if (VideoBackend::needsAsyncInit()) {
        switch (_initState.load()) {
        case InitState::Pending:
            _initState.store(InitState::QmlReady);
            qCDebug(VideoManagerLog) << "QML ready, waiting for video";
            return;
        case InitState::BackendReady:
            _initState.store(InitState::Running);
            qCDebug(VideoManagerLog) << "QML ready, video already done — creating receivers";
            break;
        case InitState::Failed:
            qCWarning(VideoManagerLog) << "QML ready but video init failed";
            return;
        case InitState::NotStarted:
        case InitState::QmlReady:
        case InitState::Running:
            qCWarning(VideoManagerLog) << "_initAfterQmlIsReady: unexpected state" << static_cast<int>(_initState.load());
            return;
        }
    }
    _createVideoReceivers();
}

void VideoManager::_onBackendInitComplete(bool success)
{
    if (!success) {
        _initState.store(InitState::Failed);
        qCCritical(VideoManagerLog) << "video initialization failed";
        return;
    }

    if (VideoBackend::needsAsyncInit() && _videoSettings) {
        _videoSettings->pruneUnavailableDecoders();
        VideoBackend::applyDecoderPriorities(_videoSettings->forceVideoDecoder()->rawValue().toInt());
    }

    switch (_initState.load()) {
    case InitState::Pending:
        _initState.store(InitState::BackendReady);
        qCDebug(VideoManagerLog) << "video ready, waiting for QML";
        return;
    case InitState::QmlReady:
        _initState.store(InitState::Running);
        qCDebug(VideoManagerLog) << "video ready, QML already done — creating receivers";
        _createVideoReceivers();
        return;
    default:
        qCWarning(VideoManagerLog) << "_onBackendInitComplete: unexpected state" << static_cast<int>(_initState.load());
        return;
    }
}

void VideoManager::_createVideoReceivers()
{
#ifdef QGC_UNITTEST_BUILD
    if (_createVideoReceiversForTest) {
        _createVideoReceiversForTest();
        return;
    }
#endif
    static const QStringList videoStreamList = {
        kPrimaryVideoReceiverName,
        kThermalVideoReceiverName
    };

    QStringList existing;
    existing.reserve(_videoReceivers.size());
    for (const VideoReceiver *receiver : std::as_const(_videoReceivers)) {
        existing.append(receiver->name());
    }

    for (const QString &streamName : videoStreamList) {
        // Skip only names that already initialized; a once-failed receiver was removed from the
        // list, so re-entry retries it instead of being blocked by an all-or-nothing guard.
        if (existing.contains(streamName)) {
            continue;
        }
        VideoReceiver *receiver = QGCCorePlugin::instance()->createVideoReceiver(this);
        if (!receiver) {
            continue;
        }
        receiver->setName(streamName);

        _initVideoReceiver(receiver, _mainWindow);
    }
}

void VideoManager::cleanup()
{
    for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
        QGCCorePlugin::instance()->releaseVideoSink(receiver->sink());
    }
}

void VideoManager::ensureAdditionalVideoReceiver(const QString &receiverName, MavlinkCameraControlInterface *camera)
{
    if (!receiverName.startsWith(QLatin1String(kAdditionalVideoReceiverPrefix)) || !camera) {
        return;
    }

    VideoReceiver *receiver = _findVideoReceiver(receiverName);
    if (!receiver) {
        receiver = QGCCorePlugin::instance()->createVideoReceiver(this);
        if (!receiver) {
            return;
        }
        receiver->setName(receiverName);
        _initVideoReceiver(receiver, _mainWindow);
        receiver = _findVideoReceiver(receiverName);
        if (!receiver) {
            return;
        }
    }

    const QPointer<MavlinkCameraControlInterface> oldCamera = _additionalVideoReceiverCameras.value(receiverName);
    if (oldCamera == camera) {
        _updateAdditionalVideoReceiver(receiver);
        return;
    }

    if (_additionalVideoReceiverConnections.contains(receiverName)) {
        (void) disconnect(_additionalVideoReceiverConnections.take(receiverName));
    }

    _additionalVideoReceiverCameras[receiverName] = camera;
    _additionalVideoReceiverConnections[receiverName] = connect(camera, &MavlinkCameraControlInterface::currentStreamChanged, this, [this, receiverName]() {
        _updateAdditionalVideoReceiver(_findVideoReceiver(receiverName));
    });

    _updateAdditionalVideoReceiver(receiver);
}

void VideoManager::ensureAdditionalVideoSourceReceiver(const QString &receiverName, const QString &videoSource,
                                                       const QString &uri, bool lowLatency, int rtpJitterLatencyMs,
                                                       bool rtspAutoReconnect, bool disableWhenDisarmed,
                                                       bool forceCpuVideoPath, int forceVideoDecoder)
{
    if (!receiverName.startsWith(QLatin1String(kAdditionalVideoSourceReceiverPrefix))) {
        return;
    }

    VideoReceiver *receiver = _findVideoReceiver(receiverName);
    if (!receiver) {
        receiver = QGCCorePlugin::instance()->createVideoReceiver(this);
        if (!receiver) {
            return;
        }
        receiver->setName(receiverName);
        (void) _updateManualVideoSource(receiver, videoSource, uri, lowLatency, rtpJitterLatencyMs,
                                        rtspAutoReconnect, disableWhenDisarmed, forceCpuVideoPath, forceVideoDecoder);
        _initVideoReceiver(receiver, _mainWindow);
        receiver = _findVideoReceiver(receiverName);
        if (!receiver) {
            return;
        }
    }

    const bool recreateSink = (receiver->forceCpuVideoPath() != forceCpuVideoPath) ||
                              (receiver->forceVideoDecoder() != forceVideoDecoder);
    const bool changed = _updateManualVideoSource(receiver, videoSource, uri, lowLatency, rtpJitterLatencyMs,
                                                 rtspAutoReconnect, disableWhenDisarmed, forceCpuVideoPath,
                                                 forceVideoDecoder);
    const bool disabledWhenDisarmed = receiver->disableWhenDisarmed() && _activeVehicle && !_activeVehicle->armed();
    if (!_primaryVideoSourceEnabled() || receiver->uri().isEmpty() || disabledWhenDisarmed) {
        _stopReceiver(receiver);
    } else if (recreateSink && _recreateVideoSink(receiver)) {
        _startReceiver(receiver);
    } else if (changed) {
        _restartVideo(receiver);
    } else if (!receiver->started()) {
        _startReceiver(receiver);
    }
}

void VideoManager::restartAdditionalVideoReceiver(const QString &receiverName)
{
    VideoReceiver *receiver = _findVideoReceiver(receiverName);
    if (_isAdditionalVideoReceiver(receiver)) {
        _restartVideo(receiver);
    }
}

void VideoManager::releaseAdditionalVideoReceiver(const QString &receiverName)
{
    if (!receiverName.startsWith(QLatin1String(kAdditionalVideoReceiverPrefix)) &&
        !receiverName.startsWith(QLatin1String(kAdditionalVideoSourceReceiverPrefix))) {
        return;
    }

    if (_additionalVideoReceiverConnections.contains(receiverName)) {
        (void) disconnect(_additionalVideoReceiverConnections.take(receiverName));
    }
    _additionalVideoReceiverCameras.remove(receiverName);

    VideoReceiver *receiver = _findVideoReceiver(receiverName);
    if (!receiver) {
        return;
    }

    _stopReceiver(receiver);
    if (receiver->sink()) {
        QGCCorePlugin::instance()->releaseVideoSink(receiver->sink());
        receiver->setSink(nullptr);
    }
    _videoReceivers.removeOne(receiver);
    receiver->deleteLater();
}

bool VideoManager::receiverDecoding(const QString &receiverName) const
{
    const VideoReceiver *receiver = _findVideoReceiver(receiverName);
    return receiver ? receiver->decoding() : false;
}

void VideoManager::_cleanupOldVideos()
{
    if (!SettingsManager::instance()->videoSettings()->enableStorageLimit()->rawValue().toBool()) {
        return;
    }

    const QString savePath = SettingsManager::instance()->appSettings()->videoSavePath();
    QDir videoDir = QDir(savePath);
    videoDir.setFilter(QDir::Files | QDir::Readable | QDir::NoSymLinks | QDir::Writable);
    videoDir.setSorting(QDir::Time);

    QStringList nameFilters;
    for (size_t i = 0; i < std::size(kFileExtension); i++) {
        nameFilters << QStringLiteral("*.") + kFileExtension[i];
    }

    videoDir.setNameFilters(nameFilters);
    QFileInfoList vidList = videoDir.entryInfoList();
    if (vidList.isEmpty()) {
        return;
    }

    uint64_t total = 0;
    for (const QFileInfo &video : std::as_const(vidList)) {
        total += video.size();
    }

    const uint64_t maxSize = SettingsManager::instance()->videoSettings()->maxVideoSize()->rawValue().toUInt() * qPow(1024, 2);
    while ((total >= maxSize) && !vidList.isEmpty()) {
        const QFileInfo info = vidList.takeLast();
        total -= info.size();
        const QString path = info.filePath();
        qCDebug(VideoManagerLog) << "Removing old video file:" << path;
        (void) QFile::remove(path);
    }
}

void VideoManager::startRecording(const QString &videoFile)
{
    const VideoReceiver::FILE_FORMAT fileFormat = static_cast<VideoReceiver::FILE_FORMAT>(_videoSettings->recordingFormat()->rawValue().toInt());
    if (!VideoReceiver::isValidFileFormat(fileFormat)) {
        QGC::showAppMessage(tr("Invalid video format defined."));
        return;
    }

    _cleanupOldVideos();

    const QString savePath = SettingsManager::instance()->appSettings()->videoSavePath();
    if (savePath.isEmpty()) {
        QGC::showAppMessage(tr("Unabled to record video. Video save path must be specified in Settings."));
        return;
    }

    const QString videoFileUrl = videoFile.isEmpty() ? QDateTime::currentDateTime().toString("yyyy-MM-dd_hh.mm.ss") : videoFile;
    const QString ext = kFileExtension[fileFormat];

    const QString videoFileNameTemplate = savePath + "/" + videoFileUrl + ".%1" + ext;

    for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
        if (!receiver->started()) {
            qCDebug(VideoManagerLog) << "Video receiver is not ready.";
            continue;
        }
        const QString streamName = _isPrimaryVideoReceiver(receiver) ? "" : (receiver->name() + ".");
        const QString videoFileName = videoFileNameTemplate.arg(streamName);
        receiver->startRecording(videoFileName, fileFormat);
    }
}

void VideoManager::stopRecording()
{
    for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
        receiver->stopRecording();
    }
}

void VideoManager::grabImage(const QString &imageFile)
{
    if (imageFile.isEmpty()) {
        _imageFile = SettingsManager::instance()->appSettings()->photoSavePath();
        _imageFile += QStringLiteral("/") + QDateTime::currentDateTime().toString("yyyy-MM-dd_hh.mm.ss.zzz") + QStringLiteral(".jpg");
    } else {
        _imageFile = imageFile;
    }

    emit imageFileChanged(_imageFile);

    for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
        receiver->takeScreenshot(_imageFile);
        // QSharedPointer<QQuickItemGrabResult> result = receiver->widget()->grabToImage(const QSize &targetSize = QSize())
    }
}

double VideoManager::aspectRatio() const
{
    // Live decoded resolution wins — set by VideoReceiver::videoSizeChanged once frames flow.
    if (!_videoSize.isEmpty()) {
        return static_cast<double>(_videoSize.width()) / _videoSize.height();
    }

    for (VideoReceiver *receiver : _videoReceivers) {
        QGCVideoStreamInfo *pInfo = receiver->videoStreamInfo();
        if (!receiver->isThermal() && pInfo && !pInfo->isThermal()) {
            return pInfo->aspectRatio();
        }
    }

    return _videoSettings->aspectRatio()->rawValue().toDouble();
}

double VideoManager::thermalAspectRatio() const
{
    for (VideoReceiver *receiver : _videoReceivers) {
        QGCVideoStreamInfo *pInfo = receiver->videoStreamInfo();
        if (receiver->isThermal() && pInfo && pInfo->isThermal()) {
            return pInfo->aspectRatio();
        }
    }

    return 1.0;
}

double VideoManager::hfov() const
{
    for (VideoReceiver *receiver : _videoReceivers) {
        QGCVideoStreamInfo *pInfo = receiver->videoStreamInfo();
        if (!receiver->isThermal() && pInfo && !pInfo->isThermal()) {
            return pInfo->hfov();
        }
    }

    return 1.0;
}

double VideoManager::thermalHfov() const
{
    for (VideoReceiver *receiver : _videoReceivers) {
        QGCVideoStreamInfo *pInfo = receiver->videoStreamInfo();
        if (receiver->isThermal() && pInfo && pInfo->isThermal()) {
            return pInfo->hfov();
        }
    }

    return _videoSettings->aspectRatio()->rawValue().toDouble();
}

bool VideoManager::hasThermal() const
{
    for (VideoReceiver *receiver : _videoReceivers) {
        QGCVideoStreamInfo *pInfo = receiver->videoStreamInfo();
        if (receiver->isThermal() && pInfo && pInfo->isThermal()) {
            return true;
        }
    }

    return false;
}

bool VideoManager::hasVideo() const
{
    return (_videoSettings->streamEnabled()->rawValue().toBool() && _videoSettings->streamConfigured());
}

bool VideoManager::isUvc() const
{
    return (!_uvcVideoSourceID.isEmpty() && UVCReceiver::enabled() && hasVideo());
}

void VideoManager::setfullScreen(bool on)
{
    if (on) {
        if (!_activeVehicle || _activeVehicle->vehicleLinkManager()->communicationLost()) {
            on = false;
        }
    }

    if (on != _fullScreen) {
        _fullScreen = on;
        emit fullScreenChanged();
    }
}

bool VideoManager::isStreamSource() const
{
    static const QStringList videoSourceList = {
        VideoSettings::videoSourceUDPH264,
        VideoSettings::videoSourceUDPH265,
        VideoSettings::videoSourceRTSP,
        VideoSettings::videoSourceTCP,
        VideoSettings::videoSourceMPEGTS,
        VideoSettings::videoSource3DRSolo,
        VideoSettings::videoSourceParrotDiscovery,
        VideoSettings::videoSourceYuneecMantisG,
        VideoSettings::videoSourceHerelinkAirUnit,
        VideoSettings::videoSourceHerelinkHotspot,
    };
    const QString videoSource = _videoSettings->videoSource()->rawValue().toString();
    return (videoSourceList.contains(videoSource) || autoStreamConfigured());
}

void VideoManager::_videoSourceChanged()
{
    bool changed = false;
    if (_activeVehicle) {
        QGCCameraManager* camMgr = _activeVehicle->cameraManager();
        for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
            if (_isAdditionalVideoSourceReceiver(receiver)) {
                continue;
            }

            QGCVideoStreamInfo* info = nullptr;
            if (_isAdditionalCameraVideoReceiver(receiver)) {
                MavlinkCameraControlInterface *camera = _additionalVideoReceiverCameras.value(receiver->name());
                info = camera ? camera->currentStreamInstance() : nullptr;
            } else if (receiver->isThermal()) {
                info = camMgr ? camMgr->thermalStreamInstance() : nullptr;
            } else {
                info = camMgr ? camMgr->currentStreamInstance() : nullptr;
            }
            receiver->setVideoStreamInfo(info);
            changed |= _updateSettings(receiver);
        }
    } else {
        for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
            if (_isAdditionalVideoSourceReceiver(receiver)) {
                continue;
            }

            receiver->setVideoStreamInfo(nullptr);
            changed |= _updateSettings(receiver);
        }
    }

    if (changed) {
        emit hasVideoChanged();
        emit isStreamSourceChanged();
        emit isAutoStreamChanged();

        if (!_primaryVideoSourceEnabled()) {
            for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
                _stopReceiver(receiver);
            }
        } else if (hasVideo()) {
            for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
                if (!_isAdditionalVideoSourceReceiver(receiver)) {
                    _restartVideo(receiver);
                }
            }
        } else {
            for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
                _stopReceiver(receiver);
            }
        }

        qCDebug(VideoManagerLog) << "New Video Source:" << _videoSettings->videoSource()->rawValue().toString();
    }
}

bool VideoManager::_updateUVC(VideoReceiver * /*receiver*/)
{
    bool result = false;

    const QString oldUvcVideoSrcID = _uvcVideoSourceID;

    if (!UVCReceiver::enabled() || !hasVideo() || isStreamSource()) {
        _uvcVideoSourceID = QString();
    } else {
        _uvcVideoSourceID = UVCReceiver::getSourceId();
    }

    if (oldUvcVideoSrcID != _uvcVideoSourceID) {
        qCDebug(VideoManagerLog) << "UVC changed from [" << oldUvcVideoSrcID << "] to [" << _uvcVideoSourceID << "]";
        if (!_uvcVideoSourceID.isEmpty()) {
            UVCReceiver::checkPermission();
        }
        result = true;
        emit uvcVideoSourceIDChanged();
        emit isUvcChanged();
    }

    return result;
}

bool VideoManager::autoStreamConfigured() const
{
    for (VideoReceiver *receiver : _videoReceivers) {
        QGCVideoStreamInfo *pInfo = receiver->videoStreamInfo();
        if (_isPrimaryVideoReceiver(receiver) && pInfo && !pInfo->isThermal()) {
            return !pInfo->uri().isEmpty();
        }
    }

    return false;
}

bool VideoManager::_updateAutoStream(VideoReceiver *receiver)
{
    const QGCVideoStreamInfo *pInfo = receiver->videoStreamInfo();
    if (!pInfo) {
        return false;
    }

    qCDebug(VideoManagerLog) << QString("Configure stream (%1):").arg(receiver->name()) << pInfo->uri();

    QString source, url;
    switch (pInfo->type()) {
    case VIDEO_STREAM_TYPE_RTSP:
        source = VideoSettings::videoSourceRTSP;
        url = pInfo->uri();
        if (source == VideoSettings::videoSourceRTSP) {
            _videoSettings->rtspUrl()->setRawValue(url);
        }
        break;
    case VIDEO_STREAM_TYPE_TCP_MPEG:
        source = VideoSettings::videoSourceTCP;
        url = pInfo->uri();
        break;
    case VIDEO_STREAM_TYPE_RTPUDP:
        if (pInfo->encoding() == VIDEO_STREAM_ENCODING_H265) {
            source = VideoSettings::videoSourceUDPH265;
            url = pInfo->uri().contains("udp265://") ? pInfo->uri() : QStringLiteral("udp265://0.0.0.0:%1").arg(pInfo->uri());
        } else {
            source = VideoSettings::videoSourceUDPH264;
            url = pInfo->uri().contains("udp://") ? pInfo->uri() : QStringLiteral("udp://0.0.0.0:%1").arg(pInfo->uri());
        }
        break;
    case VIDEO_STREAM_TYPE_MPEG_TS:
        source = VideoSettings::videoSourceMPEGTS;
        url = pInfo->uri().contains("mpegts://") ? pInfo->uri() : QStringLiteral("mpegts://0.0.0.0:%1").arg(pInfo->uri());
        break;
    default:
        qCWarning(VideoManagerLog) << "Unknown VIDEO_STREAM_TYPE";
        source = VideoSettings::videoSourceNoVideo;
        url = pInfo->uri();
        break;
    }

    const bool settingsChanged = _updateVideoUri(receiver, url);
    if (settingsChanged && _isPrimaryVideoReceiver(receiver)) {
        _videoSettings->videoSource()->setRawValue(source);
        emit autoStreamConfiguredChanged();
    }

    return settingsChanged;
}

bool VideoManager::_updateVideoUri(VideoReceiver *receiver, const QString &uri)
{
    if (!receiver) {
        qCDebug(VideoManagerLog) << "VideoReceiver is NULL";
        return false;
    }

    if ((uri == receiver->uri()) && !receiver->uri().isNull()) {
        return false;
    }

    qCDebug(VideoManagerLog) << "New Video URI" << uri;

    receiver->setUri(uri);

    return true;
}

bool VideoManager::_updateSettings(VideoReceiver *receiver)
{
    if (!receiver) {
        qCDebug(VideoManagerLog) << "VideoReceiver is NULL";
        return false;
    }

    if (_isAdditionalVideoSourceReceiver(receiver)) {
        return false;
    }

    bool settingsChanged = false;

    receiver->setDisableWhenDisarmed(_videoSettings->disableWhenDisarmed()->rawValue().toBool());
    receiver->setForceCpuVideoPath(_videoSettings->forceCpuVideoPath()->rawValue().toBool());
    receiver->setForceVideoDecoder(_videoSettings->forceVideoDecoder()->rawValue().toInt());

    const bool lowLatency = _videoSettings->lowLatencyMode()->rawValue().toBool();
    if (lowLatency != receiver->lowLatency()) {
        receiver->setLowLatency(lowLatency);
        settingsChanged = true;
    }

    const int rtpLatencyMs = static_cast<int>(std::min(_videoSettings->rtpJitterLatencyMs()->rawValue().toUInt(), static_cast<uint>(INT_MAX)));
    if (rtpLatencyMs != receiver->rtpJitterLatencyMs()) {
        receiver->setRtpJitterLatencyMs(rtpLatencyMs);
        settingsChanged = true;
    }

    const bool autoReconnect = _videoSettings->rtspAutoReconnect()->rawValue().toBool();
    if (autoReconnect != receiver->autoReconnect()) {
        receiver->setAutoReconnect(autoReconnect);
        // No settingsChanged: autoReconnect is live, doesn't require pipeline restart.
    }

    if (receiver->isThermal()) {
        return settingsChanged;
    }

    if (_isAdditionalCameraVideoReceiver(receiver)) {
        settingsChanged |= _updateAutoStream(receiver);
        if (!receiver->videoStreamInfo()) {
            settingsChanged |= _updateVideoUri(receiver, QString());
        }
        return settingsChanged;
    }

    settingsChanged |= _updateUVC(receiver);
    settingsChanged |= _updateAutoStream(receiver);

    const QString source = _videoSettings->videoSource()->rawValue().toString();
    if (source == VideoSettings::videoSourceUDPH264) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("udp://%1").arg(_videoSettings->udpUrl()->rawValue().toString()));
    } else if (source == VideoSettings::videoSourceUDPH265) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("udp265://%1").arg(_videoSettings->udpUrl()->rawValue().toString()));
    } else if (source == VideoSettings::videoSourceMPEGTS) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("mpegts://%1").arg(_videoSettings->udpUrl()->rawValue().toString()));
    } else if (source == VideoSettings::videoSourceRTSP) {
        settingsChanged |= _updateVideoUri(receiver, _videoSettings->rtspUrl()->rawValue().toString());
    } else if (source == VideoSettings::videoSourceTCP) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("tcp://%1").arg(_videoSettings->tcpUrl()->rawValue().toString()));
    } else if (source == VideoSettings::videoSource3DRSolo) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("udp://0.0.0.0:5600"));
    } else if (source == VideoSettings::videoSourceParrotDiscovery) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("udp://0.0.0.0:8888"));
    } else if (source == VideoSettings::videoSourceYuneecMantisG) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("rtsp://192.168.42.1:554/live"));
    } else if (source == VideoSettings::videoSourceHerelinkAirUnit) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("rtsp://192.168.0.10:8554/H264Video"));
    } else if (source == VideoSettings::videoSourceHerelinkHotspot) {
        settingsChanged |= _updateVideoUri(receiver, QStringLiteral("rtsp://192.168.43.1:8554/fpv_stream"));
    } else if ((source == VideoSettings::videoDisabled) || (source == VideoSettings::videoSourceNoVideo)) {
        settingsChanged |= _updateVideoUri(receiver, QString());
    } else {
        settingsChanged |= _updateVideoUri(receiver, QString());
        if (!isUvc()) {
            qCCritical(VideoManagerLog) << "Video source URI \"" << source << "\" is not supported. Please add support!";
        }
    }

    return settingsChanged;
}

bool VideoManager::_updateManualVideoSource(VideoReceiver *receiver, const QString &videoSource, const QString &uri,
                                            bool lowLatency, int rtpJitterLatencyMs, bool rtspAutoReconnect,
                                            bool disableWhenDisarmed, bool forceCpuVideoPath, int forceVideoDecoder)
{
    if (!_isAdditionalVideoSourceReceiver(receiver)) {
        return false;
    }

    bool settingsChanged = false;

    if (lowLatency != receiver->lowLatency()) {
        receiver->setLowLatency(lowLatency);
        settingsChanged = true;
    }

    rtpJitterLatencyMs = std::max(0, rtpJitterLatencyMs);
    if (rtpJitterLatencyMs != receiver->rtpJitterLatencyMs()) {
        receiver->setRtpJitterLatencyMs(rtpJitterLatencyMs);
        settingsChanged = true;
    }

    if (rtspAutoReconnect != receiver->autoReconnect()) {
        receiver->setAutoReconnect(rtspAutoReconnect);
    }

    if (disableWhenDisarmed != receiver->disableWhenDisarmed()) {
        receiver->setDisableWhenDisarmed(disableWhenDisarmed);
    }

    if (forceCpuVideoPath != receiver->forceCpuVideoPath()) {
        receiver->setForceCpuVideoPath(forceCpuVideoPath);
        settingsChanged = true;
    }

    if (forceVideoDecoder != receiver->forceVideoDecoder()) {
        receiver->setForceVideoDecoder(forceVideoDecoder);
        settingsChanged = true;
    }

    QString receiverUri;
    if (_primaryVideoSourceEnabled()) {
        if ((videoSource == VideoSettings::videoSourceUDPH264) && !uri.isEmpty()) {
            receiverUri = uri.contains(QStringLiteral("://")) ? uri : QStringLiteral("udp://%1").arg(uri);
        } else if ((videoSource == VideoSettings::videoSourceUDPH265) && !uri.isEmpty()) {
            receiverUri = uri.contains(QStringLiteral("://")) ? uri : QStringLiteral("udp265://%1").arg(uri);
        } else if ((videoSource == VideoSettings::videoSourceMPEGTS) && !uri.isEmpty()) {
            receiverUri = uri.contains(QStringLiteral("://")) ? uri : QStringLiteral("mpegts://%1").arg(uri);
        } else if ((videoSource == VideoSettings::videoSourceRTSP) && !uri.isEmpty()) {
            receiverUri = uri;
        } else if ((videoSource == VideoSettings::videoSourceTCP) && !uri.isEmpty()) {
            receiverUri = uri.contains(QStringLiteral("://")) ? uri : QStringLiteral("tcp://%1").arg(uri);
        } else if (videoSource == VideoSettings::videoSource3DRSolo) {
            receiverUri = QStringLiteral("udp://0.0.0.0:5600");
        } else if (videoSource == VideoSettings::videoSourceParrotDiscovery) {
            receiverUri = QStringLiteral("udp://0.0.0.0:8888");
        } else if (videoSource == VideoSettings::videoSourceYuneecMantisG) {
            receiverUri = QStringLiteral("rtsp://192.168.42.1:554/live");
        } else if (videoSource == VideoSettings::videoSourceHerelinkAirUnit) {
            receiverUri = QStringLiteral("rtsp://192.168.0.10:8554/H264Video");
        } else if (videoSource == VideoSettings::videoSourceHerelinkHotspot) {
            receiverUri = QStringLiteral("rtsp://192.168.43.1:8554/fpv_stream");
        } else if (UVCReceiver::enabled() && UVCReceiver::deviceExists(videoSource)) {
            receiverUri = QString();
        } else {
            qCWarning(VideoManagerLog) << "Additional video source is not supported:" << videoSource;
        }
    }

    settingsChanged |= _updateVideoUri(receiver, receiverUri);

    return settingsChanged;
}

void VideoManager::_setActiveVehicle(Vehicle *vehicle)
{
    qCDebug(VideoManagerLog) << Q_FUNC_INFO << "new vehicle" << vehicle << "old active vehicle" << _activeVehicle;

    if (_activeVehicle) {
        (void) disconnect(_activeVehicle->vehicleLinkManager(), &VehicleLinkManager::communicationLostChanged, this, &VideoManager::_communicationLostChanged);
        auto cameraManager = _activeVehicle->cameraManager();
        if (cameraManager) {
            MavlinkCameraControlInterface *pCamera = cameraManager->currentCameraInstance();
            if (pCamera) {
                pCamera->stopStream();
            }
            (void) disconnect(cameraManager, &QGCCameraManager::streamChanged, this, &VideoManager::_videoSourceChanged);
        }

        for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
            // disconnect(receiver->videoStreamInfo(), &QGCVideoStreamInfo::infoChanged, ))
            if (!_isAdditionalVideoSourceReceiver(receiver)) {
                receiver->setVideoStreamInfo(nullptr);
            }
        }
        const QStringList additionalReceivers = _additionalVideoReceiverCameras.keys();
        for (const QString &receiverName : additionalReceivers) {
            releaseAdditionalVideoReceiver(receiverName);
        }
    }

    _activeVehicle = vehicle;
    if (_activeVehicle) {
        (void) connect(_activeVehicle->vehicleLinkManager(), &VehicleLinkManager::communicationLostChanged, this, &VideoManager::_communicationLostChanged);
        if (_activeVehicle->cameraManager()) {
            (void) connect(_activeVehicle->cameraManager(), &QGCCameraManager::streamChanged, this, &VideoManager::_videoSourceChanged);
            MavlinkCameraControlInterface *pCamera = _activeVehicle->cameraManager()->currentCameraInstance();
            if (pCamera) {
                pCamera->resumeStream();
            }
        }

        for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
            if (_isAdditionalVideoSourceReceiver(receiver)) {
                continue;
            }

            if (_activeVehicle->cameraManager()) {
                if (_isAdditionalCameraVideoReceiver(receiver)) {
                    MavlinkCameraControlInterface *camera = _additionalVideoReceiverCameras.value(receiver->name());
                    receiver->setVideoStreamInfo(camera ? camera->currentStreamInstance() : nullptr);
                } else if (receiver->isThermal()) {
                    receiver->setVideoStreamInfo(_activeVehicle->cameraManager()->thermalStreamInstance());
                } else {
                    receiver->setVideoStreamInfo(_activeVehicle->cameraManager()->currentStreamInstance());
                }
            } else {
                receiver->setVideoStreamInfo(nullptr);
            }
            // connect(receiver->videoStreamInfo(), &QGCVideoStreamInfo::infoChanged, ))
        }
    } else {
        setfullScreen(false);
    }
}

void VideoManager::_communicationLostChanged(bool connectionLost)
{
    if (connectionLost) {
        setfullScreen(false);
    }
}

bool VideoManager::_isPrimaryVideoReceiver(const VideoReceiver *receiver) const
{
    return receiver && (receiver->name() == QLatin1String(kPrimaryVideoReceiverName));
}

bool VideoManager::_isAdditionalCameraVideoReceiver(const VideoReceiver *receiver) const
{
    return receiver && receiver->name().startsWith(QLatin1String(kAdditionalVideoReceiverPrefix));
}

bool VideoManager::_isAdditionalVideoSourceReceiver(const VideoReceiver *receiver) const
{
    return receiver && receiver->name().startsWith(QLatin1String(kAdditionalVideoSourceReceiverPrefix));
}

bool VideoManager::_isAdditionalVideoReceiver(const VideoReceiver *receiver) const
{
    return _isAdditionalCameraVideoReceiver(receiver) || _isAdditionalVideoSourceReceiver(receiver);
}

bool VideoManager::_primaryVideoSourceEnabled() const
{
    if (!_videoSettings) {
        return false;
    }

    const QString videoSource = _videoSettings->videoSource()->rawValue().toString();
    return (videoSource != VideoSettings::videoDisabled) && (videoSource != VideoSettings::videoSourceNoVideo);
}

VideoReceiver *VideoManager::_findVideoReceiver(const QString &receiverName) const
{
    const auto receiverIt = std::find_if(_videoReceivers.cbegin(), _videoReceivers.cend(), [&receiverName](const VideoReceiver *receiver) {
        return receiver && (receiver->name() == receiverName);
    });
    return (receiverIt == _videoReceivers.cend()) ? nullptr : *receiverIt;
}

void VideoManager::_updateAdditionalVideoReceiver(VideoReceiver *receiver)
{
    if (!_isAdditionalCameraVideoReceiver(receiver)) {
        return;
    }

    MavlinkCameraControlInterface *camera = _additionalVideoReceiverCameras.value(receiver->name());
    receiver->setVideoStreamInfo(camera ? camera->currentStreamInstance() : nullptr);

    const bool changed = _updateSettings(receiver);
    if (changed && _primaryVideoSourceEnabled() && !receiver->uri().isEmpty()) {
        _restartVideo(receiver);
    } else if (!_primaryVideoSourceEnabled() || receiver->uri().isEmpty()) {
        _stopReceiver(receiver);
    }
}

void VideoManager::_restartAllVideos()
{
    for (VideoReceiver *videoReceiver : std::as_const(_videoReceivers)) {
        _restartVideo(videoReceiver);
    }
}

void VideoManager::_restartVideo(VideoReceiver *receiver)
{
    if (!receiver) {
        qCDebug(VideoManagerLog) << "VideoReceiver is NULL";
        return;
    }

    qCDebug(VideoManagerLog) << "Restart video receiver" << receiver->name();

    if (receiver->started()) {
        _stopReceiver(receiver);
        // onStopComplete Signal Will Restart It
    } else {
        _startReceiver(receiver);
    }
}

bool VideoManager::_recreateVideoSink(VideoReceiver *receiver)
{
    if (!receiver || !receiver->widget()) {
        return false;
    }

    _stopReceiver(receiver);
    if (receiver->sink()) {
        QGCCorePlugin::instance()->releaseVideoSink(receiver->sink());
        receiver->setSink(nullptr);
    }

    void *sink = QGCCorePlugin::instance()->createVideoSink(receiver->widget(), receiver);
    if (!sink) {
        qCCritical(VideoManagerLog) << "createVideoSink() failed" << receiver->name();
        return false;
    }

    receiver->setSink(sink);
    VideoBackend::attachSink(receiver, sink, receiver->widget());
    return true;
}

void VideoManager::_stopReceiver(VideoReceiver *receiver)
{
    if (!receiver) {
        qCDebug(VideoManagerLog) << "VideoReceiver is NULL";
        return;
    }

    if (receiver->started()) {
        receiver->stop();
    }
}

void VideoManager::stopVideo()
{
    for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
        _stopReceiver(receiver);
    }
}

void VideoManager::stopVideoWhenDisarmed()
{
    if (_videoSettings->disableWhenDisarmed()->rawValue().toBool()) {
        _videoSettings->streamEnabled()->setRawValue(false);
    }

    for (VideoReceiver *receiver : std::as_const(_videoReceivers)) {
        if (receiver->disableWhenDisarmed()) {
            _stopReceiver(receiver);
        }
    }
}

void VideoManager::_startReceiver(VideoReceiver *receiver)
{
    if (!receiver) {
        qCDebug(VideoManagerLog) << "VideoReceiver is NULL";
        return;
    }

    if (receiver->started()) {
        qCDebug(VideoManagerLog) << "VideoReceiver is already started" << receiver->name();
        return;
    }

    if (receiver->uri().isEmpty()) {
        qCDebug(VideoManagerLog) << "VideoUri is NULL" << receiver->name();
        return;
    }

    if (receiver->disableWhenDisarmed() && _activeVehicle && !_activeVehicle->armed()) {
        qCDebug(VideoManagerLog) << "Video receiver disabled while disarmed" << receiver->name();
        return;
    }

    if (VideoBackend::needsAsyncInit()) {
        VideoBackend::applyDecoderPriorities(receiver->forceVideoDecoder());
    }

    const bool rtspReceiver = receiver->uri().startsWith(QLatin1String("rtsp://"), Qt::CaseInsensitive) ||
                              receiver->uri().startsWith(QLatin1String("rtsps://"), Qt::CaseInsensitive);
    const QString source = _videoSettings->videoSource()->rawValue().toString();
    const uint32_t timeout = ((rtspReceiver || (source == VideoSettings::videoSourceRTSP)) ? _videoSettings->rtspTimeout()->rawValue().toUInt() : 3);

    receiver->start(timeout);
}

void VideoManager::_initVideoReceiver(VideoReceiver *receiver, QQuickWindow *window)
{
    if (!window) {
        qCCritical(VideoManagerLog) << "Failed To Init Video Receiver - window is NULL";
        receiver->deleteLater();
        return;
    }

    if (_videoReceivers.contains(receiver)) {
        qCWarning(VideoManagerLog) << "Receiver already initialized";
        return;
    }

    _videoReceivers.append(receiver);
    (void) _updateSettings(receiver);

    QQuickItem *widget = window->findChild<QQuickItem*>(receiver->name());
    if (!widget) {
        qCCritical(VideoManagerLog) << "stream widget not found" << receiver->name();
        _videoReceivers.removeOne(receiver);
        receiver->deleteLater();
        return;
    }
    receiver->setWidget(widget);

    void *sink = QGCCorePlugin::instance()->createVideoSink(receiver->widget(), receiver);
    if (!sink) {
        qCCritical(VideoManagerLog) << "createVideoSink() failed" << receiver->name();
        _videoReceivers.removeOne(receiver);
        receiver->deleteLater();
        return;
    }
    receiver->setSink(sink);

    VideoBackend::attachSink(receiver, sink, widget);

    (void) connect(receiver, &VideoReceiver::onStartComplete, this, [this, receiver](VideoReceiver::STATUS status) {
        qCDebug(VideoManagerLog) << "Video" << receiver->name() << "Start complete, status:" << status;
        switch (status) {
        case VideoReceiver::STATUS_OK:
            receiver->setStarted(true);
            if (receiver->sink()) {
                receiver->startDecoding(receiver->sink());
            }
            break;
        case VideoReceiver::STATUS_INVALID_URL:
        case VideoReceiver::STATUS_INVALID_STATE:
            break;
        default:
            // Rate limit restarts on start failure.
            QTimer::singleShot(1000, receiver, [this, receiver]() {
                _restartVideo(receiver);
            });
            break;
        }
    });

    (void) connect(receiver, &VideoReceiver::onStopComplete, this, [this, receiver](VideoReceiver::STATUS status) {
        qCDebug(VideoManagerLog) << "Stop complete" << receiver->name() << receiver->uri()  << ", status:" << status;
        receiver->setStarted(false);
        if (status == VideoReceiver::STATUS_INVALID_URL) {
            qCDebug(VideoManagerLog) << "Invalid video URL. Not restarting";
        } else {
            QTimer::singleShot(1000, receiver, [this, receiver]() {
                qCDebug(VideoManagerLog) << "Restarting video receiver" << receiver->name() << receiver->uri();
                _startReceiver(receiver);
            });
        }
    });

    (void) connect(receiver, &VideoReceiver::streamingChanged, this, [this, receiver](bool active) {
        qCDebug(VideoManagerLog) << "Video" << receiver->name() << "streaming changed, active:" << (active ? "yes" : "no");
        if (_isPrimaryVideoReceiver(receiver)) {
            _streaming = active;
            emit streamingChanged();
        }
    });

    (void) connect(receiver, &VideoReceiver::decodingChanged, this, [this, receiver](bool active) {
        qCDebug(VideoManagerLog) << "Video" << receiver->name() << "decoding changed, active:" << (active ? "yes" : "no");
        if (_isPrimaryVideoReceiver(receiver)) {
            _decoding = active;
            emit decodingChanged();
        } else if (_isAdditionalVideoReceiver(receiver)) {
            emit receiverDecodingChanged(receiver->name());
        }
    });

    (void) connect(receiver, &VideoReceiver::recordingChanged, this, [this, receiver](bool active) {
        qCDebug(VideoManagerLog) << "Video" << receiver->name() << "recording changed, active:" << (active ? "yes" : "no");
        if (_isPrimaryVideoReceiver(receiver)) {
            _recording = active;
            if (!active) {
                _subtitleWriter->stopCapturingTelemetry();
            }
            emit recordingChanged(_recording);
        }
    });

    (void) connect(receiver, &VideoReceiver::recordingStarted, this, [this, receiver](const QString &filename) {
        qCDebug(VideoManagerLog) << "Video" << receiver->name() << "recording started";
        if (_isPrimaryVideoReceiver(receiver)) {
            _subtitleWriter->startCapturingTelemetry(filename, videoSize());
        }
    });

    (void) connect(receiver, &VideoReceiver::videoSizeChanged, this, [this, receiver](QSize size) {
        qCDebug(VideoManagerLog) << "Video" << receiver->name() << "resized. New resolution:" << size.width() << "x" << size.height();
        if (_isPrimaryVideoReceiver(receiver)) {
            _videoSize = size;
            emit videoSizeChanged();
            emit aspectRatioChanged();
        }
    });

    (void) connect(receiver, &VideoReceiver::onTakeScreenshotComplete, this, [receiver](VideoReceiver::STATUS status) {
        if (status == VideoReceiver::STATUS_OK) {
            qCDebug(VideoManagerLog) << "Video" << receiver->name() << "screenshot taken";
        } else {
            qCWarning(VideoManagerLog) << "Video" << receiver->name() << "screenshot failed";
        }
    });

    (void) connect(receiver, &VideoReceiver::videoStreamInfoChanged, this, [this, receiver]() {
        const QGCVideoStreamInfo *videoStreamInfo = receiver->videoStreamInfo();
        qCDebug(VideoManagerLog) << "Video" << receiver->name() << "stream info:" << (videoStreamInfo ? "received" : "lost");

        (void) _updateSettings(receiver);
    });

    (void) _updateSettings(receiver);

    const bool shouldStart = _isAdditionalVideoReceiver(receiver) ? _primaryVideoSourceEnabled() : hasVideo();
    if (shouldStart && !receiver->uri().isEmpty()) {
        _startReceiver(receiver);
    }
}

void VideoManager::startVideo()
{
    qCDebug(VideoManagerLog) << "startVideo";

    if (!hasVideo()) {
        qCDebug(VideoManagerLog) << "Stream not enabled/configured";
        return;
    }

    _restartAllVideos();
}
