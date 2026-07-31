#ifndef FACEMANAGERDIALOG_H
#define FACEMANAGERDIALOG_H

#include <QDialog>
#include <QTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <opencv2/opencv.hpp>
#include <memory>
#include <vector>

class FaceDetector;
class FaceAligner;
class FaceRecognizer;
class FaceDatabase;

class FaceManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit FaceManagerDialog(const QString& retina_model,
                               const QString& arcface_model,
                               const QString& db_path,
                               int camera_index = 0,
                               QWidget* parent = nullptr);
    ~FaceManagerDialog();

private slots:
    void onCaptureFrame();
    void onAddClicked();
    void onDeleteClicked();
    void onModifyClicked();
    void onConfirmClicked();
    void onPersonSelected();
    void onCancelClicked();
    void onCloseClicked();
    void onNameTextChanged(const QString& text);

private:
    void setupUI();
    void loadPersonList(int selectRow = 0);
    void setEditMode(bool edit);
    cv::Mat cropFace(const cv::Mat& frame, const cv::Rect& bbox);

    QTimer* capture_timer_;
    cv::VideoCapture cap_;

    std::unique_ptr<FaceDetector> detector_;
    std::unique_ptr<FaceAligner> aligner_;
    std::unique_ptr<FaceRecognizer> recognizer_;
    std::unique_ptr<FaceDatabase> db_;

    QListWidget* person_list_;
    QPushButton* add_btn_;
    QPushButton* del_btn_;
    QPushButton* mod_btn_;

    QLabel* raw_face_label_;
    QLabel* aligned_face_label_;
    QLineEdit* name_edit_;
    QPushButton* confirm_btn_;
    QPushButton* cancel_btn_;
    QPushButton* close_btn_;

    cv::Mat current_aligned_;
    std::vector<float> current_feature_;
    QString selected_person_id_;
};

#endif // FACEMANAGERDIALOG_H