import QtQuick
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

RowLayout {
    id: root

    property Item pipView
    property Item fullReferenceItem
    property real fullZ: 0
    property var promoteHandler: null
    property var restoreHandler: null
    property var activeVehicle: QGroundControl.multiVehicleManager.activeVehicle

    readonly property real _margins: ScreenTools.defaultFontPixelWidth / 2
    readonly property real _referenceWidth: parent ? parent.width : ScreenTools.defaultFontPixelHeight * 60
    readonly property var _videoSettings: QGroundControl.settingsManager.videoSettings
    readonly property bool _videoEnabled: _videoSettings.videoSource.rawValue !== _videoSettings.disabledVideoSource
    readonly property var _manualSources: _videoSettings.additionalVideoSources
    readonly property var _cameraManager: activeVehicle ? activeVehicle.cameraManager : null
    readonly property int _cameraCount: _cameraManager ? _cameraManager.cameras.count : 0
    readonly property int _manualSourceCount: _manualSources ? _manualSources.count : 0
    readonly property int _displayItemCount: Math.max(0, _cameraCount - 1) + _manualSourceCount

    width: _referenceWidth
    spacing: _margins
    visible: _videoEnabled && _displayItemCount > 0 && !QGroundControl.videoManager.fullScreen
    z: QGroundControl.zOrderWidgets

    property int receiverRevision: 0

    function _receiverName(camera) {
        return activeVehicle && camera ? "additionalVideoContent_" + activeVehicle.id + "_" + camera.compID : ""
    }

    Repeater {
        model: root._cameraManager ? root._cameraManager.cameras : null

        delegate: FlyViewAdditionalVideoWindow {
            id: cameraDelegate

            required property int index

            readonly property var camera: root._cameraManager ? root._cameraManager.cameras.get(index) : null
            readonly property bool isPrimaryCamera: root._cameraManager && index === root._cameraManager.currentCamera
            readonly property string receiverName: root._receiverName(camera)

            Layout.alignment: Qt.AlignBottom

            show: root._videoEnabled && !isPrimaryCamera
            sizeReferenceItem: root.parent
            fullReferenceItem: root.fullReferenceItem
            fullZ: root.fullZ
            initialPipSize: root.pipView ? root.pipView.width : root._referenceWidth * 0.2
            videoObjectName: receiverName
            streamSource: true
            uvcSource: false
            streamEnabled: root._videoSettings.streamEnabled.rawValue
            decoding: root.receiverRevision >= 0 && QGroundControl.videoManager.receiverDecoding(receiverName)

            function updateReceiver() {
                if (!receiverName) {
                    return
                }
                if (show) {
                    QGroundControl.videoManager.ensureAdditionalVideoReceiver(receiverName, camera)
                } else {
                    QGroundControl.videoManager.releaseAdditionalVideoReceiver(receiverName)
                }
            }

            Component.onCompleted: updateReceiver()
            Component.onDestruction: {
                if (fullMode && root.restoreHandler) {
                    root.restoreHandler()
                }
                if (receiverName) {
                    QGroundControl.videoManager.releaseAdditionalVideoReceiver(receiverName)
                }
            }

            onShowChanged: updateReceiver()
            onPromoteRequested: {
                if (root.promoteHandler) {
                    root.promoteHandler(window)
                }
            }
            onRestoreRequested: {
                if (root.restoreHandler) {
                    root.restoreHandler()
                }
            }
            onReceiverNameChanged: updateReceiver()
            onCameraChanged: updateReceiver()

            Connections {
                target: root._cameraManager
                function onCurrentCameraChanged() { cameraDelegate.updateReceiver() }
                function onStreamChanged() { cameraDelegate.updateReceiver() }
            }
        }
    }

    Repeater {
        model: root._manualSources

        delegate: FlyViewAdditionalVideoWindow {
            id: manualSourceDelegate

            required property var object

            readonly property var source: object
            readonly property bool isUvcSource: source && root._videoSettings.additionalVideoSourceIsUvc(source.videoSource)
            readonly property bool usesUri: source && root._videoSettings.additionalVideoSourceUsesUri(source.videoSource)
            readonly property bool hasConfiguredSource: source && (isUvcSource || !usesUri || source.uri !== "")
            readonly property string receiverName: source ? source.receiverName : ""

            Layout.alignment: Qt.AlignBottom

            show: root._videoEnabled && !!source
            sizeReferenceItem: root.parent
            fullReferenceItem: root.fullReferenceItem
            fullZ: root.fullZ
            initialPipSize: root.pipView ? root.pipView.width : root._referenceWidth * 0.2
            videoObjectName: receiverName
            uvcVideoSourceID: isUvcSource && source ? source.videoSource : ""
            streamSource: !isUvcSource
            uvcSource: root._videoEnabled && isUvcSource && hasConfiguredSource
            streamEnabled: root._videoEnabled
            decoding: isUvcSource ? hasConfiguredSource : (root.receiverRevision >= 0 && QGroundControl.videoManager.receiverDecoding(receiverName))

            function updateReceiver() {
                if (!source || !receiverName) {
                    return
                }

                if (!show || isUvcSource) {
                    QGroundControl.videoManager.releaseAdditionalVideoReceiver(receiverName)
                    return
                }

                QGroundControl.videoManager.ensureAdditionalVideoSourceReceiver(receiverName,
                                                                                source.videoSource,
                                                                                source.uri,
                                                                                source.lowLatencyMode,
                                                                                source.rtpJitterLatencyMs,
                                                                                source.rtspAutoReconnect)
            }

            Component.onCompleted: updateReceiver()
            Component.onDestruction: {
                if (fullMode && root.restoreHandler) {
                    root.restoreHandler()
                }
                if (receiverName) {
                    QGroundControl.videoManager.releaseAdditionalVideoReceiver(receiverName)
                }
            }

            onSourceChanged: updateReceiver()
            onShowChanged: updateReceiver()
            onPromoteRequested: {
                if (root.promoteHandler) {
                    root.promoteHandler(window)
                }
            }
            onRestoreRequested: {
                if (root.restoreHandler) {
                    root.restoreHandler()
                }
            }
            onReceiverNameChanged: updateReceiver()

            Connections {
                target: manualSourceDelegate.source
                function onVideoSourceChanged() { manualSourceDelegate.updateReceiver() }
                function onUriChanged() { manualSourceDelegate.updateReceiver() }
                function onLowLatencyModeChanged() { manualSourceDelegate.updateReceiver() }
                function onRtpJitterLatencyMsChanged() { manualSourceDelegate.updateReceiver() }
                function onRtspAutoReconnectChanged() { manualSourceDelegate.updateReceiver() }
            }
        }
    }

    Connections {
        target: QGroundControl.videoManager
        function onReceiverDecodingChanged(receiverName) {
            if (receiverName.startsWith("additionalVideoContent_") || receiverName.startsWith("additionalVideoSource_")) {
                root.receiverRevision++
            }
        }
    }
}
