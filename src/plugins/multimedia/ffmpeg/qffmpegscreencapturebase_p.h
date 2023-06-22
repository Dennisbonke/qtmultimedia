// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QFFMPEGSCREENCAPTUREBASE_P_H
#define QFFMPEGSCREENCAPTUREBASE_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API. It exists purely as an
// implementation detail. This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <private/qplatformsurfacecapture_p.h>

#include "qpointer.h"

QT_BEGIN_NAMESPACE

class QFFmpegScreenCaptureBase : public QPlatformSurfaceCapture
{
public:
    using QPlatformSurfaceCapture::QPlatformSurfaceCapture;

    void setActive(bool active) final;

    bool isActive() const final;

    void setScreen(QScreen *screen) final;

    QScreen *screen() const final;

protected:
    virtual bool setActiveInternal(bool active) = 0;

private:
    template<typename Source, typename NewSource, typename Signal>
    void setSource(Source &source, NewSource newSource, Signal sig);

private:
    bool m_active = false;
    QPointer<QScreen> m_screen;
};

QT_END_NAMESPACE

#endif // QFFMPEGSCREENCAPTUREBASE_P_H
