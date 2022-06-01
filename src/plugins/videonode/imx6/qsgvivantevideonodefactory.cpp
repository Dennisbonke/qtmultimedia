/****************************************************************************
**
** Copyright (C) 2016 Pelagicore AG
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:COMM$
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** $QT_END_LICENSE$
**
**
**
**
**
**
**
**
**
**
**
**
**
**
**
**
**
**
**
******************************************************************************/

#include "qsgvivantevideonodefactory.h"
#include "qsgvivantevideonode.h"
#include <QtGui/QGuiApplication>

QList<QVideoFrameFormat::PixelFormat> QSGVivanteVideoNodeFactory::supportedPixelFormats(
        QVideoFrame::HandleType handleType) const
{
    const bool isWebGl = QGuiApplication::platformName() == QLatin1String("webgl");
    if (!isWebGl && handleType == QVideoFrame::NoHandle)
        return QSGVivanteVideoNode::getVideoFormat2GLFormatMap().keys();
    else
        return QList<QVideoFrameFormat::PixelFormat>();
}

QSGVideoNode *QSGVivanteVideoNodeFactory::createNode(const QVideoFrameFormat &format)
{
    if (supportedPixelFormats(format.handleType()).contains(format.pixelFormat())) {
        return new QSGVivanteVideoNode(format);
    }
    return 0;
}
