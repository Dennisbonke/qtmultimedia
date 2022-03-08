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

#include "qffmpegdecoder_p.h"
#include "qffmpegmediaformatinfo_p.h"
#include "qffmpegmediadevices_p.h"
#include "private/qiso639_2_p.h"
#include "qffmpeg_p.h"
#include "qffmpegmediametadata_p.h"
#include "qffmpegvideobuffer_p.h"
#include "private/qplatformaudiooutput_p.h"
#include "qffmpeghwaccel_p.h"
#include "qffmpegvideosink_p.h"
#include "qvideosink.h"
#include "qaudiosink.h"
#include "qaudiooutput.h"
#include "qffmpegaudiodecoder_p.h"

#include <qlocale.h>
#include <qtimer.h>

#include <qloggingcategory.h>

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
}

QT_BEGIN_NAMESPACE

using namespace QFFmpeg;

Q_LOGGING_CATEGORY(qLcDemuxer, "qt.multimedia.ffmpeg.demuxer")
Q_LOGGING_CATEGORY(qLcDecoder, "qt.multimedia.ffmpeg.decoder")
Q_LOGGING_CATEGORY(qLcVideoRenderer, "qt.multimedia.ffmpeg.videoRenderer")
Q_LOGGING_CATEGORY(qLcAudioRenderer, "qt.multimedia.ffmpeg.audioRenderer")

Codec::Data::Data(AVCodecContext *context, AVStream *stream, const HWAccel &hwAccel)
    : context(context)
    , stream(stream)
    , hwAccel(hwAccel)
{
}

Codec::Data::~Data()
{
    if (!context)
        return;
    avcodec_close(context);
    avcodec_free_context(&context);
}

Codec::Codec(AVFormatContext *format, int streamIndex)
{
    qCDebug(qLcDecoder) << "Codec::Codec" << streamIndex;
    Q_ASSERT(streamIndex >= 0 && streamIndex < (int)format->nb_streams);

    AVStream *stream = format->streams[streamIndex];
    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        qCDebug(qLcDecoder) << "Failed to find a valid FFmpeg decoder";
        return;
    }

    QFFmpeg::HWAccel hwAccel;
    if (decoder->type == AVMEDIA_TYPE_VIDEO) {
        hwAccel = QFFmpeg::HWAccel(decoder);
    }

    auto *context = avcodec_alloc_context3(decoder);
    if (!context) {
        qCDebug(qLcDecoder) << "Failed to allocate a FFmpeg codec context";
        return;
    }

    int ret = avcodec_parameters_to_context(context, stream->codecpar);
    if (ret < 0) {
        qCDebug(qLcDecoder) << "Failed to set FFmpeg codec parameters";
        return;
    }

    context->hw_device_ctx = hwAccel.hwDeviceContextAsBuffer();
    // ### This still gives errors about wrong HW formats (as we accept all of them)
    // But it would be good to get so we can filter out pixel format we don't support natively
    //    context->get_format = QFFmpeg::getFormat;

    /* Init the decoder, with reference counting and threading */
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "refcounted_frames", "1", 0);
    av_dict_set(&opts, "threads", "auto", 0);
    ret = avcodec_open2(context, decoder, &opts);
    if (ret < 0) {
        qCDebug(qLcDecoder) << "Failed to open FFmpeg codec context.";
        avcodec_free_context(&context);
        return;
    }

    d = new Data(context, stream, hwAccel);
}


Demuxer::Demuxer(Decoder *decoder, AVFormatContext *context)
    : Thread()
    , decoder(decoder)
    , context(context)
{
    QString objectName = QLatin1String("Demuxer");
    setObjectName(objectName);

    streamDecoders.resize(context->nb_streams);
}

StreamDecoder *Demuxer::addStream(int streamIndex)
{
    if (streamIndex < 0)
        return nullptr;
    QMutexLocker locker(&mutex);
    Codec codec(context, streamIndex);
    if (!codec.isValid()) {
        decoder->error(QMediaPlayer::FormatError, "Invalid media file");
        return nullptr;
    }

    Q_ASSERT(codec.context()->codec_type == AVMEDIA_TYPE_AUDIO ||
             codec.context()->codec_type == AVMEDIA_TYPE_VIDEO ||
             codec.context()->codec_type == AVMEDIA_TYPE_SUBTITLE);
    auto *stream = new StreamDecoder(this, codec);
    Q_ASSERT(!streamDecoders.at(streamIndex));
    streamDecoders[streamIndex] = stream;
    stream->start();
    updateEnabledStreams();
    return stream;
}

void Demuxer::removeStream(int streamIndex)
{
    if (streamIndex < 0)
        return;
    QMutexLocker locker(&mutex);
    Q_ASSERT(streamIndex < (int)context->nb_streams);
    Q_ASSERT(streamDecoders.at(streamIndex) != nullptr);
    streamDecoders[streamIndex]->kill();
    streamDecoders[streamIndex] = nullptr;
    updateEnabledStreams();
}

void Demuxer::stopDecoding()
{
    qCDebug(qLcDemuxer) << "StopDecoding";
    QMutexLocker locker(&mutex);
    sendFinalPacketToStreams();
}
int Demuxer::seek(qint64 pos)
{
    QMutexLocker locker(&mutex);
    for (StreamDecoder *d : qAsConst(streamDecoders)) {
        if (d)
            d->mutex.lock();
    }
    for (StreamDecoder *d : qAsConst(streamDecoders)) {
        if (d)
            d->flush();
    }
    for (StreamDecoder *d : qAsConst(streamDecoders)) {
        if (d)
            d->mutex.unlock();
    }
    qint64 seekPos = pos*AV_TIME_BASE/1000;
    av_seek_frame(context, -1, seekPos, AVSEEK_FLAG_BACKWARD);
    last_pts = -1;
    while (last_pts < 0)
        loop();
    qCDebug(qLcDemuxer) << "Demuxer::seek" << pos << last_pts;
    return last_pts;
}

void Demuxer::updateEnabledStreams()
{
    if (isStopped())
        return;
    for (uint i = 0; i < context->nb_streams; ++i) {
        AVDiscard discard = AVDISCARD_DEFAULT;
        if (!streamDecoders.at(i))
            discard = AVDISCARD_ALL;
        context->streams[i]->discard = discard;
    }
}

void Demuxer::sendFinalPacketToStreams()
{
    if (m_isStopped.loadAcquire())
        return;
    for (auto *streamDecoder : qAsConst(streamDecoders)) {
        qCDebug(qLcDemuxer) << "Demuxer: sending last packet to stream" << streamDecoder;
        if (!streamDecoder)
            continue;
        streamDecoder->addPacket(nullptr);
    }
    m_isStopped.storeRelease(true);
}

void Demuxer::init()
{
    qCDebug(qLcDemuxer) << "Demuxer started";
}

void Demuxer::cleanup()
{
    qCDebug(qLcDemuxer) << "Demuxer::cleanup";
    for (auto *streamDecoder : qAsConst(streamDecoders)) {
        if (!streamDecoder)
            continue;
        streamDecoder->clearDemuxer();
    }
    avformat_close_input(&context);
    Thread::cleanup();
}

bool Demuxer::shouldWait() const
{
    if (m_isStopped)
        return true;
//    qCDebug(qLcDemuxer) << "XXXX Demuxer::shouldWait" << this << data->seek_pos.loadRelaxed();
    // require a minimum of 200ms of data
    qint64 queueSize = 0;
    bool buffersFull = true;
    for (auto *d : streamDecoders) {
        if (!d)
            continue;
        if (d->queuedDuration() < 200)
            buffersFull = false;
        queueSize += d->queuedPacketSize();
    }
//    qCDebug(qLcDemuxer) << "    queue size" << queueSize << MaxQueueSize;
    if (queueSize > MaxQueueSize)
        return true;
//    qCDebug(qLcDemuxer) << "    waiting!";
    return buffersFull;

}

void Demuxer::loop()
{
    if (m_isStopped.loadRelaxed()) {
        last_pts = 0;
        return;
    }

    AVPacket *packet = av_packet_alloc();
    if (av_read_frame(context, packet) < 0) {
        eos = true;
        sendFinalPacketToStreams();
        emit atEnd();
        return;
    }

    if (last_pts < 0 && packet->pts != AV_NOPTS_VALUE) {
        auto *stream = context->streams[packet->stream_index];
        last_pts = timeStamp(packet->pts, stream->time_base);
    }

    auto *streamDecoder = streamDecoders.at(packet->stream_index);
    if (!streamDecoder) {
        av_packet_free(&packet);
        return;
    }
    streamDecoder->addPacket(packet);
}


StreamDecoder::StreamDecoder(Demuxer *demuxer, const Codec &codec)
    : Thread()
    , demuxer(demuxer)
    , codec(codec)
{
    Q_ASSERT(codec.context()->codec_type == AVMEDIA_TYPE_AUDIO ||
             codec.context()->codec_type == AVMEDIA_TYPE_VIDEO ||
             codec.context()->codec_type == AVMEDIA_TYPE_SUBTITLE);

    QString objectName;
    switch (codec.context()->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        objectName = QLatin1String("AudioDecoderThread");
        // Queue size: 3 frames for video/subtitle, 9 for audio
        frameQueue.maxSize = 9;
        break;
    case AVMEDIA_TYPE_VIDEO:
        objectName = QLatin1String("VideoDecoderThread");
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        objectName = QLatin1String("SubtitleDecoderThread");
        break;
    default:
        Q_UNREACHABLE();
    }
    setObjectName(objectName);
}

void StreamDecoder::clearDemuxer()
{
    QMutexLocker locker(&mutex);
    demuxer = nullptr;
}


void StreamDecoder::addPacket(AVPacket *packet)
{
    {
        QMutexLocker locker(&packetQueue.mutex);
//        qCDebug(qLcDecoder) << "enqueuing packet of type" << type()
//                            << "size" << packet->size
//                            << "stream index" << packet->stream_index
//                            << "pts" << codec.toMs(packet->pts)
//                            << "duration" << codec.toMs(packet->duration);
        packetQueue.queue.enqueue(Packet(packet));
        if (packet) {
            packetQueue.size += packet->size;
            packetQueue.duration += codec.toMs(packet->duration);
        }
    }
    condition.wakeAll();
}

void StreamDecoder::flush()
{
    qCDebug(qLcDecoder) << ">>>> flushing stream decoder" << type();
    avcodec_flush_buffers(codec.context());
    {
        QMutexLocker locker(&packetQueue.mutex);
        packetQueue.queue.clear();
        packetQueue.size = 0;
        packetQueue.duration = 0;
    }
    {
        QMutexLocker locker(&frameQueue.mutex);
        frameQueue.queue.clear();
    }
    qCDebug(qLcDecoder) << ">>>> done flushing stream decoder" << type();
}

void StreamDecoder::setRenderer(Renderer *r)
{
    QMutexLocker locker(&mutex);
    m_renderer = r;
    if (m_renderer)
        m_renderer->wake();
}

void StreamDecoder::killHelper()
{
    if (m_renderer)
        m_renderer->setStream(nullptr);
}

Packet StreamDecoder::takePacket()
{
    QMutexLocker locker(&packetQueue.mutex);
    if (packetQueue.queue.isEmpty()) {
        if (demuxer)
            demuxer->wake();
        return {};
    }
    auto packet = packetQueue.queue.dequeue();
    if (packet.avPacket()) {
        packetQueue.size -= packet.avPacket()->size;
        packetQueue.duration -= codec.toMs(packet.avPacket()->duration);
    }
//    qCDebug(qLcDecoder) << "<<<< dequeuing packet of type" << type()
//                        << "size" << packet.avPacket()->size
//                        << "stream index" << packet.avPacket()->stream_index
//             << "pts" << codec.toMs(packet.avPacket()->pts)
//             << "duration" << codec.toMs(packet.avPacket()->duration)
//                    << "ts" << decoder->clockController.currentTime();
    if (demuxer)
        demuxer->wake();
    return packet;
}

void StreamDecoder::addFrame(const Frame &f)
{
    Q_ASSERT(f.isValid());
    QMutexLocker locker(&frameQueue.mutex);
    frameQueue.queue.append(std::move(f));
    if (m_renderer)
        m_renderer->wake();
}

Frame StreamDecoder::takeFrame()
{
    QMutexLocker locker(&frameQueue.mutex);
    // wake up the decoder so it delivers more frames
    if (frameQueue.queue.isEmpty()) {
        wake();
        return {};
    }
    auto f = frameQueue.queue.dequeue();
    wake();
    return f;
}

void StreamDecoder::init()
{
    qCDebug(qLcDecoder) << "Starting decoder";
}

bool StreamDecoder::shouldWait() const
{
    if (eos || hasNoPackets() || hasEnoughFrames())
        return true;
    return false;
}

void StreamDecoder::loop()
{
    if (codec.context()->codec->type == AVMEDIA_TYPE_SUBTITLE)
        decodeSubtitle();
    else
        decode();
}

void StreamDecoder::decode()
{
    Q_ASSERT(codec.context());

    AVFrame *frame = av_frame_alloc();
//    if (type() == 0)
//        qDebug() << "receiving frame";
    int res = avcodec_receive_frame(codec.context(), frame);

    if (res >= 0) {
        qint64 pts;
        if (frame->pts != AV_NOPTS_VALUE)
            pts = codec.toMs(frame->pts);
        else
            pts = codec.toMs(frame->best_effort_timestamp);
        addFrame(Frame{frame, codec, pts});
    } else if (res == AVERROR(EOF) || res == AVERROR_EOF) {
        eos = true;
        av_frame_free(&frame);
        return;
    } else if (res != AVERROR(EAGAIN)) {
        char buf[512];
        av_make_error_string(buf, 512, res);
        qWarning() << "error in decoder" << res << buf;
        av_frame_free(&frame);
        return;
    }

    Packet packet = takePacket();
    if (!packet.isValid())
        return;

    avcodec_send_packet(codec.context(), packet.avPacket());
}

void StreamDecoder::decodeSubtitle()
{
    //    qCDebug(qLcDecoder) << "    decoding subtitle" << "has delay:" << (codec->codec->capabilities & AV_CODEC_CAP_DELAY);
    AVSubtitle subtitle;
    memset(&subtitle, 0, sizeof(subtitle));
    int gotSubtitle = 0;
    Packet packet = takePacket();
    if (!packet.isValid())
        return;

    int res = avcodec_decode_subtitle2(codec.context(), &subtitle, &gotSubtitle, packet.avPacket());
    //    qCDebug(qLcDecoder) << "       subtitle got:" << res << gotSubtitle << subtitle.format << Qt::hex << (quint64)subtitle.pts;
    if (res >= 0 && gotSubtitle) {
        // apparently the timestamps in the AVSubtitle structure are not always filled in
        // if they are missing, use the packets pts and duration values instead
        qint64 start, end;
        if (subtitle.pts == AV_NOPTS_VALUE) {
            start = codec.toMs(packet.avPacket()->pts);
            end = start + codec.toMs(packet.avPacket()->duration);
        } else {
            qint64 pts = timeStamp(subtitle.pts, AVRational{1, AV_TIME_BASE});
            start = pts + subtitle.start_display_time;
            end = pts + subtitle.end_display_time;
        }
        //        qCDebug(qLcDecoder) << "    got subtitle (" << start << "--" << end << "):";
        QString text;
        for (uint i = 0; i < subtitle.num_rects; ++i) {
            const auto *r = subtitle.rects[i];
            //            qCDebug(qLcDecoder) << "    subtitletext:" << r->text << "/" << r->ass;
            if (i)
                text += QLatin1Char('\n');
            if (r->text)
                text += QString::fromUtf8(r->text);
            else {
                const char *ass = r->ass;
                int nCommas = 0;
                while (*ass) {
                    if (nCommas == 9)
                        break;
                    if (*ass == ',')
                        ++nCommas;
                    ++ass;
                }
                text += QString::fromUtf8(ass);
            }
        }
        text.replace(QLatin1String("\\N"), QLatin1String("\n"));
        text.replace(QLatin1String("\\n"), QLatin1String("\n"));
        text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        if (text.endsWith(QLatin1Char('\n')))
            text.chop(1);

//        qCDebug(qLcDecoder) << "    >>> subtitle adding" << text << start << end;
        Frame sub{text, start, end - start};
        addFrame(sub);
    }
}

QPlatformMediaPlayer::TrackType StreamDecoder::type() const
{
    switch (codec.stream()->codecpar->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        return QPlatformMediaPlayer::AudioStream;
    case AVMEDIA_TYPE_VIDEO:
        return QPlatformMediaPlayer::VideoStream;
    case AVMEDIA_TYPE_SUBTITLE:
        return QPlatformMediaPlayer::SubtitleStream;
    default:
        return QPlatformMediaPlayer::NTrackTypes;
    }
}

Renderer::Renderer(QPlatformMediaPlayer::TrackType type)
    : Thread()
    , type(type)
{
    QString objectName;
    if (type == QPlatformMediaPlayer::AudioStream)
        objectName = QLatin1String("AudioRenderThread");
    else
        objectName = QLatin1String("VideoRenderThread");
    setObjectName(objectName);
}

void Renderer::setStream(StreamDecoder *stream)
{
    QMutexLocker locker(&mutex);
    if (streamDecoder == stream)
        return;
    if (streamDecoder)
        streamDecoder->setRenderer(nullptr);
    streamDecoder = stream;
    if (streamDecoder)
        streamDecoder->setRenderer(this);
    streamChanged();
    wake();
}

void Renderer::killHelper()
{
    if (streamDecoder)
        streamDecoder->setRenderer(nullptr);
    streamDecoder = nullptr;
}

bool Renderer::shouldWait() const
{
    if (!paused.loadAcquire())
        return false;
    if (step)
        return false;
    return true;
}


void ClockedRenderer::setPaused(bool paused)
{
    Clock::setPaused(paused);
    Renderer::setPaused(paused);
}

VideoRenderer::VideoRenderer(Decoder *decoder, QVideoSink *sink)
    : ClockedRenderer(decoder, QPlatformMediaPlayer::VideoStream)
    , sink(sink)
{}

void VideoRenderer::killHelper()
{
    if (subtitleStreamDecoder)
        subtitleStreamDecoder->setRenderer(nullptr);
    if (streamDecoder)
        streamDecoder->kill();
    streamDecoder = nullptr;
}

void VideoRenderer::setSubtitleStream(StreamDecoder *stream)
{
    QMutexLocker locker(&mutex);
    qCDebug(qLcVideoRenderer) << "setting subtitle stream to" << stream;
    if (stream == subtitleStreamDecoder)
        return;
    if (subtitleStreamDecoder)
        subtitleStreamDecoder->setRenderer(nullptr);
    subtitleStreamDecoder = stream;
    if (subtitleStreamDecoder)
        subtitleStreamDecoder->setRenderer(this);
    sink->setSubtitleText({});
    wake();
}

void VideoRenderer::init()
{
    qCDebug(qLcVideoRenderer) << "starting video renderer";
    ClockedRenderer::init();
}

void VideoRenderer::loop()
{
    if (!streamDecoder) {
        timeOut = 10; // ### Fixme, this is to avoid 100% CPU load before play()
        return;
    }

    Frame frame = streamDecoder->takeFrame();
    if (!frame.isValid()) {
        timeOut = 10;
//        qDebug() << "no valid frame" << timer.elapsed();
        return;
    }
//    qCDebug(qLcVideoRenderer) << "received video frame" << frame.pts();
    if (frame.pts()*1000 < seekTime()) {
        qCDebug(qLcVideoRenderer) << "  discarding" << frame.pts() << seekTime();
        return;
    }

    AVStream *stream = frame.codec()->stream();
    qint64 startTime = frame.pts();
    qint64 duration = (1000*stream->avg_frame_rate.den + (stream->avg_frame_rate.num>>1))
                      /stream->avg_frame_rate.num;

    if (sink) {
        qint64 startTime = frame.pts();
//        qDebug() << "RHI:" << accel.isNull() << accel.rhi() << sink->rhi();
        QFFmpegVideoBuffer *buffer = new QFFmpegVideoBuffer(frame.takeAVFrame());
        QVideoFrameFormat format(buffer->size(), buffer->pixelFormat());
        QVideoFrame videoFrame(buffer, format);
        videoFrame.setStartTime(startTime);
        videoFrame.setEndTime(startTime + duration);
//        qDebug() << "Creating video frame" << startTime << (startTime + duration) << subtitleStreamDecoder;

        // add in subtitles
        const Frame *currentSubtitle = nullptr;
        if (subtitleStreamDecoder)
            currentSubtitle = subtitleStreamDecoder->lockAndPeekFrame();

        if (currentSubtitle && currentSubtitle->isValid()) {
//            qDebug() << "frame: subtitle" << currentSubtitle->text() << currentSubtitle->pts() << currentSubtitle->duration();
            qCDebug(qLcVideoRenderer) << "    " << currentSubtitle->pts() << currentSubtitle->duration() << currentSubtitle->text();
            if (currentSubtitle->pts() <= startTime && currentSubtitle->end() > startTime) {
//                qCDebug(qLcVideoRenderer) << "        setting text";
                sink->setSubtitleText(currentSubtitle->text());
            }
            if (currentSubtitle->end() < startTime) {
//                qCDebug(qLcVideoRenderer) << "        removing subtitle item";
                sink->setSubtitleText({});
                subtitleStreamDecoder->removePeekedFrame();
            }
        } else {
            sink->setSubtitleText({});
        }
        if (subtitleStreamDecoder)
            subtitleStreamDecoder->unlockAndReleaseFrame();

//        qCDebug(qLcVideoRenderer) << "    sending a video frame" << startTime << duration << decoder->baseTimer.elapsed();
        sink->setVideoFrame(videoFrame);
        doneStep();
    }
    const Frame *nextFrame = streamDecoder->lockAndPeekFrame();
    qint64 nextFrameTime = 0;
    if (nextFrame)
        nextFrameTime = nextFrame->pts();
    else
        nextFrameTime = startTime + duration;
    streamDecoder->unlockAndReleaseFrame();
    qint64 mtime = timeUpdated(startTime*1000);
    timeOut = usecsTo(mtime, nextFrameTime*1000)/1000;
//    qDebug() << "    next video frame in" << startTime << nextFrameTime << currentTime() << timeOut;
}

AudioRenderer::AudioRenderer(Decoder *decoder, QAudioOutput *output)
    : ClockedRenderer(decoder, QPlatformMediaPlayer::AudioStream)
    , output(output)
{
    connect(output, &QAudioOutput::deviceChanged, this, &AudioRenderer::updateAudio);
}

void AudioRenderer::syncTo(qint64 usecs)
{
    Clock::syncTo(usecs);
    audioBaseTime = usecs;
    processedBase = processedUSecs;
}

void AudioRenderer::setPlaybackRate(float rate, qint64 currentTime)
{
    audioBaseTime = currentTime;
    processedBase = processedUSecs;
    Clock::setPlaybackRate(rate, currentTime);
    deviceChanged = true;
}

void AudioRenderer::updateOutput(const Codec *codec)
{
    qCDebug(qLcAudioRenderer) << ">>>>>> updateOutput" << currentTime() << seekTime() << processedUSecs << isMaster();
    freeOutput();
    qCDebug(qLcAudioRenderer) << "    " << currentTime() << seekTime() << processedUSecs;

    AVStream *audioStream = codec->stream();

    format.setSampleFormat(QFFmpegMediaFormatInfo::sampleFormat(AVSampleFormat(audioStream->codecpar->format)));
    format.setSampleRate(audioStream->codecpar->sample_rate);
    format.setChannelCount(2); // #### FIXME
    // ### add channel layout

    if (playbackRate() < 0.5 || playbackRate() > 2)
        audioMuted = true;

    audioSink = new QAudioSink(output->device(), format);
    audioSink->setBufferSize(format.bytesForDuration(100000));
    audioDevice = audioSink->start();
    latencyUSecs = format.durationForBytes(audioSink->bufferSize()); // ### ideally get full latency
    qCDebug(qLcAudioRenderer) << "   -> have an audio sink" << audioDevice;

    // init resampler. It's ok to always do this, as the resampler will be a no-op if
    // formats agree.

    AVSampleFormat requiredFormat = QFFmpegMediaFormatInfo::avSampleFormat(format.sampleFormat());
    auto channelLayout = audioStream->codecpar->channel_layout;
    if (!channelLayout)
        channelLayout = QFFmpegMediaFormatInfo::avChannelLayout(QAudioFormat::defaultChannelConfigForChannelCount(audioStream->codecpar->channels));

    qCDebug(qLcAudioRenderer) << "init resampler" << requiredFormat << audioStream->codecpar->channels;
    resampler = swr_alloc_set_opts(nullptr,  // we're allocating a new context
                                   AV_CH_LAYOUT_STEREO,  // out_ch_layout
                                   requiredFormat,    // out_sample_fmt
                                   outputSamples(audioStream->codecpar->sample_rate),                // out_sample_rate
                                   channelLayout, // in_ch_layout
                                   AVSampleFormat(audioStream->codecpar->format),   // in_sample_fmt
                                   audioStream->codecpar->sample_rate,                // in_sample_rate
                                   0,                    // log_offset
                                   nullptr);
    // if we're not the master clock, we might need to handle clock adjustments, initialize for that
    av_opt_set_double(resampler, "async", format.sampleRate()/50, 0);

//    qDebug() << "setting up resampler, input rate" << audioStream->codecpar->sample_rate << "output rate:" << outputSamples(audioStream->codecpar->sample_rate);
    swr_init(resampler);
}

void AudioRenderer::freeOutput()
{
    if (audioSink) {
        audioSink->reset();
        delete audioSink;
        audioSink = nullptr;
        audioDevice = nullptr;
    }
    if (resampler) {
        swr_free(&resampler);
        resampler = nullptr;
    }
    audioMuted = false;
    bufferedData.clear();
    bufferWritten = 0;

    audioBaseTime = currentTime();
    processedBase = 0;
    processedUSecs = writtenUSecs = 0;
}

void AudioRenderer::init()
{
    qCDebug(qLcAudioRenderer) << "Starting audio renderer";
    ClockedRenderer::init();
}

void AudioRenderer::cleanup()
{
    freeOutput();
}

void AudioRenderer::loop()
{
    if (!streamDecoder) {
        timeOut = 10; // ### Fixme, this is to avoid 100% CPU load before play()
        return;
    }

    if (deviceChanged)
        freeOutput();
    deviceChanged = false;
    doneStep();

    qint64 bytesWritten = 0;
    if (!bufferedData.isEmpty()) {
        bytesWritten = audioDevice->write(bufferedData.constData() + bufferWritten, bufferedData.size() - bufferWritten);
        bufferWritten += bytesWritten;
        if (bufferWritten == bufferedData.size()) {
            bufferedData.clear();
            bufferWritten = 0;
        }
        processedUSecs = audioSink->processedUSecs();
    } else {
        Frame frame = streamDecoder->takeFrame();
        if (!frame.isValid()) {
            timeOut = 10;
            return;
        }

        if (!audioSink)
            updateOutput(frame.codec());

        qint64 startTime = frame.pts();
        if (startTime*1000 < seekTime())
            return;

        if (!paused.loadAcquire()) {
            int outSamples = outputSamples(frame.avFrame()->nb_samples);
            QByteArray samples(format.bytesForFrames(outSamples), Qt::Uninitialized);
            const uint8_t **in = (const uint8_t **)frame.avFrame()->extended_data;
            uint8_t *out = (uint8_t *)samples.data();
            int out_samples = swr_convert(resampler, &out, outSamples,
                                          in, frame.avFrame()->nb_samples);
            if (out_samples != outSamples)
                samples.resize(format.bytesForFrames(outSamples));
            if (audioMuted)
                // This is somewhat inefficient, but it'll work
                samples.fill(0);
            bytesWritten = audioDevice->write(samples.data(), samples.size());
            if (bytesWritten < samples.size()) {
                bufferedData = samples;
                bufferWritten = bytesWritten;
            }

            processedUSecs = audioSink->processedUSecs();
        }
    }

    qint64 duration = format.durationForBytes(bytesWritten);
    writtenUSecs += duration;
    timeOut = (writtenUSecs - processedUSecs - latencyUSecs)/1000;

    if (timeOut < 0)
        timeOut = 0;

//    if (!bufferedData.isEmpty())
//        qDebug() << ">>>>>>>>>>>>>>>>>>>>>>>> could not write all data" << (bufferedData.size() - bufferWritten);
//    qDebug() << "Audio: processed" << processedUSecs << "written" << writtenUSecs
//             << "delta" << (writtenUSecs - processedUSecs) << "timeOut" << timeOut;
//    qDebug() << "    updating time to" << (currentTimeNoLock()/1000);
    timeUpdated(audioBaseTime + (processedUSecs - processedBase)*playbackRate());
}

void AudioRenderer::streamChanged()
{
    // mutex is already locked
    deviceChanged = true;
}

void AudioRenderer::updateAudio()
{
    QMutexLocker locker(&mutex);
    deviceChanged = true;
}

Decoder::Decoder()
{
}

Decoder::~Decoder()
{
    pause();
    auto *vr = videoRenderer;
    auto *ar = audioRenderer;
    auto *d = demuxer;
    videoRenderer = nullptr;
    audioRenderer = nullptr;
    demuxer = nullptr;
    if (vr)
        vr->kill();
    if (ar)
        ar->kill();
    if (d)
        d->kill();
}

void Decoder::setUrl(const QUrl &media)
{
    QByteArray url = media.toEncoded(QUrl::PreferLocalFile);

    AVFormatContext *context = nullptr;
    int ret = avformat_open_input(&context, url.constData(), nullptr, nullptr);
    if (ret < 0) {
        auto code = QMediaPlayer::ResourceError;
        if (ret == AVERROR(EACCES))
            code = QMediaPlayer::AccessDeniedError;
        else if (ret == AVERROR(EINVAL))
            code = QMediaPlayer::FormatError;

        emitError(code, QMediaPlayer::tr("Could not open file"));
        return;
    }

    ret = avformat_find_stream_info(context, nullptr);
    if (ret < 0) {
        emitError(QMediaPlayer::FormatError, QMediaPlayer::tr("Could not find stream information for media file"));
        return;
    }

#ifndef QT_NO_DEBUG
    av_dump_format(context, 0, url.constData(), 0);
#endif

    m_metaData = QFFmpegMetaData::fromAVMetaData(context->metadata);
    m_metaData.insert(QMediaMetaData::FileFormat,
                      QVariant::fromValue(QFFmpegMediaFormatInfo::fileFormatForAVInputFormat(context->iformat)));

    checkStreams(context);

    m_isSeekable = !(context->ctx_flags & AVFMTCTX_UNSEEKABLE);

    demuxer = new Demuxer(this, context);
    demuxer->start();

    qCDebug(qLcDecoder) << ">>>>>> index:" << metaObject()->indexOfSlot("updateCurrentTime(qint64)");
    clockController.setNotify(this, metaObject()->method(metaObject()->indexOfSlot("updateCurrentTime(qint64)")));
}

static void insertVideoData(QMediaMetaData &metaData, AVStream *stream)
{
    Q_ASSERT(stream);
    auto *codecPar = stream->codecpar;
    metaData.insert(QMediaMetaData::VideoBitRate, (int)codecPar->bit_rate);
    metaData.insert(QMediaMetaData::VideoCodec, QVariant::fromValue(QFFmpegMediaFormatInfo::videoCodecForAVCodecId(codecPar->codec_id)));
    metaData.insert(QMediaMetaData::Resolution, QSize(codecPar->width, codecPar->height));
    metaData.insert(QMediaMetaData::VideoFrameRate,
                    qreal(stream->avg_frame_rate.num)/qreal(stream->avg_frame_rate.den));
};

static void insertAudioData(QMediaMetaData &metaData, AVStream *stream)
{
    Q_ASSERT(stream);
    auto *codecPar = stream->codecpar;
    metaData.insert(QMediaMetaData::AudioBitRate, (int)codecPar->bit_rate);
    metaData.insert(QMediaMetaData::AudioCodec,
                    QVariant::fromValue(QFFmpegMediaFormatInfo::audioCodecForAVCodecId(codecPar->codec_id)));
};

void Decoder::checkStreams(AVFormatContext *context)
{
    qint64 duration = 0;
    AVStream *firstAudioStream = nullptr;
    AVStream *defaultAudioStream = nullptr;
    AVStream *firstVideoStream = nullptr;
    AVStream *defaultVideoStream = nullptr;

    for (unsigned int i = 0; i < context->nb_streams; ++i) {
        auto *stream = context->streams[i];

        QMediaMetaData metaData = QFFmpegMetaData::fromAVMetaData(stream->metadata);
        QPlatformMediaPlayer::TrackType type = QPlatformMediaPlayer::VideoStream;
        auto *codecPar = stream->codecpar;

        bool isDefault = stream->disposition & AV_DISPOSITION_DEFAULT;
        switch (codecPar->codec_type) {
        case AVMEDIA_TYPE_UNKNOWN:
        case AVMEDIA_TYPE_DATA:          ///< Opaque data information usually continuous
        case AVMEDIA_TYPE_ATTACHMENT:    ///< Opaque data information usually sparse
        case AVMEDIA_TYPE_NB:
            continue;
        case AVMEDIA_TYPE_VIDEO:
            type = QPlatformMediaPlayer::VideoStream;
            insertVideoData(metaData, stream);
            if (!firstVideoStream)
                firstVideoStream = stream;
            if (isDefault && !defaultVideoStream)
                defaultVideoStream = stream;
            break;
        case AVMEDIA_TYPE_AUDIO:
            type = QPlatformMediaPlayer::AudioStream;
            insertAudioData(metaData, stream);
            if (!firstAudioStream)
                firstAudioStream = stream;
            if (isDefault && !defaultAudioStream)
                defaultAudioStream = stream;
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            type = QPlatformMediaPlayer::SubtitleStream;
            break;
        }
        if (isDefault && m_requestedStreams[type] < 0)
            m_requestedStreams[type] = m_streamMap[type].size();

        m_streamMap[type].append({ (int)i, isDefault, metaData });
        duration = qMax(duration, 1000*stream->duration*stream->time_base.num/stream->time_base.den);
    }

    if (m_requestedStreams[QPlatformMediaPlayer::VideoStream] < 0 && m_streamMap[QPlatformMediaPlayer::VideoStream].size()) {
        m_requestedStreams[QPlatformMediaPlayer::VideoStream] = 0;
        defaultVideoStream = firstVideoStream;
    }
    if (m_requestedStreams[QPlatformMediaPlayer::AudioStream] < 0 && m_streamMap[QPlatformMediaPlayer::AudioStream].size()) {
        m_requestedStreams[QPlatformMediaPlayer::AudioStream] = 0;
        defaultAudioStream = firstAudioStream;
    }
    if (defaultVideoStream) {
        insertVideoData(m_metaData, defaultVideoStream);
        m_currentAVStreamIndex[QPlatformMediaPlayer::VideoStream] = defaultVideoStream->index;
    }
    if (defaultAudioStream) {
        insertAudioData(m_metaData, defaultAudioStream);
        m_currentAVStreamIndex[QPlatformMediaPlayer::AudioStream] = defaultAudioStream->index;
    }
    m_requestedStreams[QPlatformMediaPlayer::SubtitleStream] = -1;
    m_currentAVStreamIndex[QPlatformMediaPlayer::SubtitleStream] = -1;

    if (player)
        player->tracksChanged();

    if (m_duration != duration) {
        m_duration = duration;
        if (player)
            player->durationChanged(duration);
        else if (audioDecoder)
            audioDecoder->durationChanged(duration);
    }
}

int Decoder::activeTrack(QPlatformMediaPlayer::TrackType type)
{
    return m_requestedStreams[type];
}

void Decoder::setActiveTrack(QPlatformMediaPlayer::TrackType type, int streamNumber)
{
    if (streamNumber < 0 || streamNumber >= m_streamMap[type].size())
        streamNumber = -1;
    if (m_requestedStreams[type] == streamNumber)
        return;
    m_requestedStreams[type] = streamNumber;
    int avStreamIndex = m_streamMap[type].value(streamNumber).avStreamIndex;
    changeAVTrack(type, avStreamIndex);
}

void Decoder::error(int errorCode, const QString &errorString)
{
    QMetaObject::invokeMethod(this, "emitError", Q_ARG(int, errorCode), Q_ARG(QString, errorString));
}

void Decoder::emitError(int error, const QString &errorString)
{
    if (player)
        player->error(error, errorString);
    else if (audioDecoder) {
        // unfortunately the error enums for QAudioDecoder and QMediaPlayer aren't identical.
        // Map them.
        switch (QMediaPlayer::Error(error)) {
        case QMediaPlayer::NoError:
            error = QAudioDecoder::NoError;
            break;
        case QMediaPlayer::ResourceError:
            error = QAudioDecoder::ResourceError;
            break;
        case QMediaPlayer::FormatError:
            error = QAudioDecoder::FormatError;
            break;
        case QMediaPlayer::NetworkError:
            // fall through, Network error doesn't exist in QAudioDecoder
        case QMediaPlayer::AccessDeniedError:
            error = QAudioDecoder::AccessDeniedError;
            break;
        }

        audioDecoder->error(error, errorString);
    }
}

void Decoder::stop()
{
    qCDebug(qLcDecoder) << "Decoder::stop";
    pause();
    if (demuxer)
        demuxer->stopDecoding();
    seek(0);
    if (videoSink)
        videoSink->setVideoFrame({});
    qCDebug(qLcDecoder) << "Decoder::stop: done" << clockController.currentTime();
}

void Decoder::setPaused(bool b)
{
    if (paused == b)
        return;
    paused = b;
    clockController.setPaused(b);
    if (!b) {
        qCDebug(qLcDecoder) << "start decoding!";
        if (demuxer)
            demuxer->startDecoding();
    }
}

void Decoder::triggerStep()
{
    if (audioRenderer)
        audioRenderer->singleStep();
    if (videoRenderer)
        videoRenderer->singleStep();
}

void Decoder::setVideoSink(QVideoSink *sink)
{
    qCDebug(qLcDecoder) << "setVideoSink" << sink;
    if (sink == videoSink)
        return;
    videoSink = sink;
    if (!videoSink || m_currentAVStreamIndex[QPlatformMediaPlayer::VideoStream] < 0) {
        if (videoRenderer) {
            videoRenderer->kill();
            videoRenderer = nullptr;
            demuxer->removeStream(m_currentAVStreamIndex[QPlatformMediaPlayer::VideoStream]);
            demuxer->removeStream(m_currentAVStreamIndex[QPlatformMediaPlayer::SubtitleStream]);
        }
    } else if (!videoRenderer) {
        videoRenderer = new VideoRenderer(this, sink);
        videoRenderer->start();
        StreamDecoder *stream = demuxer->addStream(m_currentAVStreamIndex[QPlatformMediaPlayer::VideoStream]);
        videoRenderer->setStream(stream);
        stream = demuxer->addStream(m_currentAVStreamIndex[QPlatformMediaPlayer::SubtitleStream]);
        videoRenderer->setSubtitleStream(stream);
    }
}

void Decoder::setAudioSink(QPlatformAudioOutput *output)
{
    if (audioOutput == output)
        return;

    qCDebug(qLcDecoder) << "setAudioSink" << audioOutput;
    audioOutput = output;
    if (!output || m_currentAVStreamIndex[QPlatformMediaPlayer::AudioStream] < 0) {
        if (audioRenderer) {
            audioRenderer->kill();
            audioRenderer = nullptr;
            demuxer->removeStream(m_currentAVStreamIndex[QPlatformMediaPlayer::AudioStream]);
        }
    } else if (!audioRenderer) {
        audioRenderer = new AudioRenderer(this, output->q);
        audioRenderer->start();
        auto *stream = demuxer->addStream(m_currentAVStreamIndex[QPlatformMediaPlayer::AudioStream]);
        audioRenderer->setStream(stream);
    }
}

void Decoder::changeAVTrack(QPlatformMediaPlayer::TrackType type, int streamIndex)
{
    int oldIndex = m_currentAVStreamIndex[type];
    qCDebug(qLcDecoder) << ">>>>> change track" << type << "from" << oldIndex << "to" << streamIndex << clockController.currentTime();
    m_currentAVStreamIndex[type] = streamIndex;
    if (!demuxer)
        return;
    qCDebug(qLcDecoder) << "    applying to renderer.";
    bool isPaused = paused;
    if (!isPaused)
        setPaused(true);
    auto *streamDecoder = demuxer->addStream(streamIndex);
    switch (type) {
    case QPlatformMediaPlayer::AudioStream:
        audioRenderer->setStream(streamDecoder);
        break;
    case QPlatformMediaPlayer::VideoStream:
        videoRenderer->setStream(streamDecoder);
        break;
    case QPlatformMediaPlayer::SubtitleStream:
        videoRenderer->setSubtitleStream(streamDecoder);
        break;
    default:
        Q_UNREACHABLE();
    }
    demuxer->removeStream(oldIndex);
    demuxer->seek(clockController.currentTime()/1000);
    if (!isPaused)
        setPaused(false);
    else
        triggerStep();
}

QPlatformMediaPlayer::TrackType trackType(AVMediaType mediaType)
{
    switch (mediaType) {
    case AVMEDIA_TYPE_VIDEO:
        return QPlatformMediaPlayer::VideoStream;
    case AVMEDIA_TYPE_AUDIO:
        return QPlatformMediaPlayer::AudioStream;
    case AVMEDIA_TYPE_SUBTITLE:
        return QPlatformMediaPlayer::SubtitleStream;
    default:
        break;
    }
    return QPlatformMediaPlayer::NTrackTypes;
}

void Decoder::seek(qint64 pos)
{
    if (!demuxer)
        return;
    qint64 currentTime = demuxer->seek(pos);
    clockController.syncTo(pos*1000);
    if (player)
        player->positionChanged(currentTime);
    demuxer->wake();
    triggerStep();
}

void Decoder::setPlaybackRate(float rate)
{
    bool p = paused;
    if (!paused)
        setPaused(true);
    clockController.setPlaybackRate(rate);
    if (!p)
        setPaused(false);
}

void Decoder::updateCurrentTime(qint64 time)
{
    if (player)
        player->positionChanged(time/1000);
}

QT_END_NAMESPACE
