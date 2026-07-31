/**
 * @file camerawidget.h
 * @brief 摄像头实时画面组件 (Qt6)
 *
 * 使用 Qt6 Multimedia 模块实现摄像头画面采集与显示。
 * 支持拍照截图、视频录制功能。
 */

#ifndef CAMERAWIDGET_H
#define CAMERAWIDGET_H

#include <QWidget>
#include <QCamera>
#include <QCameraDevice>
#include <QImageCapture>
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QVideoSink>
#include <QImage>
#include <QRectF>
#include <QVector>

#ifdef Q_OS_WASM
class QTimer;
#endif

class CameraWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CameraWidget(QWidget *parent = nullptr);
    ~CameraWidget();

    void startCamera();                       // 启动摄像头；WASM 下需由用户点击触发
    void stopCamera();                        // 停止摄像头
    bool isCameraActive() const;
    bool hasFrame() const;
    bool hasStaticBackground() const;
    QImage currentDisplayFrame() const;
    bool captureCurrentFrameAsBackground();    // 截取当前实时帧并固定为背景
    void setFaceBoxes(const QVector<QRectF> &boxes); // 图片归一化坐标 [0,1]
    void clearFaceBoxes();
    bool hasFaceBoxes() const;
    bool showNextMarkedFaceFullscreen();        // 按原图比例裁剪已标记人脸并全屏放大，循环切换
    void exitFaceZoom();
    void capturePhoto();
    void requestFrame();                      // 从实时流请求一帧，发射 frameCaptured
    void startRecording();
    void stopRecording();
    void setVolume(int volume);
    void switchCamera();
    void setCameraDevice(int index);   // 按索引切换摄像头
    int currentCameraIndex() const;
    void restartCamera();

signals:
    void frameCaptured(const QImage &frame);  // 实时帧捕获结果

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void initCamera();

    QCamera *camera_;
    QMediaCaptureSession *capture_session_;
    QImageCapture *image_capture_;
    QMediaRecorder *media_recorder_;
    QVideoSink *video_sink_;
#ifdef Q_OS_WASM
    QTimer *wasm_frame_poll_timer_;
#endif
    QImage current_frame_;
    QImage background_frame_;
    QVector<QRectF> face_boxes_;
    bool face_zoom_active_;
    int face_zoom_index_;
    int current_volume_;
    int current_camera_index_;
};

#endif // CAMERAWIDGET_H
