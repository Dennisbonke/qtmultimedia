/****************************************************************************
**
** Copyright (C) 2022 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtMultimedia of the Qt Toolkit.
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

package org.qtproject.qt.android.multimedia;

import android.graphics.SurfaceTexture;

public class QtSurfaceTextureListener implements SurfaceTexture.OnFrameAvailableListener
{
    private final long m_id;

    public QtSurfaceTextureListener(long id)
    {
        m_id = id;
    }

    @Override
    public void onFrameAvailable(SurfaceTexture surfaceTexture)
    {
        notifyFrameAvailable(m_id);
    }

    private static native void notifyFrameAvailable(long id);
}
