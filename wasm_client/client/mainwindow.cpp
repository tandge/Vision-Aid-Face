/**
 * @file mainwindow.cpp
 * @brief Main window implementation
 *
 * UI 由 mainwindow.ui 描述, uic 自动生成 ui_mainwindow.h, 本文件仅
 * 处理业务逻辑 (按钮槽函数 / 状态切换 / 摄像头与语音识别交互)。
 *
 * 视觉规范参考 gui_spec.txt / docs/gui.html:
 * - 375 x 680 画布, 24px 圆角
 * - 线性渐变背景 #F0F0F0 -> #E8E8E8
 * - 底部 4 个圆形按钮 (麦克风 / 发送 / 视频 / 结束通话)
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "camerawidget.h"
#include "pulsedots.h"
#include "whisperclient.h"
#include <QListWidgetItem>

#ifndef Q_OS_WASM
#include "facemanagerdialog.h"
#include "facedetector.h"
#include "facealigner.h"
#include "facerecognizer.h"
#include "facedatabase.h"
#ifdef HAS_TEXTTOSPEECH
#include <QTextToSpeech>
#endif
#include <opencv2/opencv.hpp>
#endif

#include <QGraphicsDropShadowEffect>
#include <QDebug>
#include <QCoreApplication>
#include <QFile>
#include <QMessageBox>
#include <QCameraDevice>
#include <QMediaDevices>
#include <QByteArray>
#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QAbstractItemView>
#ifdef Q_OS_WASM
#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QStringList>
#endif
#include <memory>
#ifdef Q_OS_WASM
#include <cstdlib>
#include <cstring>
#include <string>
#endif

#ifdef Q_OS_WASM
#include <emscripten/emscripten.h>
#endif

#ifndef Q_OS_WASM
namespace {
    std::unique_ptr<FaceDetector>   g_detector;
    std::unique_ptr<FaceAligner>    g_aligner;
    std::unique_ptr<FaceRecognizer> g_recognizer;
    std::unique_ptr<FaceDatabase>   g_db;
}
#else
namespace {
char *duplicateCString(const std::string &text)
{
    char *result = static_cast<char *>(std::malloc(text.size() + 1));
    if (!result) {
        return nullptr;
    }
    std::memcpy(result, text.c_str(), text.size() + 1);
    return result;
}

void speakInBrowser(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    const char *message = utf8.constData();
    EM_ASM({
        const text = UTF8ToString($0);
        if (!('speechSynthesis' in window) || typeof SpeechSynthesisUtterance === 'undefined') {
            console.warn('Web Speech API is not available in this browser.');
            return;
        }

        window.speechSynthesis.cancel();
        const utterance = new SpeechSynthesisUtterance(text);
        utterance.lang = 'zh-CN';
        utterance.rate = 1.0;
        utterance.pitch = 1.0;
        utterance.volume = 1.0;

        const selectVoiceAndSpeak = () => {
            const voices = window.speechSynthesis.getVoices ? window.speechSynthesis.getVoices() : [];
            const preferredVoice = voices.find(v => /zh|Chinese|中文/i.test(`${v.lang} ${v.name}`));
            if (preferredVoice) {
                utterance.voice = preferredVoice;
            }
            window.speechSynthesis.speak(utterance);
        };

        if (window.speechSynthesis.getVoices && window.speechSynthesis.getVoices().length === 0) {
            window.speechSynthesis.onvoiceschanged = selectVoiceAndSpeak;
            setTimeout(selectVoiceAndSpeak, 200);
        } else {
            selectVoiceAndSpeak();
        }
    }, message);
}

EM_ASYNC_JS(char *, wasmEnumerateVideoDevices, (), {
    function makeCString(text) {
        const length = lengthBytesUTF8(text) + 1;
        const ptr = _malloc(length);
        stringToUTF8(text, ptr, length);
        return ptr;
    }

    async function enumerateCameras() {
        const devices = await navigator.mediaDevices.enumerateDevices();
        console.log('All media devices:', devices);
        const videoDevices = devices.filter(device => device.kind === 'videoinput');
        console.log('Video devices found:', videoDevices);
        return videoDevices;
    }

    try {
        if (!navigator.mediaDevices || !navigator.mediaDevices.enumerateDevices) {
            console.error('enumerateDevices not available');
            return makeCString(JSON.stringify({ ok: false, error: 'enumerateDevices is not available' }));
        }

        // 强制获取权限，确保能获取设备标签和完整列表
        let cameraDevices = await enumerateCameras();
        const hasNamedCamera = cameraDevices.some(device => device.label && device.label.trim().length > 0);

        console.log('Initial camera count:', cameraDevices.length);
        console.log('Initial camera details:', cameraDevices);

        if ((!cameraDevices.length || !hasNamedCamera) && navigator.mediaDevices.getUserMedia) {
            const hasLiveStream = window.bnefCameraStream
                && window.bnefCameraStream.getVideoTracks().some(track => track.readyState === 'live');
            if (!hasLiveStream) {
                try {
                    console.log('Requesting camera permission to get full device info');
                    window.bnefCameraStream = await navigator.mediaDevices.getUserMedia({ video: true, audio: false });
                    console.log('Camera permission granted');
                } catch (permissionError) {
                    console.warn('Permission request failed:', permissionError);
                }
            }
            cameraDevices = await enumerateCameras();
            console.log('Camera count after permission:', cameraDevices.length);
            console.log('Camera details after permission:', cameraDevices);
        }

        const cameras = cameraDevices.map((device, index) => ({
            label: device.label && device.label.trim() !== '' ? device.label : `摄像头 ${index + 1}`,
            deviceId: device.deviceId || `device_${index}`,
            groupId: device.groupId || `group_${index}`
        }));

        console.log('Final camera list to return:', cameras);

        return makeCString(JSON.stringify({ ok: true, devices: cameras }));
    } catch (error) {
        console.warn('Failed to enumerate camera devices:', error);
        return makeCString(JSON.stringify({
            ok: false,
            error: String(error && error.message ? error.message : error)
        }));
    }
});

EM_ASYNC_JS(char *, wasmRunRetinaFaceOnPng, (const char *pngDataUrlPtr), {
    const pngDataUrl = UTF8ToString(pngDataUrlPtr);
    const modelUrl = 'models/retinaface.onnx';
    const absoluteModelUrl = 'https://bynbj.com/tools/eye_friend/models/retinaface.onnx';
    const modelPath = '/models/retinaface.onnx';
    const ortScriptUrl = 'onnxruntime/ort.min.js';
    const inputSize = 640;
    const confThreshold = 0.5;
    const nmsThreshold = 0.4;

    function makeCString(text) {
        const length = lengthBytesUTF8(text) + 1;
        const ptr = _malloc(length);
        stringToUTF8(text, ptr, length);
        return ptr;
    }

    function ok(boxes) {
        return JSON.stringify({ ok: true, boxes });
    }

    function fail(message) {
        return JSON.stringify({ ok: false, error: String(message || 'unknown error') });
    }

    function loadScript(url) {
        return new Promise((resolve, reject) => {
            const existing = document.querySelector(`script[data-bnef-src="${url}"]`);
            if (existing) {
                if (existing.dataset.loaded === '1') resolve();
                else existing.addEventListener('load', resolve, { once: true });
                return;
            }
            const script = document.createElement('script');
            script.src = url;
            script.async = true;
            script.dataset.bnefSrc = url;
            script.onload = () => { script.dataset.loaded = '1'; resolve(); };
            script.onerror = () => reject(new Error(`failed to load ${url}`));
            document.head.appendChild(script);
        });
    }

    async function syncFs(populate) {
        return new Promise((resolve, reject) => {
            FS.syncfs(populate, err => err ? reject(err) : resolve());
        });
    }

    async function ensureModel() {
        try { FS.mkdir('/models'); } catch (e) {}
        try { await syncFs(true); } catch (e) { console.warn('IDBFS populate failed:', e); }
        try {
            const stat = FS.stat(modelPath);
            if (stat && stat.size > 1024) return modelPath;
        } catch (e) {}

        let response = await fetch(modelUrl, { credentials: 'same-origin' });
        if (!response.ok) {
            response = await fetch(absoluteModelUrl, { mode: 'cors' });
        }
        if (!response.ok) {
            throw new Error(`failed to download retinaface.onnx: HTTP ${response.status}`);
        }
        const bytes = new Uint8Array(await response.arrayBuffer());
        FS.writeFile(modelPath, bytes);
        try { await syncFs(false); } catch (e) { console.warn('IDBFS flush failed:', e); }
        return modelPath;
    }

    async function decodeImage() {
        const image = new Image();
        image.decoding = 'async';
        await new Promise((resolve, reject) => {
            image.onload = resolve;
            image.onerror = () => reject(new Error('failed to decode image'));
            image.src = pngDataUrl;
        });
        const canvas = document.createElement('canvas');
        canvas.width = inputSize;
        canvas.height = inputSize;
        const ctx = canvas.getContext('2d', { willReadFrequently: true });
        ctx.drawImage(image, 0, 0, inputSize, inputSize);
        const data = ctx.getImageData(0, 0, inputSize, inputSize).data;
        const input = new Float32Array(3 * inputSize * inputSize);
        const plane = inputSize * inputSize;
        for (let y = 0; y < inputSize; ++y) {
            for (let x = 0; x < inputSize; ++x) {
                const src = (y * inputSize + x) * 4;
                const dst = y * inputSize + x;
                input[dst] = data[src];
                input[plane + dst] = data[src + 1];
                input[plane * 2 + dst] = data[src + 2];
            }
        }
        return { image, input };
    }

    function generatePriors() {
        const minSizes = [[16, 32], [64, 128], [256, 512]];
        const steps = [8, 16, 32];
        const priors = [];
        for (let k = 0; k < steps.length; ++k) {
            const f = Math.floor(inputSize / steps[k]);
            for (let i = 0; i < f; ++i) {
                for (let j = 0; j < f; ++j) {
                    for (const minSize of minSizes[k]) {
                        priors.push([(j + 0.5) * steps[k] / inputSize,
                                     (i + 0.5) * steps[k] / inputSize,
                                     minSize / inputSize,
                                     minSize / inputSize]);
                    }
                }
            }
        }
        return priors;
    }

    function iou(a, b) {
        const x1 = Math.max(a.x1, b.x1);
        const y1 = Math.max(a.y1, b.y1);
        const x2 = Math.min(a.x2, b.x2);
        const y2 = Math.min(a.y2, b.y2);
        const w = Math.max(0, x2 - x1);
        const h = Math.max(0, y2 - y1);
        const inter = w * h;
        const areaA = Math.max(0, a.x2 - a.x1) * Math.max(0, a.y2 - a.y1);
        const areaB = Math.max(0, b.x2 - b.x1) * Math.max(0, b.y2 - b.y1);
        return inter / Math.max(1e-6, areaA + areaB - inter);
    }

    function nms(boxes) {
        boxes.sort((a, b) => b.score - a.score);
        const picked = [];
        for (const box of boxes) {
            if (picked.every(existing => iou(box, existing) <= nmsThreshold)) {
                picked.push(box);
            }
        }
        return picked;
    }

    function tensorData(output, names, index) {
        if (names[index] && output[names[index]]) return output[names[index]].data;
        const values = Object.values(output);
        return values[index] ? values[index].data : null;
    }

    function decodeOutputs(output) {
        const names = Object.keys(output);
        const out0 = tensorData(output, names, 0);
        const out1 = tensorData(output, names, 1);
        const priors = generatePriors();
        const boxes = [];

        if (names.length >= 3 && out0 && out1) {
            const bbox = out0;
            const conf = out1;
            const count = Math.min(priors.length, Math.floor(bbox.length / 4), Math.floor(conf.length / 2));
            for (let i = 0; i < count; ++i) {
                const score = conf[i * 2 + 1];
                if (score < confThreshold) continue;
                const [cx, cy, w, h] = priors[i];
                const dx = bbox[i * 4 + 0];
                const dy = bbox[i * 4 + 1];
                const dw = bbox[i * 4 + 2];
                const dh = bbox[i * 4 + 3];
                const predCx = cx + dx * 0.1 * w;
                const predCy = cy + dy * 0.1 * h;
                const predW = w * Math.exp(dw * 0.2);
                const predH = h * Math.exp(dh * 0.2);
                boxes.push({
                    x1: Math.max(0, predCx - predW * 0.5),
                    y1: Math.max(0, predCy - predH * 0.5),
                    x2: Math.min(1, predCx + predW * 0.5),
                    y2: Math.min(1, predCy + predH * 0.5),
                    score
                });
            }
        } else {
            const mergedTensor = Object.values(output)[0];
            const data = mergedTensor && mergedTensor.data;
            const dims = mergedTensor && mergedTensor.dims;
            const cols = dims && dims.length >= 3 ? dims[2] : 15;
            const count = data ? Math.min(priors.length, Math.floor(data.length / cols)) : 0;
            for (let i = 0; i < count; ++i) {
                const score = data[i * cols + 4];
                if (score < confThreshold) continue;
                const [cx, cy, w, h] = priors[i];
                const dx = data[i * cols + 0];
                const dy = data[i * cols + 1];
                const dw = data[i * cols + 2];
                const dh = data[i * cols + 3];
                const predCx = cx + dx * 0.1 * w;
                const predCy = cy + dy * 0.1 * h;
                const predW = w * Math.exp(dw * 0.2);
                const predH = h * Math.exp(dh * 0.2);
                boxes.push({
                    x1: Math.max(0, predCx - predW * 0.5),
                    y1: Math.max(0, predCy - predH * 0.5),
                    x2: Math.min(1, predCx + predW * 0.5),
                    y2: Math.min(1, predCy + predH * 0.5),
                    score
                });
            }
        }
        return nms(boxes).map(b => ({ x: b.x1, y: b.y1, w: b.x2 - b.x1, h: b.y2 - b.y1, score: b.score }));
    }

    try {
        if (!FS.analyzePath('/models').exists) {
            FS.mkdir('/models');
        }
        if (!FS.filesystems.IDBFS) {
            console.warn('IDBFS is not available; model cache will be memory-only.');
        } else if (!FS.analyzePath('/models').object.mounted) {
            try { FS.mount(IDBFS, {}, '/models'); } catch (e) {}
        }

        await loadScript(ortScriptUrl);
        if (!self.ort) throw new Error('onnxruntime-web did not initialize');
        ort.env.wasm.wasmPaths = 'onnxruntime/';
        ort.env.wasm.numThreads = 1;
        const path = await ensureModel();
        const modelBytes = FS.readFile(path);
        const { input } = await decodeImage();
        const session = window.bnefRetinaFaceSession || await ort.InferenceSession.create(modelBytes, { executionProviders: ['wasm'] });
        window.bnefRetinaFaceSession = session;
        const inputName = session.inputNames[0];
        const feeds = {};
        feeds[inputName] = new ort.Tensor('float32', input, [1, 3, inputSize, inputSize]);
        const output = await session.run(feeds);
        return makeCString(ok(decodeOutputs(output)));
    } catch (error) {
        console.error('RetinaFace WASM detection failed:', error);
        return makeCString(fail(error && error.message ? error.message : error));
    }
});
}
#endif


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , whisper_client_(nullptr)
    , is_mic_active_(false)
    , is_video_recording_(false)
    , is_call_active_(true)
    , keep_camera_combo_visible_(false)
#ifdef Q_OS_WASM
    , wasm_face_detection_busy_(false)
    , wasm_realtime_face_detection_enabled_(false)
    , wasm_face_marking_enabled_(false)
    , wasm_face_detection_timer_(nullptr)
#endif
    , clock_timer_(nullptr)
{
    // 由 uic 生成的 setupUi() 一次性构造全部子控件 / 布局 / 样式
    ui->setupUi(this);

    // 视觉装饰: 窗口阴影 + 时间文字阴影 (这类 QGraphicsEffect 不便在 .ui 内描述)
    auto *window_shadow = new QGraphicsDropShadowEffect(this);
    window_shadow->setBlurRadius(40);
    window_shadow->setOffset(0, 8);
    window_shadow->setColor(QColor(0, 0, 0, 60));
    setGraphicsEffect(window_shadow);

    auto *time_shadow = new QGraphicsDropShadowEffect(ui->timeLabel);
    time_shadow->setBlurRadius(4);
    time_shadow->setOffset(0, 1);
    time_shadow->setColor(QColor(0, 0, 0, 80));
    ui->timeLabel->setGraphicsEffect(time_shadow);

    auto *hint_shadow = new QGraphicsDropShadowEffect(ui->voiceHintLabel);
    hint_shadow->setBlurRadius(6);
    hint_shadow->setOffset(0, 2);
    hint_shadow->setColor(QColor(0, 0, 0, 100));
    ui->voiceHintLabel->setGraphicsEffect(hint_shadow);

    // 语音识别后端
    whisper_client_ = new WhisperClient(this);
    connect(whisper_client_, &WhisperClient::transcriptionReady,
            this, &MainWindow::onTranscriptionReceived);
    connect(whisper_client_, &WhisperClient::errorOccurred, this,
            [](const QString &error) { qWarning() << "Whisper error:" << error; });

#ifdef HAS_TEXTTOSPEECH
    // 语音合成后端 (成员变量, 避免局部对象被提前销毁)
    tts_ = new QTextToSpeech(this);
#endif

    // 实时视频流帧捕获 -> 人脸识别
#ifndef Q_OS_WASM
    if (ui->cameraWidget) {
        connect(ui->cameraWidget, &CameraWidget::frameCaptured,
                this, &MainWindow::onFrameCapturedForFace);
    }
#endif

    // 底部 4 个按钮的信号连接 (按钮本身的样式 / 文字 / checkable 等已在 .ui 中描述)
    connect(ui->micButton,     &QPushButton::toggled, this, &MainWindow::onMicrophoneToggled);
    connect(ui->sendButton,    &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(ui->videoButton,   &QPushButton::toggled, this, &MainWindow::onVideoToggled);
    connect(ui->endCallButton, &QPushButton::clicked, this, &MainWindow::onEndCallClicked);

#ifdef Q_OS_WASM
    const int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/NotoSansSC-VF.ttf"));
    QString wasmFontFamily = QStringLiteral("sans-serif");
    if (fontId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty()) {
            wasmFontFamily = families.first();
            QFont wasmFont(wasmFontFamily);
            wasmFont.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::PreferMatch));
            qApp->setFont(wasmFont);
            setFont(wasmFont);
        }
    } else {
        qWarning() << "Failed to load bundled Chinese font in MainWindow";
    }

    const QString wasmFontCss = QStringLiteral("font-family: '%1', sans-serif;").arg(wasmFontFamily);
    setStyleSheet(styleSheet() + QStringLiteral("\n"
        "QWidget, QLabel, QComboBox, QComboBox QAbstractItemView, QToolTip { %1 }\n"
        "QToolTip { color: #FFFFFF; background-color: #2C2C2C; border: 1px solid rgba(255,255,255,0.35); padding: 6px; }\n")
        .arg(wasmFontCss));

    for (QWidget *widget : findChildren<QWidget *>()) {
        QFont widgetFont(wasmFontFamily);
        widgetFont.setStyleStrategy(static_cast<QFont::StyleStrategy>(QFont::PreferAntialias | QFont::PreferMatch));
        widget->setFont(widgetFont);
    }

    if (ui->micButton) {
        ui->micButton->setText(QStringLiteral("Mic"));
        ui->micButton->setToolTip(QStringLiteral("麦克风 - 开启/关闭语音输入"));
    }
    if (ui->sendButton) {
        ui->sendButton->setText(QStringLiteral("Cam"));
        ui->sendButton->setToolTip(QStringLiteral("打开摄像头"));
    }
    if (ui->videoButton) {
        ui->videoButton->setText(QStringLiteral("Shot"));
        ui->videoButton->setToolTip(QStringLiteral("截取当前画面作为背景"));
    }
    if (ui->endCallButton) {
        ui->endCallButton->setText(QStringLiteral("Zoom"));
        ui->endCallButton->setToolTip(QStringLiteral("循环全屏放大已标记的人脸"));
    }

    wasm_face_detection_timer_ = new QTimer(this);
    wasm_face_detection_timer_->setInterval(1000);
    connect(wasm_face_detection_timer_, &QTimer::timeout, this, [this]() {
        if (wasm_realtime_face_detection_enabled_ && wasm_face_marking_enabled_) {
            runWasmRetinaFaceDetection(true);
        }
    });
#endif

    // 枚举摄像头设备并填充顶部下拉框。WASM 授权前通常只能拿到空列表，点击按钮授权后会再次刷新。
    refreshCameraDevices();
    auto *mediaDevices = new QMediaDevices(this);
    connect(mediaDevices, &QMediaDevices::videoInputsChanged,
            this, &MainWindow::refreshCameraDevices);

    // 摄像头选择事件
    if (ui->cameraRadioButton1 && ui->cameraWidget) {
        connect(ui->cameraRadioButton1, &QRadioButton::clicked, [this]() {
            ui->cameraWidget->setCameraDevice(0);
        });
    }
    if (ui->cameraRadioButton2 && ui->cameraWidget) {
        connect(ui->cameraRadioButton2, &QRadioButton::clicked, [this]() {
            ui->cameraWidget->setCameraDevice(1);
        });
    }

    // 状态栏时间刷新
    clock_timer_ = new QTimer(this);
    connect(clock_timer_, &QTimer::timeout, this, &MainWindow::updateStatusBarTime);
    clock_timer_->start(1000);
    updateStatusBarTime();
}

MainWindow::~MainWindow()
{
    if (whisper_client_) {
        whisper_client_->stopRecognition();
    }
    delete ui;
}

void MainWindow::refreshCameraDevices()
{
    if (!ui->cameraGroupBox) {
        return;
    }

    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();

    qDebug() << "=== refreshCameraDevices called ===";
    qDebug() << "QMediaDevices::videoInputs() count:" << cameras.size();

    for (int i = 0; i < cameras.size(); ++i) {
        const QCameraDevice &device = cameras[i];
        qDebug() << "\nCamera" << i << ":";
        qDebug() << "  Description:" << device.description();
        qDebug() << "  ID:" << device.id();
        qDebug() << "  Position:" << (device.position() == QCameraDevice::FrontFace ? "Front" :
                                      device.position() == QCameraDevice::BackFace ? "Back" : "Other");
    }

    // 直接显示到 UI 上进行调试
    QString debugText = QString("Found %1 cameras via QMediaDevices::videoInputs()").arg(cameras.size());
    qDebug() << debugText;

#ifdef Q_OS_WASM
    char *devicesJsonPtr = wasmEnumerateVideoDevices();
    const QString devicesJson = devicesJsonPtr ? QString::fromUtf8(devicesJsonPtr) : QString();
    if (devicesJsonPtr) {
        std::free(devicesJsonPtr);
    }

    qDebug() << "WASM devices JSON:" << devicesJson;

    const QJsonDocument devicesDoc = QJsonDocument::fromJson(devicesJson.toUtf8());

    qDebug() << "=== Full JSON response ===";
    qDebug() << devicesJson;

    int addedCount = 0;

    // 更新 Radio button 控件组
    QMap<int, QString> cameraLabels;

    if (devicesDoc.isObject() && devicesDoc.object().value(QStringLiteral("ok")).toBool(false)) {
        const QJsonArray devices = devicesDoc.object().value(QStringLiteral("devices")).toArray();
        qDebug() << "=== Browser API devices ===";
        qDebug() << "Count:" << devices.size();

        for (int i = 0; i < devices.size(); ++i) {
            const QJsonObject device = devices[i].toObject();
            qDebug() << "Device" << i << ":" << device;

            QString label = device.value(QStringLiteral("label")).toString().trimmed();
            if (label.isEmpty()) {
                label = QStringLiteral("摄像头 %1").arg(i + 1);
            }

            cameraLabels[i] = label;
            addedCount++;
        }
    }

    // 同时显示 Qt API 找到的摄像头，防止遗漏
    if (cameras.size() > 0) {
        qDebug() << "=== Qt API devices ===";
        qDebug() << "Count:" << cameras.size();

        for (int i = 0; i < cameras.size(); ++i) {
            const QCameraDevice &device = cameras[i];
            qDebug() << "Device" << i << ":" << device.description() <<
                        "Position:" << (device.position() == QCameraDevice::FrontFace ? "Front" :
                                        device.position() == QCameraDevice::BackFace ? "Back" : "Other");

            QString label = device.description().trimmed();
            if (label.isEmpty()) {
                label = QStringLiteral("摄像头 %1").arg(addedCount + i + 1);
            }
            if (device.position() == QCameraDevice::FrontFace)
                label += QStringLiteral(" [前置]");
            else if (device.position() == QCameraDevice::BackFace)
                label += QStringLiteral(" [后置]");

            cameraLabels[addedCount + i] = label;
        }
    }

    qDebug() << "=== Total devices added ===";
    qDebug() << "Added count:" << cameraLabels.size();

    // 更新 Radio button 文字
    if (ui->cameraRadioButton1) {
        if (cameraLabels.contains(0)) {
            ui->cameraRadioButton1->setText(cameraLabels[0]);
            ui->cameraRadioButton1->setVisible(true);
        } else {
            ui->cameraRadioButton1->setVisible(false);
        }
    }

    if (ui->cameraRadioButton2) {
        if (cameraLabels.contains(1)) {
            ui->cameraRadioButton2->setText(cameraLabels[1]);
            ui->cameraRadioButton2->setVisible(true);
        } else {
            ui->cameraRadioButton2->setVisible(false);
        }
    }
#else
    // 非 WASM 版本，确保显示所有摄像头
    qDebug() << "Displaying" << cameras.size() << "cameras in non-WASM version";
    for (int i = 0; i < cameras.size(); ++i) {
        const QCameraDevice &device = cameras[i];
        QString label = device.description().trimmed();
        if (label.isEmpty()) {
            label = QStringLiteral("摄像头 %1").arg(i + 1);
        }
        if (device.position() == QCameraDevice::FrontFace)
            label += QStringLiteral(" [前置]");
        else if (device.position() == QCameraDevice::BackFace)
            label += QStringLiteral(" [后置]");
        auto *item = new QListWidgetItem(label);
        item->setTextAlignment(Qt::AlignHCenter);
        ui->cameraListWidget->addItem(item);
    }
#endif

    // 更新当前选中的 radio button
    int currentIndex = 0;
    if (ui->cameraWidget) {
        currentIndex = ui->cameraWidget->currentCameraIndex();
    }

    if (currentIndex == 0) {
        ui->cameraRadioButton1->setChecked(true);
    } else if (currentIndex == 1) {
        ui->cameraRadioButton2->setChecked(true);
    }
}

#ifdef Q_OS_WASM
void MainWindow::runWasmRetinaFaceDetection(bool continuousMode)
{
    if (wasm_face_detection_busy_) {
        if (!continuousMode) {
            QTimer::singleShot(300, this, [this]() {
                runWasmRetinaFaceDetection(false);
            });
        }
        return;
    }
    if (!ui->cameraWidget || !wasm_face_marking_enabled_) {
        return;
    }

    const QImage frame = ui->cameraWidget->currentDisplayFrame();
    if (frame.isNull()) {
        if (!continuousMode) {
            ui->voiceHintLabel->setText(QStringLiteral("请先点击 Cam 打开摄像头，或点击 Shot 固定画面"));
            ui->voiceHintLabel->show();
        }
        return;
    }

    QByteArray pngBytes;
    QBuffer buffer(&pngBytes);
    buffer.open(QIODevice::WriteOnly);
    frame.convertToFormat(QImage::Format_RGBA8888).save(&buffer, "PNG");
    const QString dataUrl = QStringLiteral("data:image/png;base64,%1")
        .arg(QString::fromLatin1(pngBytes.toBase64()));
    const QByteArray dataUrlUtf8 = dataUrl.toUtf8();

    wasm_face_detection_busy_ = true;
    if (!continuousMode) {
        ui->endCallButton->setEnabled(false);
        ui->voiceHintLabel->setText(QStringLiteral("正在加载 RetinaFace 并检测人脸..."));
        ui->voiceHintLabel->show();
    }

    char *jsonPtr = wasmRunRetinaFaceOnPng(dataUrlUtf8.constData());
    const QString json = jsonPtr ? QString::fromUtf8(jsonPtr) : QString();
    if (jsonPtr) {
        std::free(jsonPtr);
    }

    QVector<QRectF> boxes;
    QString errorMessage;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        errorMessage = QStringLiteral("RetinaFace 返回结果无效");
    } else {
        const QJsonObject root = doc.object();
        if (!root.value(QStringLiteral("ok")).toBool(false)) {
            errorMessage = root.value(QStringLiteral("error")).toString(QStringLiteral("RetinaFace 检测失败"));
        } else {
            const QJsonArray array = root.value(QStringLiteral("boxes")).toArray();
            for (const QJsonValue &value : array) {
                const QJsonObject box = value.toObject();
                boxes.append(QRectF(box.value(QStringLiteral("x")).toDouble(),
                                    box.value(QStringLiteral("y")).toDouble(),
                                    box.value(QStringLiteral("w")).toDouble(),
                                    box.value(QStringLiteral("h")).toDouble()));
            }
        }
    }

    if ((continuousMode && !wasm_realtime_face_detection_enabled_) || !wasm_face_marking_enabled_) {
        wasm_face_detection_busy_ = false;
        return;
    }

    if (errorMessage.isEmpty()) {
        ui->cameraWidget->setFaceBoxes(boxes);
        if (!continuousMode) {
            ui->voiceHintLabel->setText(boxes.isEmpty()
                ? QStringLiteral("未检测到人脸")
                : QStringLiteral("已标记 %1 张人脸").arg(boxes.size()));
            ui->voiceHintLabel->show();
        } else if (ui->cameraWidget->isCameraActive()) {
            ui->voiceHintLabel->hide();
        }
    } else {
        if (!continuousMode) {
            ui->cameraWidget->clearFaceBoxes();
            ui->voiceHintLabel->setText(QStringLiteral("检测失败: %1").arg(errorMessage));
            ui->voiceHintLabel->show();
        } else {
            qWarning() << "Realtime RetinaFace detection failed:" << errorMessage;
        }
    }
    if (!continuousMode) {
        ui->endCallButton->setEnabled(true);
    }
    wasm_face_detection_busy_ = false;
}
#endif

// ============================================================
// 槽函数 - 与 docs/gui.html 底部四个按钮一一对应
// ============================================================

void MainWindow::onMicrophoneToggled(bool checked)
{
    is_mic_active_ = checked;
    // 按钮 1 (麦克风) 的激活态视觉切换已由 .ui 中的 QSS :checked 选择器自动完成

    if (checked) {
#ifdef HAS_TEXTTOSPEECH
        if (tts_) {
            tts_->say(QStringLiteral("打开麦克风"));
        }
#endif
#ifdef Q_OS_WASM
        speakInBrowser(QStringLiteral("打开麦克风"));
#endif
        ui->pulseDots->startAnimation();
        ui->voiceHintLabel->setText(QStringLiteral("正在倾听..."));
        ui->transcriptionLabel->show();
        whisper_client_->startRecognition();
    } else {
        ui->pulseDots->stopAnimation();
        ui->voiceHintLabel->setText(QStringLiteral("你可以开始说话"));
        whisper_client_->stopRecognition();

        QTimer::singleShot(2000, this, [this]() {
            if (!is_mic_active_) {
                ui->transcriptionLabel->hide();
                ui->transcriptionLabel->clear();
            }
        });
    }
}

void MainWindow::onSendClicked()
{
#ifndef Q_OS_WASM
    QString appDir = QCoreApplication::applicationDirPath();
    QString retinaPath = appDir + QStringLiteral("/models/retinaface.onnx");
    QString arcfacePath  = appDir + QStringLiteral("/models/arcface_r50.onnx");
    QString dbPath       = appDir + QStringLiteral("/face.db");

    if (!QFile::exists(retinaPath)) {
        QMessageBox::warning(this, QStringLiteral("缺少模型"),
            QStringLiteral("未找到检测模型:\n%1\n请将 retinaface.onnx 放到 exe 同级的 models/ 目录下。").arg(retinaPath));
        return;
    }
    if (!QFile::exists(arcfacePath)) {
        QMessageBox::warning(this, QStringLiteral("缺少模型"),
            QStringLiteral("未找到识别模型:\n%1\n请将 arcface_r50.onnx 放到 exe 同级的 models/ 目录下。").arg(arcfacePath));
        return;
    }

    try {
        int camIdx = ui->cameraWidget ? ui->cameraWidget->currentCameraIndex() : 0;
        FaceManagerDialog dlg(retinaPath, arcfacePath, dbPath, camIdx, this);
        dlg.exec();

        // 人脸管理窗口也占用了摄像头，关闭后重启主窗口摄像头恢复实时画面
        if (ui->cameraWidget) {
            ui->cameraWidget->restartCamera();
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, QStringLiteral("人脸管理启动失败"),
            QStringLiteral("加载模型或初始化失败:\n%1").arg(QString::fromLocal8Bit(e.what())));
    }
#else
    if (!ui->cameraWidget) {
        return;
    }

    // WebAssembly 下必须由用户点击触发摄像头授权；授权成功后 Qt 会开始向 CameraWidget 推送实时帧。
    keep_camera_combo_visible_ = true;
    wasm_face_marking_enabled_ = false;
    wasm_realtime_face_detection_enabled_ = false;
    wasm_face_detection_busy_ = false;
    if (wasm_face_detection_timer_) {
        wasm_face_detection_timer_->stop();
    }
    ui->cameraWidget->clearFaceBoxes();
    ui->cameraWidget->startCamera();
    ui->cameraWidget->clearFaceBoxes();
    ui->pulseDots->hide();
    ui->voiceHintLabel->setText(QStringLiteral("正在打开摄像头..."));
    ui->transcriptionLabel->hide();

    // 浏览器授权与设备标签更新是异步的，启动后刷新一次，并在下一轮事件循环再次刷新。
    refreshCameraDevices();
    QTimer::singleShot(1000, this, [this]() {
        refreshCameraDevices();
        if (ui->cameraWidget && ui->cameraWidget->isCameraActive()) {
            ui->cameraWidget->clearFaceBoxes();
            ui->voiceHintLabel->hide();
        } else {
            ui->voiceHintLabel->setText(QStringLiteral("请允许浏览器访问摄像头"));
            ui->voiceHintLabel->show();
        }
    });
    QTimer::singleShot(2500, this, [this]() {
        refreshCameraDevices();
        if (ui->cameraWidget && !ui->cameraWidget->hasFrame()) {
            ui->cameraWidget->restartCamera();
        }
    });
    QTimer::singleShot(4000, this, [this]() {
        refreshCameraDevices();
    });
#endif
}

void MainWindow::onVideoToggled(bool checked)
{
#ifndef Q_OS_WASM
    if (!checked) return;

    // 单次触发：立即恢复按钮状态，避免录制逻辑
    ui->videoButton->setChecked(false);

    QString appDir = QCoreApplication::applicationDirPath();
    QString retinaPath = appDir + QStringLiteral("/models/retinaface.onnx");
    QString arcfacePath = appDir + QStringLiteral("/models/arcface_r50.onnx");

    if (!QFile::exists(retinaPath) || !QFile::exists(arcfacePath)) {
        QMessageBox::warning(this, QStringLiteral("缺少模型"),
            QStringLiteral("请确保 models 目录存在"));
        return;
    }

    // 延迟初始化模型（只执行一次）
    if (!g_detector) {
        try {
            g_detector   = std::make_unique<FaceDetector>(retinaPath.toStdString());
            g_aligner    = std::make_unique<FaceAligner>();
            g_recognizer = std::make_unique<FaceRecognizer>(arcfacePath.toStdString());
            g_db         = std::make_unique<FaceDatabase>(appDir + QStringLiteral("/face.db"));
        } catch (const std::exception& e) {
            QMessageBox::critical(this, QStringLiteral("错误"),
                QStringLiteral("模型加载失败: %1").arg(QString::fromLocal8Bit(e.what())));
            return;
        }
    }

    // 向 CameraWidget 请求实时流的一帧（异步，结果通过 frameCaptured 信号返回）
    if (ui->cameraWidget) {
        ui->cameraWidget->requestFrame();
    }
#else
    Q_UNUSED(checked)
    if (!ui->cameraWidget) {
        return;
    }

    if (!ui->cameraWidget->isCameraActive()) {
        ui->cameraWidget->startCamera();
        ui->voiceHintLabel->setText(QStringLiteral("正在打开摄像头..."));
        ui->voiceHintLabel->show();
    }

    if (ui->cameraWidget->captureCurrentFrameAsBackground()) {
        wasm_realtime_face_detection_enabled_ = false;
        wasm_face_marking_enabled_ = true;
        if (wasm_face_detection_timer_) {
            wasm_face_detection_timer_->stop();
        }
        ui->voiceHintLabel->setText(QStringLiteral("已截取当前画面，正在标记人脸..."));
        ui->voiceHintLabel->show();
        runWasmRetinaFaceDetection(false);
    } else {
        ui->voiceHintLabel->setText(QStringLiteral("摄像头画面准备中，请稍后再点一次"));
        ui->voiceHintLabel->show();
    }

    ui->videoButton->blockSignals(true);
    ui->videoButton->setChecked(false);
    ui->videoButton->blockSignals(false);
#endif
}

void MainWindow::onEndCallClicked()
{
#ifdef Q_OS_WASM
    wasm_realtime_face_detection_enabled_ = false;
    if (wasm_face_detection_timer_) {
        wasm_face_detection_timer_->stop();
    }

    if (ui->cameraWidget && ui->cameraWidget->showNextMarkedFaceFullscreen()) {
        ui->voiceHintLabel->hide();
    } else {
        ui->voiceHintLabel->setText(QStringLiteral("请先点击 Cam 或 Shot 标记人脸"));
        ui->voiceHintLabel->show();
    }
    return;
#else
    // 按钮 4: 红色 X - 结束通话, 停止全部任务并关闭窗口
    is_call_active_ = false;

    if (is_mic_active_) {
        whisper_client_->stopRecognition();
        ui->pulseDots->stopAnimation();
        is_mic_active_ = false;
        ui->micButton->blockSignals(true);
        ui->micButton->setChecked(false);
        ui->micButton->blockSignals(false);
    }
    if (is_video_recording_) {
        if (ui->cameraWidget) {
            ui->cameraWidget->stopRecording();
        }
        is_video_recording_ = false;
        ui->videoButton->blockSignals(true);
        ui->videoButton->setChecked(false);
        ui->videoButton->blockSignals(false);
        ui->videoButton->setText(QStringLiteral("🎥"));
    }

    ui->voiceHintLabel->setText(QStringLiteral("通话已结束"));
    QTimer::singleShot(400, this, &QWidget::close);
#endif
}

void MainWindow::updateStatusBarTime()
{
    ui->timeLabel->setText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm")));
}

void MainWindow::onTranscriptionReceived(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return;
    }
    ui->transcriptionLabel->setText(text);
    ui->transcriptionLabel->show();

    QTimer::singleShot(3000, this, [this, text]() {
        if (ui->transcriptionLabel->text() == text) {
            ui->transcriptionLabel->clear();
            if (!is_mic_active_) {
                ui->transcriptionLabel->hide();
            }
        }
    });
}

#ifndef Q_OS_WASM
void MainWindow::onFrameCapturedForFace(const QImage &frame)
{
    if (frame.isNull()) {
        qWarning() << "Captured frame is null";
        return;
    }

    // QImage -> cv::Mat (BGR)
    cv::Mat mat;
    switch (frame.format()) {
        case QImage::Format_RGB888:
            mat = cv::Mat(frame.height(), frame.width(), CV_8UC3,
                          const_cast<uchar*>(frame.bits()), frame.bytesPerLine());
            cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);
            break;
        case QImage::Format_ARGB32:
        case QImage::Format_ARGB32_Premultiplied:
        case QImage::Format_RGB32:
            mat = cv::Mat(frame.height(), frame.width(), CV_8UC4,
                          const_cast<uchar*>(frame.bits()), frame.bytesPerLine());
            cv::cvtColor(mat, mat, cv::COLOR_BGRA2BGR);
            break;
        default:
            qWarning() << "Unsupported QImage format:" << frame.format();
#ifdef HAS_TEXTTOSPEECH
            if (tts_) {
                tts_->say(QStringLiteral("图像格式不支持"));
            }
#endif
            return;
    }

    if (!g_detector || !g_aligner || !g_recognizer || !g_db) {
        qWarning() << "Face models not initialized";
        return;
    }

    // 检测人脸（可能多个）
    auto faces = g_detector->detect(mat);
    if (faces.empty()) {
#ifdef HAS_TEXTTOSPEECH
        if (tts_) {
            tts_->say(QStringLiteral("未检测到人脸"));
        }
#else
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("未检测到人脸"));
#endif
        return;
    }

    // 预加载数据库，避免循环内重复查询
    auto records = g_db->getAllFaceFeatures();
    QStringList recognizedNames;
    int unknownCount = 0;

    // 逐个识别（不播报，只收集结果）
    for (size_t i = 0; i < faces.size(); ++i) {
        cv::Mat aligned = g_aligner->align(mat, faces[i].landmarks);
        if (aligned.empty()) {
            ++unknownCount;
            continue;
        }

        std::vector<float> feature = g_recognizer->extract(aligned);

        // 余弦相似度比对
        float best_sim = -1.0f;
        QString best_name;
        for (const auto &r : records) {
            if (r.feature.size() != feature.size()) continue;
            float dot = 0, na = 0, nb = 0;
            for (size_t j = 0; j < feature.size(); ++j) {
                dot += feature[j] * r.feature[j];
                na += feature[j] * feature[j];
                nb += r.feature[j] * r.feature[j];
            }
            if (na > 0 && nb > 0) {
                float sim = dot / (std::sqrt(na) * std::sqrt(nb));
                if (sim > best_sim) {
                    best_sim = sim;
                    best_name = r.person_name;
                }
            }
        }

        if (best_sim >= 0.70f) {
            recognizedNames.append(best_name);
        } else {
            ++unknownCount;
        }
    }

    // 拼接成一句话，只播报一次
    QString finalMsg;
    int total = static_cast<int>(faces.size());
    if (!recognizedNames.isEmpty() && unknownCount == 0) {
        finalMsg = QStringLiteral("识别到 %1，共 %2 人")
                       .arg(recognizedNames.join("、"))
                       .arg(total);
    } else if (!recognizedNames.isEmpty() && unknownCount > 0) {
        finalMsg = QStringLiteral("识别到 %1，另有 %2 人未识别，共 %3 人")
                       .arg(recognizedNames.join("、"))
                       .arg(unknownCount)
                       .arg(total);
    } else {
        finalMsg = QStringLiteral("检测到 %1 人，未识别出已知人员").arg(total);
    }

#ifdef HAS_TEXTTOSPEECH
    if (tts_) {
        tts_->say(finalMsg);
    }
#else
    QMessageBox::information(this, QStringLiteral("识别结果"), finalMsg);
#endif
}
#endif
