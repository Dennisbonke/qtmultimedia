/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
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

#include "qcamerainfo_p.h"

#include "qcamera_p.h"
#include "qmediaserviceprovider_p.h"

#include <qvideodeviceselectorcontrol.h>

QT_BEGIN_NAMESPACE

/*!
    \class QCameraInfo
    \brief The QCameraInfo class provides general information about camera devices.
    \since 5.3
    \inmodule QtMultimedia
    \ingroup multimedia
    \ingroup multimedia_camera

    QCameraInfo lets you query for camera devices that are currently available on the system.

    The static functions defaultCamera() and availableCameras() provide you a list of all
    available cameras.

    This example prints the name of all available cameras:

    \snippet multimedia-snippets/camera.cpp Camera listing

    A QCameraInfo can be used to construct a QCamera. The following example instantiates a QCamera
    whose camera device is named 'mycamera':

    \snippet multimedia-snippets/camera.cpp Camera selection

    You can also use QCameraInfo to get general information about a camera device such as
    description, physical position on the system, or camera sensor orientation.

    \snippet multimedia-snippets/camera.cpp Camera info

    \sa QCamera
*/

QCameraInfo::QCameraInfo() = default;

/*!
    Constructs a camera info object for \a camera.

    You can use it to query information about the \a camera object passed as argument.

    If the \a camera is invalid, for example when no camera device is available on the system,
    the QCameraInfo object will be invalid and isNull() will return true.
*/
QCameraInfo::QCameraInfo(const QCamera &camera)
    : d(new QCameraInfoPrivate)
{
    const QVideoDeviceSelectorControl *deviceControl = camera.d_func()->deviceControl;
    if (deviceControl && deviceControl->deviceCount() > 0) {
        const int selectedDevice = deviceControl->selectedDevice();
        d->id = deviceControl->deviceName(selectedDevice).toLatin1();
        d->description = deviceControl->deviceDescription(selectedDevice);
        d->position = deviceControl->cameraPosition(selectedDevice);
        d->orientation = deviceControl->cameraOrientation(selectedDevice);
        d->isNull = false;
    }
}

/*!
    Constructs a copy of \a other.
*/
QCameraInfo::QCameraInfo(const QCameraInfo &other)
    : d(other.d)
{
}

/*!
    Destroys the QCameraInfo.
*/
QCameraInfo::~QCameraInfo()
{
}

/*!
    Returns true if this QCameraInfo is equal to \a other.
*/
bool QCameraInfo::operator==(const QCameraInfo &other) const
{
    if (d == other.d)
        return true;

    return (d->id == other.d->id
            && d->description == other.d->description
            && d->position == other.d->position
            && d->orientation == other.d->orientation);
}

/*!
    Returns true if this QCameraInfo is null or invalid.
*/
bool QCameraInfo::isNull() const
{
    return d->isNull;
}

/*!
    Returns the device id of the camera

    This is a unique ID to identify the camera and may not be human-readable.
*/
QByteArray QCameraInfo::id() const
{
    return d->id;
}

bool QCameraInfo::isDefault() const
{
    return d->isDefault;
}

/*!
    Returns the human-readable description of the camera.
*/
QString QCameraInfo::description() const
{
    return d->description;
}

/*!
    Returns the physical position of the camera on the hardware system.
*/
QCamera::Position QCameraInfo::position() const
{
    return d->position;
}

/*!
    Returns the physical orientation of the camera sensor.

    The value is the orientation angle (clockwise, in steps of 90 degrees) of the camera sensor
    in relation to the display in its natural orientation.

    You can show the camera image in the correct orientation by rotating it by this value in the
    anti-clockwise direction.

    For example, suppose a mobile device which is naturally in portrait orientation. The back-facing
    camera is mounted in landscape. If the top side of the camera sensor is aligned with the
    right edge of the screen in natural orientation, the value should be 270. If the top side of a
    front-facing camera sensor is aligned with the right of the screen, the value should be 90.
*/
int QCameraInfo::orientation() const
{
    return d->orientation;
}

QCameraInfo::QCameraInfo(QCameraInfoPrivate *p)
    : d(p)
{}

/*!
    Sets the QCameraInfo object to be equal to \a other.
*/
QCameraInfo& QCameraInfo::operator=(const QCameraInfo& other)
{
    d = other.d;
    return *this;
}

/*!
    \fn QCameraInfo::operator!=(const QCameraInfo &other) const

    Returns true if this QCameraInfo is different from \a other.
*/

#ifndef QT_NO_DEBUG_STREAM
QDebug operator<<(QDebug d, const QCameraInfo &camera)
{
    d.maybeSpace() << QStringLiteral("QCameraInfo(name=%1, position=%2, orientation=%3)")
                          .arg(camera.description())
                          .arg(QString::fromLatin1(QCamera::staticMetaObject.enumerator(QCamera::staticMetaObject.indexOfEnumerator("Position"))
                               .valueToKey(camera.position())))
                          .arg(camera.orientation());
    return d.space();
}
#endif

QT_END_NAMESPACE
