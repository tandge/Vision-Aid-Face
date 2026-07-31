/**
 * @file mainwindow.h
 * @brief 主窗口 - 应用顶层容器
 *
 * 主窗口负责整合所有子组件：
 * - CameraWidget: 摄像头实时画面背景
 * - PulseDots: 语音状态脉冲圆点动画
 * - 底部4个功能按钮 (对应 docs/gui.html):
 *   1. 麦克风按钮 (Microphone): 开启/关闭语音输入
 *   2. 上传/发送按钮 (Send): 发送当前画面/识别文本
 *   3. 视频通话按钮 (Video Call): 开启/关闭视频通话
 *   4. 关闭按钮 (End Call): 结束通话/关闭窗口
 *
 * UI 布局通过 Qt Designer 的 mainwindow.ui 描述, 由 uic 自动生成
 * ui_mainwindow.h, 该头文件提供 Ui::MainWindow 类与全部控件指针,
 * 因此本类不再需要重复维护各控件的成员变量。
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QDateTime>
#include <QRectF>
#include <QVector>
#include <QListWidgetItem>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class WhisperClient;
class QImage;

#ifdef HAS_TEXTTOSPEECH
QT_BEGIN_NAMESPACE
class QTextToSpeech;
QT_END_NAMESPACE
#endif

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onMicrophoneToggled(bool checked);   // 按钮1: 麦克风
    void onSendClicked();                     // 按钮2: 发送/上传
    void onVideoToggled(bool checked);        // 按钮3: 视频通话
    void onEndCallClicked();                  // 按钮4: 结束通话 (红色 X)
    void updateStatusBarTime();
    void onTranscriptionReceived(const QString &text);
#ifndef Q_OS_WASM
    void onFrameCapturedForFace(const QImage &frame); // 视频按钮：实时帧人脸识别
#endif

private:
    void refreshCameraDevices();              // 刷新摄像头下拉列表，WASM 授权后会重新调用
#ifdef Q_OS_WASM
    void runWasmRetinaFaceDetection(bool continuousMode = false); // WASM RetinaFace 人脸框标记
#endif

    // 由 uic 从 mainwindow.ui 自动生成的控件容器
    Ui::MainWindow *ui;

    // 语音识别客户端 (非 UI 组件, 仍由代码持有)
    WhisperClient *whisper_client_;

#ifdef HAS_TEXTTOSPEECH
    // 语音合成 (TTS) - 成员变量保证生命周期足够播放完成
    QTextToSpeech *tts_;
#endif

    // 状态标记
    bool is_mic_active_;
    bool is_video_recording_;
    bool is_call_active_;   // 通话状态 (是否处于活动会话中)
    bool keep_camera_combo_visible_; // WASM 打开摄像头后保持设备列表可见，便于用户切换
#ifdef Q_OS_WASM
    bool wasm_face_detection_busy_;
    bool wasm_realtime_face_detection_enabled_;
    bool wasm_face_marking_enabled_; // 仅 Shot/Zoom 流程允许写入人脸标记，Cam 只打开摄像头
    QTimer *wasm_face_detection_timer_;
#endif

    // 时间刷新定时器
    QTimer *clock_timer_;
};

#endif // MAINWINDOW_H
