// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include <qtmultimediaglobal_p.h>
#include "qplatformmediaintegration_p.h"
#include <qatomic.h>
#include <qmutex.h>
#include <qplatformaudioinput_p.h>
#include <qplatformaudiooutput_p.h>
#include <qplatformvideodevices_p.h>
#include <qmediadevices.h>
#include <qcameradevice.h>
#include <qloggingcategory.h>
#include <QtCore/qcoreapplication.h>

#include "qplatformcapturablewindows_p.h"
#include "QtCore/private/qfactoryloader_p.h"
#include "private/qplatformmediaformatinfo_p.h"
#include "qplatformmediaplugin_p.h"

class QDummyIntegration : public QPlatformMediaIntegration
{
public:
    QDummyIntegration()
    {
        qCritical("QtMultimedia is not currently supported on this platform or compiler.");
    }
};

static Q_LOGGING_CATEGORY(qLcMediaPlugin, "qt.multimedia.plugin")

Q_GLOBAL_STATIC_WITH_ARGS(QFactoryLoader, loader,
                          (QPlatformMediaPlugin_iid,
                           QLatin1String("/multimedia")))

static const auto FFmpegBackend = QStringLiteral("ffmpeg");

static QStringList availableBackends()
{
    QStringList list;

    if (QFactoryLoader *fl = loader()) {
        const auto keyMap = fl->keyMap();
        for (auto it = keyMap.constBegin(); it != keyMap.constEnd(); ++it)
            if (!list.contains(it.value()))
                list << it.value();
    }

    qCDebug(qLcMediaPlugin) << "Available backends" << list;
    return list;
}

static QString defaultBackend(const QStringList &backends)
{
#ifdef QT_DEFAULT_MEDIA_BACKEND
    auto backend = QString::fromUtf8(QT_DEFAULT_MEDIA_BACKEND);
    if (backends.contains(backend))
        return backend;
#endif

#if defined(Q_OS_DARWIN) || defined(Q_OS_LINUX) || defined(Q_OS_WINDOWS) || defined(Q_OS_ANDROID)
    // Return ffmpeg backend by default.
    // Platform backends for the OS list are optionally available but have limited support.
    if (backends.contains(FFmpegBackend))
        return FFmpegBackend;
#else
    // Return platform backend (non-ffmpeg) by default.
    if (backends.size() > 1 && backends[0] == FFmpegBackend)
        return backends[1];
#endif

    return backends[0];
}

namespace {

struct InstanceHolder
{
    InstanceHolder()
    {
        if (!QCoreApplication::instance())
            qCCritical(qLcMediaPlugin()) << "Qt Multimedia requires a QCoreApplication instance";

        const auto backends = availableBackends();
        QString backend = QString::fromUtf8(qgetenv("QT_MEDIA_BACKEND"));
        if (backend.isEmpty() && !backends.isEmpty())
            backend = defaultBackend(backends);

        qCDebug(qLcMediaPlugin) << "loading backend" << backend;
        instance.reset(
                qLoadPlugin<QPlatformMediaIntegration, QPlatformMediaPlugin>(loader(), backend));

        if (!instance) {
            qWarning() << "could not load multimedia backend" << backend;
            instance = std::make_unique<QDummyIntegration>();
        }
    }

    std::unique_ptr<QPlatformMediaIntegration> instance;
};

Q_GLOBAL_STATIC(InstanceHolder, instanceHolder);

} // namespace

QT_BEGIN_NAMESPACE

QPlatformMediaIntegration *QPlatformMediaIntegration::instance()
{
    return instanceHolder->instance.get();
}

QList<QCameraDevice> QPlatformMediaIntegration::videoInputs()
{
    auto devices = videoDevices();
    return devices ? devices->videoDevices() : QList<QCameraDevice>{};
}

QMaybe<QPlatformAudioInput *> QPlatformMediaIntegration::createAudioInput(QAudioInput *q)
{
    return new QPlatformAudioInput(q);
}

QMaybe<QPlatformAudioOutput *> QPlatformMediaIntegration::createAudioOutput(QAudioOutput *q)
{
    return new QPlatformAudioOutput(q);
}

QList<QCapturableWindow> QPlatformMediaIntegration::capturableWindowsList()
{
    const auto capturableWindows = this->capturableWindows();
    return capturableWindows ? capturableWindows->windows() : QList<QCapturableWindow>{};
}

bool QPlatformMediaIntegration::isCapturableWindowValid(const QCapturableWindowPrivate &window)
{
    const auto capturableWindows = this->capturableWindows();
    return capturableWindows && capturableWindows->isWindowValid(window);
}

const QPlatformMediaFormatInfo *QPlatformMediaIntegration::formatInfo()
{
    std::call_once(m_formatInfoOnceFlg, [this]() {
        m_formatInfo.reset(createFormatInfo());
        Q_ASSERT(m_formatInfo);
    });
    return m_formatInfo.get();
}

QPlatformMediaFormatInfo *QPlatformMediaIntegration::createFormatInfo()
{
    return new QPlatformMediaFormatInfo;
}

QPlatformVideoDevices *QPlatformMediaIntegration::videoDevices()
{
    std::call_once(m_videoDevicesOnceFlag,
                   [this]() { m_videoDevices.reset(createVideoDevices()); });
    return m_videoDevices.get();
}

QPlatformCapturableWindows *QPlatformMediaIntegration::capturableWindows()
{
    std::call_once(m_capturableWindowsOnceFlag,
                   [this]() { m_capturableWindows.reset(createCapturableWindows()); });
    return m_capturableWindows.get();
}

QPlatformMediaIntegration::QPlatformMediaIntegration() = default;

QPlatformMediaIntegration::~QPlatformMediaIntegration() = default;

QT_END_NAMESPACE
