import QtQuick
import QtQuick.Window

import QGroundControl
import QGroundControl.Controls

Item {
    id: root

    width: _pipSize
    height: _pipSize * (9 / 16)
    visible: show && !_windowMode
    clip: true

    property string videoObjectName
    property string uvcVideoSourceID
    property bool streamEnabled: true
    property bool streamSource: false
    property bool uvcSource: false
    property bool decoding: false
    property bool show: true
    property Item sizeReferenceItem: parent
    property Item fullReferenceItem: sizeReferenceItem
    property real fullZ: 0
    property real initialPipSize: _sizeReferenceWidth * 0.2
    property bool fullMode: false
    property var swappedPipItem: null

    readonly property string _pipExpandedSettingsKey: videoObjectName === "" ? "AdditionalVideoPIPVisible" : "AdditionalVideoPIPVisible." + videoObjectName
    readonly property real _sizeReferenceWidth: sizeReferenceItem ? sizeReferenceItem.width : (parent ? parent.width : ScreenTools.defaultFontPixelHeight * 60)
    property alias _pipContentItem: pipContent
    property alias _windowContentItem: window.contentItem
    property bool _isExpanded: true
    property real _pipSize: initialPipSize
    property real _maxSize: 0.75
    property real _minSize: 0.10
    property bool _componentComplete: false
    property bool _windowMode: false
    property bool _uvcCameraActive: true

    signal promoteRequested(var window)
    signal restoreRequested(var window)

    Component.onCompleted: {
        _setPipIsExpanded(QGroundControl.loadBoolGlobalSetting(_pipExpandedSettingsKey, true))
        _componentComplete = true
    }

    on_WindowModeChanged: {
        if (_windowMode) {
            _restartMovedVideo()
            Qt.callLater(showWindow)
        } else {
            _restartMovedVideo()
        }
    }

    onFullModeChanged: _restartMovedVideo()
    onShowChanged: {
        if (!show && fullMode) {
            restoreRequested(root)
        }
    }

    function showWindow() {
        window.width = root.width
        window.height = root.height
        window.show()
    }

    function _restartMovedVideo() {
        if (uvcSource) {
            _uvcCameraActive = false
            uvcRestartDelay.restart()
        } else if (videoObjectName !== "") {
            QGroundControl.videoManager.restartAdditionalVideoReceiver(videoObjectName)
        }
    }

    function _setPipIsExpanded(isExpanded) {
        QGroundControl.saveBoolGlobalSetting(_pipExpandedSettingsKey, isExpanded)
        _isExpanded = isExpanded
    }

    states: [
        State {
            name: "pip"
            when: !root._windowMode && !root.fullMode

            AnchorChanges {
                target:         videoSurface
                anchors.top:    pipContent.top
                anchors.bottom: pipContent.bottom
                anchors.left:   pipContent.left
                anchors.right:  pipContent.right
            }

            ParentChange {
                target: videoSurface
                parent: pipContent
            }

            PropertyChanges {
                target: videoSurface
                z:      0
            }
        },
        State {
            name: "full"
            when: root.fullMode && !root._windowMode

            AnchorChanges {
                target:         videoSurface
                anchors.top:    root.fullReferenceItem.top
                anchors.bottom: root.fullReferenceItem.bottom
                anchors.left:   root.fullReferenceItem.left
                anchors.right:  root.fullReferenceItem.right
            }

            ParentChange {
                target: videoSurface
                parent: root.fullReferenceItem
            }

            PropertyChanges {
                target: videoSurface
                z:      root.fullZ
            }
        },
        State {
            name: "window"
            when: root._windowMode

            AnchorChanges {
                target:         videoSurface
                anchors.top:    window.contentItem.top
                anchors.bottom: window.contentItem.bottom
                anchors.left:   window.contentItem.left
                anchors.right:  window.contentItem.right
            }

            ParentChange {
                target: videoSurface
                parent: window.contentItem
            }

            PropertyChanges {
                target: videoSurface
                z:      0
            }
        }
    ]

    Timer {
        id: uvcRestartDelay
        interval: 500
        repeat: false
        onTriggered: root._uvcCameraActive = true
    }

    Window {
        id: window
        visible: false
        onClosing: root._windowMode = false
    }

    Item {
        id: pipContent
        anchors.fill: parent
        visible: root._isExpanded
        clip: true
    }

    Item {
        id: videoSurface

        FlightDisplayViewVideo {
            anchors.fill:       parent
            useSmallFont:       true
            videoObjectName:    root.videoObjectName
            uvcVideoSourceID:   root.uvcVideoSourceID
            streamEnabled:      root.streamEnabled
            streamSource:       root.streamSource
            uvcSource:          root.uvcSource && root._uvcCameraActive
            decoding:           root.decoding
        }
    }

    MouseArea {
        id: pipMouseArea
        anchors.fill: parent
        enabled: root._isExpanded
        preventStealing: true
        hoverEnabled: true
        onClicked: {
            if (root.fullMode && root.swappedPipItem) {
                root.restoreRequested(root)
            } else {
                root.promoteRequested(root)
            }
        }
    }

    MouseArea {
        id: pipResize
        anchors.fill: pipResizeIcon
        preventStealing: true
        cursorShape: Qt.PointingHandCursor

        property real initialX: 0
        property real initialWidth: 0

        onPressed: (mouse) => {
            pipResize.anchors.fill = undefined
            pipResize.initialX = mouse.x
            pipResize.initialWidth = root.width
        }

        onReleased: pipResize.anchors.fill = pipResizeIcon

        onPositionChanged: (mouse) => {
            if (pipResize.pressed && root._sizeReferenceWidth > 0) {
                var parentWidth = root._sizeReferenceWidth
                var newWidth = pipResize.initialWidth + mouse.x - pipResize.initialX
                if (newWidth < parentWidth * root._maxSize && newWidth > parentWidth * root._minSize) {
                    root._pipSize = newWidth
                }
            }
        }
    }

    Image {
        id: pipResizeIcon
        source: "/qmlimages/pipResize.svg"
        fillMode: Image.PreserveAspectFit
        mipmap: true
        anchors.right: parent.right
        anchors.top: parent.top
        visible: root._isExpanded && (ScreenTools.isMobile || pipMouseArea.containsMouse)
        height: ScreenTools.defaultFontPixelHeight * 2.5
        width: ScreenTools.defaultFontPixelHeight * 2.5
        sourceSize.height: height
    }

    Connections {
        target: root.sizeReferenceItem

        function onWidthChanged() {
            if (!root._componentComplete || root._sizeReferenceWidth <= 0) {
                return
            }
            var parentWidth = root._sizeReferenceWidth
            if (root.width > parentWidth * root._maxSize) {
                root._pipSize = parentWidth * root._maxSize
            } else if (root.width < parentWidth * root._minSize) {
                root._pipSize = parentWidth * root._minSize
            }
        }
    }

    Image {
        id: popupPIP
        source: "/qmlimages/PiP.svg"
        mipmap: true
        fillMode: Image.PreserveAspectFit
        anchors.left: parent.left
        anchors.top: parent.top
        visible: root._isExpanded && !root.fullMode && !ScreenTools.isMobile && pipMouseArea.containsMouse
        height: ScreenTools.defaultFontPixelHeight * 2.5
        width: ScreenTools.defaultFontPixelHeight * 2.5
        sourceSize.height: height

        MouseArea {
            anchors.fill: parent
            onClicked: root._windowMode = true
        }
    }

    Image {
        id: hidePIP
        source: "/qmlimages/pipHide.svg"
        mipmap: true
        fillMode: Image.PreserveAspectFit
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        visible: root._isExpanded && (ScreenTools.isMobile || pipMouseArea.containsMouse)
        height: ScreenTools.defaultFontPixelHeight * 2.5
        width: ScreenTools.defaultFontPixelHeight * 2.5
        sourceSize.height: height

        MouseArea {
            anchors.fill: parent
            onClicked: root._setPipIsExpanded(false)
        }
    }

    Rectangle {
        id: showPip
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        height: ScreenTools.defaultFontPixelHeight * 2
        width: ScreenTools.defaultFontPixelHeight * 2
        radius: ScreenTools.defaultFontPixelHeight / 3
        visible: !root._isExpanded
        color: Qt.rgba(0, 0, 0, 0.75)

        Image {
            width: parent.width * 0.75
            height: parent.height * 0.75
            sourceSize.height: height
            source: "/res/buttonRight.svg"
            mipmap: true
            fillMode: Image.PreserveAspectFit
            anchors.centerIn: parent
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root._setPipIsExpanded(true)
        }
    }
}
