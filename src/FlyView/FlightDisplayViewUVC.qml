import QtQuick
import QtMultimedia

import QGroundControl

Rectangle {
    id:                 _root
    width:              parent ? parent.width : 0
    height:             parent ? parent.height : 0
    implicitWidth:      videoOutput.implicitWidth
    implicitHeight:     videoOutput.implicitHeight
    color:              Qt.rgba(0,0,0,0.75)
    clip:               true
    anchors.centerIn:   parent
    visible:            cameraActive

    property var _videoManager: QGroundControl.videoManager
    property string cameraDeviceId: _videoManager.uvcVideoSourceID
    property bool cameraActive: _videoManager.isUvc

    MediaDevices {
        id: mediaDevices

        function findCameraDevice(cameraId) {
            var videoInputs = mediaDevices.videoInputs
            for (var i = 0; i < videoInputs.length; i++) {
                if (videoInputs[i].description === cameraId) {
                    return videoInputs[i]
                }
            }
            return mediaDevices.defaultVideoInput
        }
    }

    CaptureSession {
        camera: Camera {
            id:             camera
            cameraDevice:   mediaDevices.findCameraDevice(_root.cameraDeviceId)
            active:         _root.cameraActive
        }
        videoOutput: videoOutput
    }

    VideoOutput {
        id:             videoOutput
        anchors.fill:   parent
        fillMode:       VideoOutput.PreserveAspectCrop
    }
}
