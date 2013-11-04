/****************************************************************************
**
** Copyright (C) 2013 Research In Motion
** Contact: http://www.qt-project.org/legal
**
** This file is part of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "ppsmediaplayercontrol.h"
#include "bbvideowindowcontrol.h"

#include <QtCore/qfile.h>
#include <QtCore/qsocketnotifier.h>
#include <QtCore/private/qcore_unix_p.h>

#include <screen/screen.h>
#include <sys/pps.h>

QT_BEGIN_NAMESPACE

PpsMediaPlayerControl::PpsMediaPlayerControl(QObject *parent)
    : BbMediaPlayerControl(parent),
    m_ppsStatusNotifier(0),
    m_ppsStatusFd(-1),
    m_ppsStateNotifier(0),
    m_ppsStateFd(-1)
  , m_previouslySeenState("STOPPED")
{
    openConnection();
}

PpsMediaPlayerControl::~PpsMediaPlayerControl()
{
    destroy();
}

void PpsMediaPlayerControl::startMonitoring(int, const QString &contextName)
{
    const QString ppsContextPath = QStringLiteral("/pps/services/multimedia/renderer/context/%1/").arg(contextName);
    const QString ppsStatusPath = ppsContextPath + QStringLiteral("/status");

    Q_ASSERT(m_ppsStatusFd == -1);
    errno = 0;
    m_ppsStatusFd = qt_safe_open(QFile::encodeName(ppsStatusPath).constData(), O_RDONLY);
    if (m_ppsStatusFd == -1) {
        emitPError(QStringLiteral("Unable to open %1: %2").arg(ppsStatusPath, qt_error_string(errno)));
        return;
    }

    Q_ASSERT(!m_ppsStatusNotifier);
    m_ppsStatusNotifier = new QSocketNotifier(m_ppsStatusFd, QSocketNotifier::Read);
    connect(m_ppsStatusNotifier, SIGNAL(activated(int)), this, SLOT(ppsReadyRead(int)));


    const QString ppsStatePath = ppsContextPath + QStringLiteral("/state");

    Q_ASSERT(m_ppsStateFd == -1);
    errno = 0;
    m_ppsStateFd = qt_safe_open(QFile::encodeName(ppsStatePath).constData(), O_RDONLY);
    if (m_ppsStateFd == -1) {
        emitPError(QStringLiteral("Unable to open %1: %2").arg(ppsStatePath, qt_error_string(errno)));
        return;
    }

    Q_ASSERT(!m_ppsStateNotifier);
    m_ppsStateNotifier = new QSocketNotifier(m_ppsStateFd, QSocketNotifier::Read);
    connect(m_ppsStateNotifier, SIGNAL(activated(int)), this, SLOT(ppsReadyRead(int)));

    //ensure we receive any initial state
    ppsReadyRead(m_ppsStatusFd);
    ppsReadyRead(m_ppsStateFd);
}

void PpsMediaPlayerControl::stopMonitoring()
{

    if (m_ppsStatusFd != -1) {
        ::close(m_ppsStatusFd);
        m_ppsStatusFd = -1;
    }

    delete m_ppsStatusNotifier;
    m_ppsStatusNotifier = 0;

    if (m_ppsStateFd != -1) {
        ::close(m_ppsStateFd);
        m_ppsStateFd = -1;
    }

    delete m_ppsStateNotifier;
    m_ppsStateNotifier = 0;
}

bool PpsMediaPlayerControl::nativeEventFilter(const QByteArray &eventType, void *message, long *result)
{
    Q_UNUSED(result)
    if (eventType == "screen_event_t") {
        screen_event_t event = static_cast<screen_event_t>(message);
        if (BbVideoWindowControl *control = videoWindowControl())
            control->screenEventHandler(event);
    }

    return false;
}

void PpsMediaPlayerControl::ppsReadyRead(int fd)
{
    Q_ASSERT(fd == m_ppsStateFd || fd == m_ppsStatusFd);
    const int bufferSize = 2048;
    char buffer[bufferSize];
    const ssize_t nread = qt_safe_read(fd, buffer, bufferSize - 1);
    if (nread < 0) {
        //TODO emit error?
    }

    if (nread == 0) {
        return;
    }

    // nread is the real space necessary, not the amount read.
    if (static_cast<size_t>(nread) > bufferSize - 1) {
        //TODO emit error?
        qCritical("BBMediaPlayerControl: PPS buffer size too short; need %u.", nread + 1);
        return;
    }

    buffer[nread] = 0;

    pps_decoder_t decoder;

    if (pps_decoder_initialize(&decoder, buffer) != PPS_DECODER_OK) {
        //TODO emit error?
        qCritical("Could not initialize pps_decoder");
        pps_decoder_cleanup(&decoder);
        return;
    }

    pps_decoder_push(&decoder, 0);

    const char *value = 0;
    if (pps_decoder_get_string(&decoder, "bufferlevel", &value) == PPS_DECODER_OK) {
        setMmBufferStatus(QString::fromLatin1(value));
    }

    if (pps_decoder_get_string(&decoder, "state", &value) == PPS_DECODER_OK) {
        const QByteArray state = value;
        if (state != m_previouslySeenState && state == "STOPPED")
            handleMmStopped();
        m_previouslySeenState = state;
    }

    if (pps_decoder_get_string(&decoder, "position", &value) == PPS_DECODER_OK) {
        const QByteArray valueBa = QByteArray(value);
        bool ok;
        const qint64 position = valueBa.toLongLong(&ok);
        if (!ok) {
            qCritical("Could not parse position from '%s'", valueBa.constData());
        } else {
            setMmPosition(position);
        }
    }

    pps_decoder_cleanup(&decoder);
}

QT_END_NAMESPACE
