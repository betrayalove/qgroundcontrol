import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

ColumnLayout {
    id: root

    Layout.fillWidth: true
    spacing: ScreenTools.defaultFontPixelHeight

    readonly property var _videoSettings: QGroundControl.settingsManager.videoSettings
    readonly property bool _primarySourceEnabled: _videoSettings.videoSource.rawValue
                                                  !== _videoSettings.disabledVideoSource
    readonly property real _stringFieldWidth: ScreenTools.defaultFontPixelWidth * 30

    function _sourceIndex(source) {
        return _videoSettings.additionalVideoSourceTypes.indexOf(source.videoSource)
    }

    function _sourceIsStream(source) {
        return source && !_videoSettings.additionalVideoSourceIsUvc(source.videoSource)
    }

    function _connectionVisible(source) {
        return source && _videoSettings.additionalVideoSourceUsesUri(source.videoSource)
    }

    function _decoderIndex(source) {
        return source ? _videoSettings.forceVideoDecoder.enumValues.indexOf(source.forceVideoDecoder) : 0
    }

    function _uriLabel(source) {
        if (source.videoSource === _videoSettings.rtspVideoSource) {
            return qsTr("RTSP URL")
        }
        if (source.videoSource === _videoSettings.tcpVideoSource) {
            return qsTr("TCP URL")
        }
        return qsTr("UDP URL")
    }

    Repeater {
        model: root._videoSettings.additionalVideoSources

        delegate: ColumnLayout {
            id: channel

            required property int index
            required property var object

            readonly property var source: object

            Layout.fillWidth: true
            spacing: ScreenTools.defaultFontPixelHeight
            visible: root._primarySourceEnabled

            RowLayout {
                Layout.fillWidth: true

                QGCButton {
                    text: qsTr("Remove This Video Source")
                    onClicked: root._videoSettings.removeAdditionalVideoSource(channel.index)
                }
            }

            SettingsGroupLayout {
                Layout.fillWidth: true
                heading: qsTr("Video Source")

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelHeight / 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: ScreenTools.defaultFontPixelWidth * 2

                        QGCLabel {
                            Layout.fillWidth: true
                            Layout.minimumWidth: implicitWidth
                            text: qsTr("Source")
                        }

                        QGCComboBox {
                            Layout.preferredWidth: root._stringFieldWidth
                            model: root._videoSettings.additionalVideoSourceTypeNames
                            currentIndex: channel.source ? Math.max(0, root._sourceIndex(channel.source)) : 0
                            sizeToContents: true
                            onActivated: (index) => {
                                if (channel.source) {
                                    channel.source.videoSource = root._videoSettings.additionalVideoSourceTypes[index]
                                }
                            }
                        }
                    }

                    QGCLabel {
                        Layout.fillWidth: true
                        text: root._videoSettings.videoSource.shortDescription
                        visible: text !== ""
                        font.pointSize: ScreenTools.smallFontPointSize
                        wrapMode: Text.WordWrap
                    }
                }
            }

            SettingsGroupLayout {
                Layout.fillWidth: true
                heading: qsTr("Connection")
                visible: root._connectionVisible(channel.source)

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelHeight / 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: ScreenTools.defaultFontPixelWidth * 2

                        QGCLabel {
                            Layout.fillWidth: true
                            Layout.minimumWidth: implicitWidth
                            text: channel.source ? root._uriLabel(channel.source) : qsTr("URL")
                        }

                        QGCTextField {
                            Layout.preferredWidth: root._stringFieldWidth
                            text: channel.source ? channel.source.uri : ""
                            showUnits: channel.source
                                       && channel.source.videoSource !== root._videoSettings.rtspVideoSource
                            unitsLabel: qsTr("host:port")
                            onEditingFinished: {
                                if (channel.source) {
                                    channel.source.uri = text
                                }
                            }
                        }
                    }

                    QGCLabel {
                        Layout.fillWidth: true
                        text: qsTr("Network address and port for this video stream.")
                        font.pointSize: ScreenTools.smallFontPointSize
                        wrapMode: Text.WordWrap
                    }
                }
            }

            SettingsGroupLayout {
                Layout.fillWidth: true
                heading: qsTr("Settings")
                visible: root._sourceIsStream(channel.source)

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelHeight / 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: ScreenTools.defaultFontPixelWidth * 2

                        QGCLabel {
                            Layout.fillWidth: true
                            Layout.minimumWidth: implicitWidth
                            text: root._videoSettings.aspectRatio.label
                        }

                        QGCTextField {
                            Layout.preferredWidth: root._stringFieldWidth
                            text: channel.source ? channel.source.aspectRatio.toString() : ""
                            numericValuesOnly: true
                            onEditingFinished: {
                                if (channel.source) {
                                    channel.source.aspectRatio = Number(text)
                                }
                            }
                        }
                    }

                    QGCLabel {
                        Layout.fillWidth: true
                        text: root._videoSettings.aspectRatio.shortDescription
                        visible: text !== ""
                        font.pointSize: ScreenTools.smallFontPointSize
                        wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelHeight / 4

                    QGCCheckBoxSlider {
                        Layout.fillWidth: true
                        text: root._videoSettings.disableWhenDisarmed.label
                        checked: channel.source ? channel.source.disableWhenDisarmed : false
                        onClicked: {
                            if (channel.source) {
                                channel.source.disableWhenDisarmed = checked
                            }
                        }
                    }

                    QGCLabel {
                        Layout.fillWidth: true
                        text: root._videoSettings.disableWhenDisarmed.shortDescription
                        visible: text !== ""
                        font.pointSize: ScreenTools.smallFontPointSize
                        wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelHeight / 4
                    visible: root._videoSettings.lowLatencyMode.userVisible

                    QGCCheckBoxSlider {
                        Layout.fillWidth: true
                        text: root._videoSettings.lowLatencyMode.label
                        checked: channel.source ? channel.source.lowLatencyMode : false
                        onClicked: {
                            if (channel.source) {
                                channel.source.lowLatencyMode = checked
                            }
                        }
                    }

                    QGCLabel {
                        Layout.fillWidth: true
                        text: root._videoSettings.lowLatencyMode.shortDescription
                        visible: text !== ""
                        font.pointSize: ScreenTools.smallFontPointSize
                        wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelHeight / 4
                    visible: root._videoSettings.rtpJitterLatencyMs.userVisible
                             && (!channel.source || !channel.source.lowLatencyMode)

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: ScreenTools.defaultFontPixelWidth * 2

                        QGCLabel {
                            Layout.fillWidth: true
                            Layout.minimumWidth: implicitWidth
                            text: root._videoSettings.rtpJitterLatencyMs.label
                        }

                        QGCTextField {
                            Layout.preferredWidth: root._stringFieldWidth
                            text: channel.source ? channel.source.rtpJitterLatencyMs.toString() : ""
                            numericValuesOnly: true
                            showUnits: true
                            unitsLabel: qsTr("ms")
                            onEditingFinished: {
                                if (channel.source) {
                                    channel.source.rtpJitterLatencyMs = Number(text)
                                }
                            }
                        }
                    }

                    QGCLabel {
                        Layout.fillWidth: true
                        text: root._videoSettings.rtpJitterLatencyMs.shortDescription
                        visible: text !== ""
                        font.pointSize: ScreenTools.smallFontPointSize
                        wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelHeight / 4
                    visible: root._videoSettings.rtspAutoReconnect.userVisible

                    QGCCheckBoxSlider {
                        Layout.fillWidth: true
                        text: root._videoSettings.rtspAutoReconnect.label
                        checked: channel.source ? channel.source.rtspAutoReconnect : false
                        onClicked: {
                            if (channel.source) {
                                channel.source.rtspAutoReconnect = checked
                            }
                        }
                    }

                    QGCLabel {
                        Layout.fillWidth: true
                        text: root._videoSettings.rtspAutoReconnect.shortDescription
                        visible: text !== ""
                        font.pointSize: ScreenTools.smallFontPointSize
                        wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelHeight / 4
                    visible: root._videoSettings.forceCpuVideoPath.userVisible

                    QGCCheckBoxSlider {
                        Layout.fillWidth: true
                        text: root._videoSettings.forceCpuVideoPath.label
                        checked: channel.source ? channel.source.forceCpuVideoPath : false
                        onClicked: {
                            if (channel.source) {
                                channel.source.forceCpuVideoPath = checked
                            }
                        }
                    }

                    QGCLabel {
                        Layout.fillWidth: true
                        text: root._videoSettings.forceCpuVideoPath.shortDescription
                        visible: text !== ""
                        font.pointSize: ScreenTools.smallFontPointSize
                        wrapMode: Text.WordWrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: ScreenTools.defaultFontPixelHeight / 4
                    visible: root._videoSettings.forceVideoDecoder.userVisible

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: ScreenTools.defaultFontPixelWidth * 2

                        QGCLabel {
                            Layout.fillWidth: true
                            Layout.minimumWidth: implicitWidth
                            text: root._videoSettings.forceVideoDecoder.label
                        }

                        QGCComboBox {
                            Layout.preferredWidth: root._stringFieldWidth
                            model: root._videoSettings.forceVideoDecoder.enumStrings
                            currentIndex: Math.max(0, root._decoderIndex(channel.source))
                            sizeToContents: true
                            onActivated: (index) => {
                                if (channel.source) {
                                    channel.source.forceVideoDecoder =
                                            root._videoSettings.forceVideoDecoder.enumValues[index]
                                }
                            }
                        }
                    }

                    QGCLabel {
                        Layout.fillWidth: true
                        text: root._videoSettings.forceVideoDecoder.shortDescription
                        visible: text !== ""
                        font.pointSize: ScreenTools.smallFontPointSize
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: root._primarySourceEnabled

        QGCButton {
            text: qsTr("Add Additional Video Source")
            onClicked: root._videoSettings.addAdditionalVideoSource()
        }
    }
}
