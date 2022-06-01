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

#ifndef QSGVIDEONODEFACTORY_VIVANTE_H
#define QSGVIDEONODEFACTORY_VIVANTE_H

#include <QObject>
#include <private/qsgvideonode_p.h>

class QSGVivanteVideoNodeFactory : public QObject, public QSGVideoNodeFactoryInterface
{
public:
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QSGVideoNodeFactoryInterface_iid FILE "imx6.json")
    Q_INTERFACES(QSGVideoNodeFactoryInterface)

    QList<QVideoFrameFormat::PixelFormat> supportedPixelFormats(QVideoFrame::HandleType handleType) const;
    QSGVideoNode *createNode(const QVideoFrameFormat &format);
};
#endif // QSGVIDEONODEFACTORY_VIVANTE_H
