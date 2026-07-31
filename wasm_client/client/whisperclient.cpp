/**
 * @file whisperclient.cpp
 * @brief whisper.cpp speech recognition client implementation
 *
 * In WASM environment, bridges browser Web Audio API to whisper.cpp via Emscripten.
 * Desktop environment uses Qt Multimedia for audio recording.
 */

#include "whisperclient.h"
#include <QDebug>
#include <QStandardPaths>
#include <QDir>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

WhisperClient::WhisperClient(QObject *parent)
    : QObject(parent)
    , is_recording_(false)
    , model_loaded_(false)
    , volume_threshold_(50)
    , capture_timer_(nullptr)
{
    model_path_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/models/ggml-base.bin";

    if (!QFile::exists(model_path_)) {
        model_path_ = "assets/models/ggml-base.bin";
    }

    initWhisperContext();

    capture_timer_ = new QTimer(this);
    capture_timer_->setInterval(500);

    connect(capture_timer_, &QTimer::timeout, this, [this]() {
#ifdef __EMSCRIPTEN__
        EM_ASM({
            if (typeof Module.audioBuffer !== 'undefined' && Module.audioBuffer.length > 0) {
                var bufferPtr = Module._malloc(Module.audioBuffer.length);
                Module.HEAPU8.set(Module.audioBuffer, bufferPtr);
                Module._whisperProcessAudio(bufferPtr, Module.audioBuffer.length);
                Module._free(bufferPtr);
                Module.audioBuffer = [];
            }
        });
#else
        // Desktop: placeholder for Qt Multimedia audio processing
#endif
    });
}

WhisperClient::~WhisperClient()
{
    stopRecognition();
}

bool WhisperClient::initWhisperContext()
{
    if (!QFile::exists(model_path_)) {
        qWarning() << "Whisper model not found at:" << model_path_;
        model_loaded_ = false;
        return false;
    }

#ifdef __EMSCRIPTEN__
    EM_ASM({
        Module.whisperReady = false;
        Module.audioBuffer = [];
        console.log('[WhisperClient] Initializing whisper context...');
        setTimeout(function() {
            Module.whisperReady = true;
        }, 1000);
    });
#endif

    model_loaded_ = true;
    qDebug() << "Whisper model loaded successfully";
    return true;
}

void WhisperClient::startRecognition()
{
    if (!model_loaded_) {
        emit errorOccurred("Whisper model not loaded");
        return;
    }

    if (is_recording_) {
        qWarning() << "Already recording";
        return;
    }

    is_recording_ = true;

#ifdef __EMSCRIPTEN__
    EM_ASM({
        if (typeof Module.startAudioCapture === 'undefined') {
            Module.startAudioCapture = function() {
                navigator.mediaDevices.getUserMedia({ audio: true })
                    .then(function(stream) {
                        var audioCtx = new (window.AudioContext || window.webkitAudioContext)({
                            sampleRate: 16000
                        });
                        var source = audioCtx.createMediaStreamSource(stream);
                        var processor = audioCtx.createScriptProcessor(4096, 1, 1);

                        source.connect(processor);
                        processor.connect(audioCtx.destination);

                        processor.onaudioprocess = function(e) {
                            var input = e.inputBuffer.getChannelData(0);
                            var buffer = new Int16Array(input.length);
                            for (var i = 0; i < input.length; i++) {
                                var s = Math.max(-1, Math.min(1, input[i]));
                                buffer[i] = s < 0 ? s * 0x8000 : s * 0x7FFF;
                            }
                            var bytes = new Uint8Array(buffer.buffer);
                            for (var i = 0; i < bytes.length; i++) {
                                Module.audioBuffer.push(bytes[i]);
                            }
                        };

                        Module.audioContext = audioCtx;
                        Module.audioStream = stream;
                        console.log('[WhisperClient] Audio capture started');
                    })
                    .catch(function(err) {
                        console.error('[WhisperClient] Audio capture error:', err);
                    });
            };
        }
        Module.startAudioCapture();
    });
#endif

    capture_timer_->start();
    qDebug() << "Whisper recognition started";
}

void WhisperClient::stopRecognition()
{
    is_recording_ = false;
    capture_timer_->stop();

#ifdef __EMSCRIPTEN__
    EM_ASM({
        if (Module.audioStream) {
            Module.audioStream.getTracks().forEach(function(track) {
                track.stop();
            });
            Module.audioStream = null;
        }
        if (Module.audioContext) {
            Module.audioContext.close();
            Module.audioContext = null;
        }
    });
#endif

    qDebug() << "Whisper recognition stopped";
}

void WhisperClient::setVolume(int volume)
{
    volume_threshold_ = qBound(0, volume, 100);
}

bool WhisperClient::isModelLoaded() const
{
    return model_loaded_;
}

void WhisperClient::processAudioData(const QByteArray &audio_data)
{
    if (audio_data.isEmpty()) {
        return;
    }

    QString result = runWhisperInference(audio_data);
    if (!result.isEmpty()) {
        emit transcriptionReady(result);
    }
}

QString WhisperClient::runWhisperInference(const QByteArray &pcm_data)
{
#ifdef __EMSCRIPTEN__
    int result = EM_ASM_INT({
        if (typeof Module.whisperInfer === 'undefined') {
            return 0;
        }
        var resultPtr = Module.whisperInfer($0, $1);
        return resultPtr;
    }, pcm_data.constData(), pcm_data.size());

    if (result == 0) {
        return QString();
    }

    const char *result_str = reinterpret_cast<const char *>(result);
    return QString::fromUtf8(result_str);
#else
    Q_UNUSED(pcm_data);
    return QString();
#endif
}
