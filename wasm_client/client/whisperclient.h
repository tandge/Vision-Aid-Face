/**
 * @file whisperclient.h
 * @brief whisper.cpp speech recognition client interface
 *
 * Encapsulates whisper.cpp speech recognition functionality:
 * - Audio capture
 * - Speech-to-text
 * - Recognition result callback
 *
 * In WASM environment, bridges to whisper.cpp via Emscripten.
 */

#ifndef WHISPERCLIENT_H
#define WHISPERCLIENT_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QTimer>
#include <QFile>

class WhisperClient : public QObject
{
    Q_OBJECT

public:
    explicit WhisperClient(QObject *parent = nullptr);
    ~WhisperClient();

    void startRecognition();
    void stopRecognition();
    void setVolume(int volume);
    bool isModelLoaded() const;

signals:
    void transcriptionReady(const QString &text);
    void errorOccurred(const QString &error_message);

private:
    bool initWhisperContext();
    void processAudioData(const QByteArray &audio_data);
    QString runWhisperInference(const QByteArray &pcm_data);

    bool is_recording_;
    bool model_loaded_;
    int volume_threshold_;
    QTimer *capture_timer_;
    QString model_path_;
};

#endif // WHISPERCLIENT_H
