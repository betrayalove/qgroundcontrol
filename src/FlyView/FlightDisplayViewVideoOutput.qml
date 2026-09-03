import QtQuick
import QtMultimedia

import QGroundControl

VideoOutput {
    property string videoObjectName: "videoContent"
    property bool grabImages: videoObjectName === "videoContent"

    objectName: videoObjectName

    // Do NOT set `orientation` here — VideoOutput composes orientation on top of the
    // QVideoFrame's own rotation()/mirrored() metadata that qgcqvideosink forwards from
    // GstVideoOrientationMeta. Setting it would double-rotate any stream with orientation tags.

    // videoFit enum: 0=Fit Width, 1=Fit Height, 2=Fill, 3=No Crop. The container
    // handles fit-width/fit-height sizing; only Fill needs the cropping fillMode.
    fillMode: QGroundControl.settingsManager.videoSettings.videoFit.rawValue === 2
              ? VideoOutput.PreserveAspectCrop
              : VideoOutput.PreserveAspectFit

    Connections {
        target: QGroundControl.videoManager
        enabled: grabImages
        function onImageFileChanged(filename) {
            grabToImage(function(result) {
                if (!result.saveToFile(filename)) {
                    console.error('Error capturing video frame');
                }
            });
        }
    }
}
