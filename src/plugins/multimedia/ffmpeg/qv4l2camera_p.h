/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QFFMPEGCAMERA_H
#define QFFMPEGCAMERA_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <private/qplatformcamera_p.h>
#include <private/qplatformmediaintegration_p.h>

#include <qfilesystemwatcher.h>
#include <qsocketnotifier.h>

QT_BEGIN_NAMESPACE

class QV4L2CameraDevices : public QObject
{
    Q_OBJECT
public:
    static QV4L2CameraDevices *instance(QPlatformMediaIntegration *integration);

    QList<QCameraDevice> cameraDevices() const;

public Q_SLOTS:
    void checkCameras();

private:
    void doCheckCameras();
    QV4L2CameraDevices(QPlatformMediaIntegration *integration);

    QList<QCameraDevice> cameras;
    QPlatformMediaIntegration *integration;
    QFileSystemWatcher deviceWatcher;
};

class Q_MULTIMEDIA_EXPORT QV4L2Camera : public QPlatformCamera
{
    Q_OBJECT

public:
    explicit QV4L2Camera(QCamera *parent);
    ~QV4L2Camera();

    bool isActive() const override;
    void setActive(bool active) override;

    void setCamera(const QCameraDevice &camera) override;
    bool setCameraFormat(const QCameraFormat &format) override;

//    void setCaptureSession(QPlatformMediaCaptureSession *) override;

    bool isFocusModeSupported(QCamera::FocusMode mode) const override;
    void setFocusMode(QCamera::FocusMode /*mode*/) override;

//    void setCustomFocusPoint(const QPointF &/*point*/) override;
//    void setFocusDistance(float) override;
//    void zoomTo(float /*newZoomFactor*/, float /*rate*/ = -1.) override;

    void setFlashMode(QCamera::FlashMode /*mode*/) override;
    bool isFlashModeSupported(QCamera::FlashMode mode) const override;
    bool isFlashReady() const override;

//    void setTorchMode(QCamera::TorchMode /*mode*/) override;
//    bool isTorchModeSupported(QCamera::TorchMode mode) const override;

    void setExposureMode(QCamera::ExposureMode) override;
    bool isExposureModeSupported(QCamera::ExposureMode mode) const override;
    void setExposureCompensation(float) override;
    int isoSensitivity() const override;
    void setManualIsoSensitivity(int) override;
    void setManualExposureTime(float) override;
    float exposureTime() const override;

    bool isWhiteBalanceModeSupported(QCamera::WhiteBalanceMode mode) const override;
    void setWhiteBalanceMode(QCamera::WhiteBalanceMode /*mode*/) override;
    void setColorTemperature(int /*temperature*/) override;

    void releaseBuffer(int index);

private Q_SLOTS:
    void readFrame();

private:
    bool m_active;

    QCameraDevice m_cameraDevice;

    void initV4L2Controls();
    int setV4L2ColorTemperature(int temperature);
    bool setV4L2Parameter(quint32 id, qint32 value);
    int getV4L2Parameter(quint32 id) const;

    void setV4L2CameraFormat();
    void initMMap();
    void closeMappedBuffers();
    void startCapturing();
    void stopCapturing();

    struct MappedBuffer {
        void *data;
        qsizetype size;
    };
    QList<MappedBuffer> mappedBuffers;
    QSocketNotifier *notifier = nullptr;

    bool v4l2AutoWhiteBalanceSupported = false;
    bool v4l2ColorTemperatureSupported = false;
    bool v4l2AutoExposureSupported = false;
    bool v4l2ManualExposureSupported = false;
    qint32 v4l2MinColorTemp = 5600; // Daylight...
    qint32 v4l2MaxColorTemp = 5600;
    qint32 v4l2MinExposure = 0;
    qint32 v4l2MaxExposure = 0;
    qint32 v4l2MinExposureAdjustment = 0;
    qint32 v4l2MaxExposureAdjustment = 0;
    int v4l2FileDescriptor = -1;
    timeval firstFrameTime = {-1, -1};
    int bytesPerLine = -1;
    QVideoFrameFormat::YCbCrColorSpace colorSpace = QVideoFrameFormat::YCbCr_Undefined;
    qint64 frameDuration = -1;
};

QT_END_NAMESPACE


#endif  // QFFMPEGCAMERA_H

