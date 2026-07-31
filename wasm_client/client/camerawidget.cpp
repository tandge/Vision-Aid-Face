/**
 * @file camerawidget.cpp
 * @brief 摄像头实时画面组件实现 (Qt6)
 */

#include "camerawidget.h"

#include <QMediaDevices>
#include <QResizeEvent>
#include <QPainter>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QUrl>
#include <QVideoFrame>
#include <QByteArray>
#include <QImage>
#include <QPen>
#ifdef Q_OS_WASM
#include <QTimer>
#include <emscripten/emscripten.h>
#endif

#ifdef Q_OS_WASM
namespace {
EM_JS(void, wasmStartBrowserCamera, (int cameraIndex), {
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
        console.error('getUserMedia is not available in this browser.');
        return;
    }

    if (window.bnefCameraStream
        && window.bnefCameraDeviceIndex === cameraIndex
        && window.bnefCameraStream.getVideoTracks().some(track => track.readyState === 'live')) {
        return;
    }

    const startSelectedCamera = async () => {
        let constraints = { video: true, audio: false };
        try {
            if (navigator.mediaDevices.enumerateDevices) {
                const devices = await navigator.mediaDevices.enumerateDevices();
                const cameras = devices.filter(device => device.kind === 'videoinput');
                if (cameraIndex >= 0 && cameraIndex < cameras.length && cameras[cameraIndex].deviceId) {
                    constraints = { video: { deviceId: { exact: cameras[cameraIndex].deviceId } }, audio: false };
                }
            }
        } catch (error) {
            console.warn('Failed to enumerate cameras before opening selected camera:', error);
        }

        if (window.bnefCameraStream) {
            window.bnefCameraStream.getTracks().forEach(track => track.stop());
            window.bnefCameraStream = null;
        }

        const stream = await navigator.mediaDevices.getUserMedia(constraints);
        window.bnefCameraStream = stream;
        window.bnefCameraDeviceIndex = cameraIndex;
        let video = document.getElementById('bnef-hidden-camera-video');
        if (!video) {
            video = document.createElement('video');
            video.id = 'bnef-hidden-camera-video';
            video.playsInline = true;
            video.muted = true;
            video.autoplay = true;
            video.style.position = 'fixed';
            video.style.left = '-10000px';
            video.style.top = '-10000px';
            video.style.width = '1px';
            video.style.height = '1px';
            video.style.opacity = '0';
            video.style.pointerEvents = 'none';
            document.body.appendChild(video);
        }
        video.srcObject = stream;
        const playPromise = video.play();
        if (playPromise && playPromise.catch) {
            playPromise.catch(error => console.warn('Camera video play failed:', error));
        }
    };

    startSelectedCamera().catch(error => {
        console.error('Failed to open browser camera:', error);
    });
});

EM_JS(void, wasmStopBrowserCamera, (), {
    if (window.bnefCameraStream) {
        window.bnefCameraStream.getTracks().forEach(track => track.stop());
        window.bnefCameraStream = null;
    }
    const video = document.getElementById('bnef-hidden-camera-video');
    if (video) {
        video.pause();
        video.srcObject = null;
    }
});

EM_JS(int, wasmBrowserCameraActive, (), {
    const stream = window.bnefCameraStream;
    const video = document.getElementById('bnef-hidden-camera-video');
    return !!(stream && video && video.readyState >= 2
        && stream.getVideoTracks().some(track => track.readyState === 'live'));
});

EM_JS(int, wasmBrowserCameraWidth, (), {
    const video = document.getElementById('bnef-hidden-camera-video');
    return video && video.videoWidth ? video.videoWidth : 0;
});

EM_JS(int, wasmBrowserCameraHeight, (), {
    const video = document.getElementById('bnef-hidden-camera-video');
    return video && video.videoHeight ? video.videoHeight : 0;
});

EM_JS(int, wasmCopyBrowserCameraFrame, (unsigned char *dest, int maxBytes), {
    const video = document.getElementById('bnef-hidden-camera-video');
    if (!video || video.readyState < 2 || !video.videoWidth || !video.videoHeight) {
        return 0;
    }

    let canvas = document.getElementById('bnef-hidden-camera-canvas');
    if (!canvas) {
        canvas = document.createElement('canvas');
        canvas.id = 'bnef-hidden-camera-canvas';
        canvas.style.display = 'none';
        document.body.appendChild(canvas);
    }

    const width = video.videoWidth;
    const height = video.videoHeight;
    const bytes = width * height * 4;
    if (bytes > maxBytes) {
        return 0;
    }

    if (canvas.width !== width) canvas.width = width;
    if (canvas.height !== height) canvas.height = height;
    const ctx = canvas.getContext('2d', { willReadFrequently: true });
    ctx.drawImage(video, 0, 0, width, height);
    const rgba = ctx.getImageData(0, 0, width, height).data;
    HEAPU8.set(rgba, dest);
    return bytes;
});
}
#endif

CameraWidget::CameraWidget(QWidget *parent)
    : QWidget(parent)
    , camera_(nullptr)
    , capture_session_(nullptr)
    , image_capture_(nullptr)
    , media_recorder_(nullptr)
    , video_sink_(nullptr)
#ifdef Q_OS_WASM
    , wasm_frame_poll_timer_(nullptr)
#endif
    , face_zoom_active_(false)
    , face_zoom_index_(0)
    , current_volume_(50)
    , current_camera_index_(0)
{
    setStyleSheet("background: #000000; border-radius: 24px;");
    setAttribute(Qt::WA_StyledBackground, true);
#ifndef Q_OS_WASM
    initCamera();
#endif
}

CameraWidget::~CameraWidget()
{
    if (media_recorder_ && media_recorder_->recorderState() == QMediaRecorder::RecordingState) {
        media_recorder_->stop();
    }
    if (camera_ && camera_->isActive()) {
        camera_->stop();
    }
}

void CameraWidget::startCamera()
{
#ifdef Q_OS_WASM
    if (!wasm_frame_poll_timer_) {
        wasm_frame_poll_timer_ = new QTimer(this);
        wasm_frame_poll_timer_->setInterval(33);
        connect(wasm_frame_poll_timer_, &QTimer::timeout, this, [this]() {
            const int frame_width = wasmBrowserCameraWidth();
            const int frame_height = wasmBrowserCameraHeight();
            if (frame_width <= 0 || frame_height <= 0 || !wasmBrowserCameraActive()) {
                update();
                return;
            }

            QByteArray frame_data;
            frame_data.resize(frame_width * frame_height * 4);
            const int copied = wasmCopyBrowserCameraFrame(
                reinterpret_cast<unsigned char *>(frame_data.data()), frame_data.size());
            if (copied != frame_data.size()) {
                update();
                return;
            }

            QImage image(reinterpret_cast<const uchar *>(frame_data.constData()),
                         frame_width, frame_height, QImage::Format_RGBA8888);
            if (!image.isNull()) {
                current_frame_ = image.copy();
                update();
            }
        });
    }
    wasmStartBrowserCamera(current_camera_index_);
    wasm_frame_poll_timer_->start();
    background_frame_ = QImage();
    clearFaceBoxes();
    update();
    return;
#else
    if (camera_ && camera_->isActive()) {
        return;
    }

    if (!camera_) {
        initCamera();
    } else {
        camera_->start();
    }

    update();
#endif
}

void CameraWidget::stopCamera()
{
#ifdef Q_OS_WASM
    if (wasm_frame_poll_timer_) {
        wasm_frame_poll_timer_->stop();
    }
    wasmStopBrowserCamera();
    current_frame_ = QImage();
    background_frame_ = QImage();
    clearFaceBoxes();
    update();
    return;
#else
    if (media_recorder_ && media_recorder_->recorderState() == QMediaRecorder::RecordingState) {
        media_recorder_->stop();
    }

    if (camera_ && camera_->isActive()) {
        camera_->stop();
    }

    update();
#endif
}

bool CameraWidget::isCameraActive() const
{
#ifdef Q_OS_WASM
    return wasmBrowserCameraActive();
#else
    return camera_ && camera_->isActive();
#endif
}

bool CameraWidget::hasFrame() const
{
    return !current_frame_.isNull();
}

bool CameraWidget::hasStaticBackground() const
{
    return !background_frame_.isNull();
}

QImage CameraWidget::currentDisplayFrame() const
{
    return background_frame_.isNull() ? current_frame_ : background_frame_;
}

bool CameraWidget::captureCurrentFrameAsBackground()
{
    if (current_frame_.isNull()) {
        return false;
    }

    background_frame_ = current_frame_.copy();
    clearFaceBoxes();
    update();
    return true;
}

void CameraWidget::setFaceBoxes(const QVector<QRectF> &boxes)
{
    face_boxes_ = boxes;
    face_zoom_active_ = false;
    face_zoom_index_ = 0;
    update();
}

void CameraWidget::clearFaceBoxes()
{
    if (face_boxes_.isEmpty() && !face_zoom_active_) {
        return;
    }
    face_boxes_.clear();
    face_zoom_active_ = false;
    face_zoom_index_ = 0;
    update();
}

bool CameraWidget::hasFaceBoxes() const
{
    return !face_boxes_.isEmpty();
}

bool CameraWidget::showNextMarkedFaceFullscreen()
{
    if (background_frame_.isNull() && !current_frame_.isNull()) {
        // 从实时检测切到人脸放大时，先冻结当前帧，避免后续实时画面移动导致旧人脸框错位。
        background_frame_ = current_frame_.copy();
    }

    const QImage source = currentDisplayFrame();
    if (source.isNull() || face_boxes_.isEmpty()) {
        return false;
    }

    if (!face_zoom_active_ || face_zoom_index_ < 0 || face_zoom_index_ >= face_boxes_.size()) {
        face_zoom_index_ = 0;
        face_zoom_active_ = true;
    } else {
        face_zoom_index_ = (face_zoom_index_ + 1) % face_boxes_.size();
    }
    update();
    return true;
}

void CameraWidget::exitFaceZoom()
{
    if (!face_zoom_active_) {
        return;
    }
    face_zoom_active_ = false;
    face_zoom_index_ = 0;
    update();
}

void CameraWidget::initCamera()
{
#ifdef Q_OS_WASM
    startCamera();
    return;
#else
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();

    qDebug() << "=== CameraWidget::initCamera called ===";
    qDebug() << "QMediaDevices::videoInputs() count:" << cameras.size();

    for (int i = 0; i < cameras.size(); ++i) {
        const QCameraDevice &device = cameras[i];
        qDebug() << "  Camera" << i << ":" << device.description()
                 << "(" << (device.position() == QCameraDevice::FrontFace ? "Front" :
                            device.position() == QCameraDevice::BackFace ? "Back" : "Other") << ")";
    }

    if (cameras.isEmpty()) {
#ifndef Q_OS_WASM
        qWarning() << "No camera device found, showing placeholder";
        update();
        return;
#else
        // WASM 授权前 videoInputs() 可能为空；使用默认摄像头启动 getUserMedia 权限请求。
        qWarning() << "No enumerated camera device, trying default browser camera";
#endif
    }

    if (!cameras.isEmpty() && (current_camera_index_ < 0 || current_camera_index_ >= cameras.size())) {
        current_camera_index_ = 0;
        for (int i = 0; i < cameras.size(); ++i) {
            if (cameras[i].position() == QCameraDevice::BackFace) {
                current_camera_index_ = i;
                break;
            }
        }
    }

    qDebug() << "Selected camera index:" << current_camera_index_;

    capture_session_ = new QMediaCaptureSession(this);
#ifdef Q_OS_WASM
    camera_ = cameras.isEmpty() ? new QCamera(this) : new QCamera(cameras[current_camera_index_], this);
#else
    camera_ = new QCamera(cameras[current_camera_index_], this);
#endif
    video_sink_ = new QVideoSink(this);
    image_capture_ = new QImageCapture(this);
    media_recorder_ = new QMediaRecorder(this);

    connect(video_sink_, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (!frame.isValid()) {
            return;
        }
        QImage image = frame.toImage();
        if (!image.isNull()) {
            current_frame_ = image;
            update();
        }
    });

    capture_session_->setCamera(camera_);
    capture_session_->setVideoOutput(video_sink_);
    capture_session_->setImageCapture(image_capture_);
    capture_session_->setRecorder(media_recorder_);

    camera_->start();
#endif
}

void CameraWidget::capturePhoto()
{
#ifdef Q_OS_WASM
    qWarning() << "Photo capture is not supported in browser camera fallback";
    return;
#else
    if (!image_capture_ || !camera_ || !camera_->isActive()) {
        qWarning() << "Cannot capture photo: camera not ready";
        return;
    }

    const QString pictures_dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QDir().mkpath(pictures_dir);
    const QString photo_path = pictures_dir + "/eye_friend_photo_"
        + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".jpg";
    image_capture_->captureToFile(photo_path);
    qDebug() << "Photo capture requested:" << photo_path;
#endif
}

void CameraWidget::requestFrame()
{
#ifdef Q_OS_WASM
    if (!current_frame_.isNull()) {
        emit frameCaptured(current_frame_);
    }
    return;
#else
    if (!image_capture_ || !camera_ || !camera_->isActive()) {
        qWarning() << "Cannot capture frame: camera not ready";
        return;
    }

    QMetaObject::Connection * const connPtr = new QMetaObject::Connection;
    *connPtr = connect(image_capture_, &QImageCapture::imageCaptured,
            this, [this, connPtr](int id, const QImage &preview) {
        disconnect(*connPtr);
        delete connPtr;
        Q_UNUSED(id)
        if (!preview.isNull()) {
            emit frameCaptured(preview);
        }
    });
    image_capture_->capture();
#endif
}

void CameraWidget::startRecording()
{
#ifdef Q_OS_WASM
    qWarning() << "Recording is not supported in browser camera fallback";
    return;
#else
    if (!media_recorder_ || !camera_ || !camera_->isActive()) {
        qWarning() << "Cannot start recording: camera not ready";
        return;
    }
    if (media_recorder_->recorderState() == QMediaRecorder::RecordingState) {
        qWarning() << "Already recording";
        return;
    }

    const QString videos_dir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    QDir().mkpath(videos_dir);
    const QString output_path = videos_dir + "/eye_friend_recording_"
        + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".mp4";
    media_recorder_->setOutputLocation(QUrl::fromLocalFile(output_path));
    media_recorder_->record();
    qDebug() << "Recording started to:" << output_path;
#endif
}

void CameraWidget::stopRecording()
{
#ifdef Q_OS_WASM
    return;
#else
    if (media_recorder_ && media_recorder_->recorderState() == QMediaRecorder::RecordingState) {
        media_recorder_->stop();
        qDebug() << "Recording stopped";
    }
#endif
}

void CameraWidget::setVolume(int volume)
{
    current_volume_ = qBound(0, volume, 100);
    qDebug() << "Volume set to:" << current_volume_;
}

void CameraWidget::switchCamera()
{
#ifdef Q_OS_WASM
    restartCamera();
    return;
#else
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    if (cameras.size() < 2) {
        return;
    }
    const int next = (current_camera_index_ + 1) % cameras.size();
    setCameraDevice(next);
#endif
}

void CameraWidget::setCameraDevice(int index)
{
#ifdef Q_OS_WASM
    current_camera_index_ = qMax(0, index);
    if (wasm_frame_poll_timer_ && wasm_frame_poll_timer_->isActive()) {
        stopCamera();
        startCamera();
    }
    return;
#else
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    if (index < 0 || index >= cameras.size()) {
        qWarning() << "Invalid camera index:" << index;
        return;
    }

    if (media_recorder_ && media_recorder_->recorderState() == QMediaRecorder::RecordingState) {
        media_recorder_->stop();
    }
    if (camera_) {
        camera_->stop();
    }

    delete capture_session_;
    capture_session_ = nullptr;
    delete image_capture_;
    image_capture_ = nullptr;
    delete media_recorder_;
    media_recorder_ = nullptr;
    delete video_sink_;
    video_sink_ = nullptr;
    delete camera_;
    camera_ = nullptr;

    current_camera_index_ = index;
    current_frame_ = QImage();
    background_frame_ = QImage();
    face_boxes_.clear();
    capture_session_ = new QMediaCaptureSession(this);
    camera_ = new QCamera(cameras[index], this);
    video_sink_ = new QVideoSink(this);
    image_capture_ = new QImageCapture(this);
    media_recorder_ = new QMediaRecorder(this);

    connect(video_sink_, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (!frame.isValid()) {
            return;
        }
        QImage image = frame.toImage();
        if (!image.isNull()) {
            current_frame_ = image;
            update();
        }
    });

    capture_session_->setCamera(camera_);
    capture_session_->setVideoOutput(video_sink_);
    capture_session_->setImageCapture(image_capture_);
    capture_session_->setRecorder(media_recorder_);

    camera_->start();
#endif
}

int CameraWidget::currentCameraIndex() const
{
    return current_camera_index_;
}

void CameraWidget::restartCamera()
{
#ifdef Q_OS_WASM
    stopCamera();
    startCamera();
    return;
#else
    if (media_recorder_ && media_recorder_->recorderState() == QMediaRecorder::RecordingState) {
        media_recorder_->stop();
    }

    if (camera_) {
        camera_->stop();
    }

    delete capture_session_;
    capture_session_ = nullptr;
    delete image_capture_;
    image_capture_ = nullptr;
    delete media_recorder_;
    media_recorder_ = nullptr;
    delete video_sink_;
    video_sink_ = nullptr;
    delete camera_;
    camera_ = nullptr;
    current_frame_ = QImage();
    background_frame_ = QImage();
    face_boxes_.clear();

    initCamera();
#endif
}

void CameraWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
}

void CameraWidget::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QImage display_frame = background_frame_.isNull() ? current_frame_ : background_frame_;
    if (!display_frame.isNull()) {
        const QImage frame = display_frame.convertToFormat(QImage::Format_RGB32);

        if (face_zoom_active_ && face_zoom_index_ >= 0 && face_zoom_index_ < face_boxes_.size()) {
            QRectF normalized = face_boxes_[face_zoom_index_].normalized();
            normalized = normalized.adjusted(-normalized.width() * 0.15,
                                             -normalized.height() * 0.15,
                                             normalized.width() * 0.15,
                                             normalized.height() * 0.15);
            normalized = normalized.intersected(QRectF(0.0, 0.0, 1.0, 1.0));
            QRect cropRect(qRound(normalized.left() * frame.width()),
                           qRound(normalized.top() * frame.height()),
                           qRound(normalized.width() * frame.width()),
                           qRound(normalized.height() * frame.height()));
            cropRect = cropRect.intersected(frame.rect());
            if (!cropRect.isEmpty()) {
                const QImage face = frame.copy(cropRect);
                painter.fillRect(rect(), Qt::black);
                const QSize scaledSize = face.size().scaled(size(), Qt::KeepAspectRatio);
                const QRect target((width() - scaledSize.width()) / 2,
                                   (height() - scaledSize.height()) / 2,
                                   scaledSize.width(), scaledSize.height());
                painter.drawImage(target, face);
                return;
            }
        }

        const QSize scaledSize = frame.size().scaled(size(), Qt::KeepAspectRatioByExpanding);
        const QRect target((width() - scaledSize.width()) / 2,
                           (height() - scaledSize.height()) / 2,
                           scaledSize.width(), scaledSize.height());
        painter.drawImage(target, frame);

        if (!face_boxes_.isEmpty()) {
            painter.save();
            QPen pen(QColor(0, 255, 120));
            pen.setWidth(4);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            for (const QRectF &box : face_boxes_) {
                const QRectF mapped(target.left() + box.left() * target.width(),
                                    target.top() + box.top() * target.height(),
                                    box.width() * target.width(),
                                    box.height() * target.height());
                painter.drawRect(mapped);
            }
            painter.restore();
        }
        return;
    }

    QLinearGradient gradient(0, 0, 0, height());
    gradient.setColorAt(0.0, QColor("#DCDCDC"));
    gradient.setColorAt(0.5, QColor("#E5E5E5"));
    gradient.setColorAt(1.0, QColor("#F0F0F0"));
    painter.fillRect(rect(), gradient);
    painter.setPen(QColor(120, 120, 120));
    QFont font("Inter", 14);
    painter.setFont(font);
    painter.drawText(rect(), Qt::AlignCenter, "Camera Not Ready");
}
