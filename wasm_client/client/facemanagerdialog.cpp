#include "facemanagerdialog.h"
#include "facedetector.h"
#include "facealigner.h"
#include "facerecognizer.h"
#include "facedatabase.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QUuid>
#include <QPixmap>
#include <QImage>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QFile>

static QImage cvMatToQImage(const cv::Mat& mat) {
    if (mat.empty()) return QImage();
    if (mat.type() == CV_8UC3) {
        return QImage(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_RGB888).rgbSwapped();
    } else if (mat.type() == CV_8UC4) {
        return QImage(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_ARGB32);
    } else if (mat.type() == CV_8UC1) {
        return QImage(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_Grayscale8);
    }
    return QImage();
}

FaceManagerDialog::FaceManagerDialog(const QString& retina_model,
                                     const QString& arcface_model,
                                     const QString& db_path,
                                     int camera_index,
                                     QWidget* parent)
    : QDialog(parent),
      capture_timer_(new QTimer(this)),
      cap_(camera_index),
      detector_(std::make_unique<FaceDetector>(retina_model.toStdString())),
      aligner_(std::make_unique<FaceAligner>()),
      recognizer_(std::make_unique<FaceRecognizer>(arcface_model.toStdString())),
      db_(std::make_unique<FaceDatabase>(db_path)),
      person_list_(new QListWidget(this)),
      add_btn_(new QPushButton(QStringLiteral("新增"), this)),
      del_btn_(new QPushButton(QStringLiteral("删除"), this)),
      mod_btn_(new QPushButton(QStringLiteral("修改"), this)),
      raw_face_label_(new QLabel(this)),
      aligned_face_label_(new QLabel(this)),
      name_edit_(new QLineEdit(this)),
      confirm_btn_(new QPushButton(QStringLiteral("确认"), this)),
      cancel_btn_(new QPushButton(QStringLiteral("取消"), this)),
      close_btn_(new QPushButton(QStringLiteral("关闭"), this)) {
    setupUI();
    loadPersonList();

    raw_face_label_->setFixedSize(150, 150);
    raw_face_label_->setStyleSheet(QStringLiteral("border: 1px solid #ccc;"));
    aligned_face_label_->setFixedSize(150, 150);
    aligned_face_label_->setStyleSheet(QStringLiteral("border: 1px solid #ccc;"));

    if (!cap_.isOpened()) {
        qWarning() << "Failed to open camera";
    }

    connect(capture_timer_, &QTimer::timeout, this, &FaceManagerDialog::onCaptureFrame);
    connect(add_btn_, &QPushButton::clicked, this, &FaceManagerDialog::onAddClicked);
    connect(del_btn_, &QPushButton::clicked, this, &FaceManagerDialog::onDeleteClicked);
    connect(mod_btn_, &QPushButton::clicked, this, &FaceManagerDialog::onModifyClicked);
    connect(confirm_btn_, &QPushButton::clicked, this, &FaceManagerDialog::onConfirmClicked);
    connect(cancel_btn_, &QPushButton::clicked, this, &FaceManagerDialog::onCancelClicked);
    connect(close_btn_, &QPushButton::clicked, this, &FaceManagerDialog::onCloseClicked);
    connect(person_list_, &QListWidget::currentItemChanged, this, &FaceManagerDialog::onPersonSelected);
    connect(name_edit_, &QLineEdit::textChanged, this, &FaceManagerDialog::onNameTextChanged);
    connect(name_edit_, &QLineEdit::returnPressed, this, &FaceManagerDialog::onConfirmClicked);

    setEditMode(false);
    loadPersonList();
}

FaceManagerDialog::~FaceManagerDialog() {
    capture_timer_->stop();
    if (cap_.isOpened()) cap_.release();
}

void FaceManagerDialog::setupUI() {
    setWindowTitle(QStringLiteral("人脸库管理"));
    setMinimumSize(640, 480);

    auto* left_layout = new QVBoxLayout();
    left_layout->addWidget(person_list_, 1);
    auto* left_btn_layout = new QHBoxLayout();
    left_btn_layout->addWidget(add_btn_);
    left_btn_layout->addWidget(del_btn_);
    left_btn_layout->addWidget(mod_btn_);
    left_layout->addLayout(left_btn_layout);

    auto* right_layout = new QVBoxLayout();
    auto* preview_layout = new QHBoxLayout();
    preview_layout->addWidget(raw_face_label_);
    preview_layout->addWidget(aligned_face_label_);
    right_layout->addLayout(preview_layout);

    auto* name_layout = new QHBoxLayout();
    name_layout->addWidget(new QLabel(QStringLiteral("姓名:"), this));
    name_layout->addWidget(name_edit_, 1);
    right_layout->addLayout(name_layout);

    auto* right_btn_layout = new QHBoxLayout();
    right_btn_layout->addStretch();
    right_btn_layout->addWidget(confirm_btn_);
    right_btn_layout->addWidget(cancel_btn_);
    right_btn_layout->addWidget(close_btn_);
    right_layout->addLayout(right_btn_layout);
    right_layout->addStretch();

    auto* main_layout = new QHBoxLayout(this);
    main_layout->addLayout(left_layout, 2);
    main_layout->addLayout(right_layout, 8);
}

void FaceManagerDialog::loadPersonList(int selectRow) {
    person_list_->clear();
    auto persons = db_->getAllPersons();
    for (const auto& p : persons) {
        auto* item = new QListWidgetItem(p.name);
        item->setData(Qt::UserRole, p.person_id);
        person_list_->addItem(item);
    }
    if (!persons.empty()) {
        int row = selectRow;
        if (row < 0) row = 0;
        if (row >= person_list_->count()) row = person_list_->count() - 1;
        person_list_->setCurrentRow(row);
    } else {
        // 空列表时保持浏览模式，清空右侧
        setEditMode(false);
        selected_person_id_.clear();
        name_edit_->clear();
        raw_face_label_->clear();
        aligned_face_label_->clear();
    }
}

cv::Mat FaceManagerDialog::cropFace(const cv::Mat& frame, const cv::Rect& bbox) {
    int x = std::max(0, bbox.x);
    int y = std::max(0, bbox.y);
    int w = std::min(bbox.width, frame.cols - x);
    int h = std::min(bbox.height, frame.rows - y);
    if (w <= 0 || h <= 0) return cv::Mat();
    return frame(cv::Rect(x, y, w, h)).clone();
}

void FaceManagerDialog::setEditMode(bool edit) {
    if (edit) {
        // 编辑模式：启动实时捕获，姓名可编辑
        if (!capture_timer_->isActive()) {
            capture_timer_->start(80);
        }
        name_edit_->setEnabled(true);
    } else {
        // 浏览模式：停止实时捕获，姓名只读
        if (capture_timer_->isActive()) {
            capture_timer_->stop();
        }
        name_edit_->setEnabled(false);
        raw_face_label_->clear();
    }
}

void FaceManagerDialog::onCaptureFrame() {
    if (!cap_.isOpened()) return;
    cv::Mat frame;
    cap_ >> frame;
    if (frame.empty()) return;

    auto faces = detector_->detect(frame);
    if (faces.empty()) {
        raw_face_label_->clear();
        aligned_face_label_->clear();
        current_aligned_.release();
        current_feature_.clear();
        return;
    }

    const auto& best = faces[0];
    cv::Mat raw_crop = cropFace(frame, best.bbox);
    if (!raw_crop.empty()) {
        cv::Mat disp;
        cv::resize(raw_crop, disp, cv::Size(150, 150));
        raw_face_label_->setPixmap(QPixmap::fromImage(cvMatToQImage(disp)));
    }

    cv::Mat aligned = aligner_->align(frame, best.landmarks);
    if (!aligned.empty()) {
        current_aligned_ = aligned.clone();
        cv::Mat disp;
        cv::resize(aligned, disp, cv::Size(150, 150));
        aligned_face_label_->setPixmap(QPixmap::fromImage(cvMatToQImage(disp)));
        current_feature_ = recognizer_->extract(aligned);
    }
}

void FaceManagerDialog::onAddClicked() {
    int row = person_list_->currentRow();
    if (row < 0) row = person_list_->count() - 1;

    auto* item = new QListWidgetItem(QStringLiteral(""));
    item->setData(Qt::UserRole, QString()); // 空 person_id 表示尚未保存
    person_list_->insertItem(row + 1, item);

    // 屏蔽信号，防止 setCurrentItem 触发 onPersonSelected 切回浏览模式
    person_list_->blockSignals(true);
    person_list_->setCurrentItem(item);
    person_list_->blockSignals(false);

    selected_person_id_.clear();
    name_edit_->clear();
    name_edit_->setFocus();
    raw_face_label_->clear();
    aligned_face_label_->clear();
    current_aligned_.release();
    current_feature_.clear();

    // 进入编辑模式（新增）
    setEditMode(true);
}

void FaceManagerDialog::onDeleteClicked() {
    auto* item = person_list_->currentItem();
    if (!item) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先选择人员"));
        return;
    }
    QString pid = item->data(Qt::UserRole).toString();
    QString name = item->text();
    int currentRow = person_list_->currentRow();

    int ret = QMessageBox::question(this, QStringLiteral("确认删除"),
                                    QStringLiteral("确定删除 %1 ?").arg(name));
    if (ret != QMessageBox::Yes) return;

    // 删除关联的人脸特征（SQLite 外键级联已开启，这里显式清理作为双重保险）
    db_->deleteFaceFeatures(pid);
    db_->deletePerson(pid);

    int newRow = currentRow - 1;
    if (newRow < 0) newRow = 0;
    loadPersonList(newRow);
}

void FaceManagerDialog::onModifyClicked() {
    auto* item = person_list_->currentItem();
    if (!item) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先选择人员"));
        return;
    }
    // 进入编辑模式，允许修改姓名和重新采集人脸
    setEditMode(true);
}

void FaceManagerDialog::onConfirmClicked() {
    QString name = name_edit_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请输入姓名"));
        return;
    }
    if (current_feature_.empty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("未检测到有效人脸"));
        return;
    }

    QString pid = selected_person_id_;
    if (pid.isEmpty()) {
        pid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        PersonInfo info;
        info.person_id = pid;
        info.name = name;
        if (!db_->addPerson(info)) {
            QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("添加人员失败"));
            return;
        }
    } else {
        // 修改模式下同步更新姓名
        db_->updatePersonName(pid, name);
    }

    QString img_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (img_dir.isEmpty()) img_dir = QDir::currentPath();
    QDir().mkpath(img_dir + "/faces");
    QString img_path = img_dir + QStringLiteral("/faces/%1_%2.jpg")
                                .arg(pid)
                                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmss")));

    cv::imwrite(img_path.toStdString(), current_aligned_);
    db_->addFaceFeature(pid, current_feature_, img_path, 1.0f, true);

    // 更新左侧当前列表项的名字和 person_id
    auto* item = person_list_->currentItem();
    if (item) {
        item->setText(name);
        item->setData(Qt::UserRole, pid);
    }

    // 切换到浏览模式，显示刚保存的图像
    selected_person_id_ = pid;
    setEditMode(false);
    QString saved_img = db_->getPrimaryFaceImagePath(pid);
    if (!saved_img.isEmpty() && QFile::exists(saved_img)) {
        cv::Mat img = cv::imread(saved_img.toStdString());
        if (!img.empty()) {
            cv::Mat disp;
            cv::resize(img, disp, cv::Size(150, 150));
            aligned_face_label_->setPixmap(QPixmap::fromImage(cvMatToQImage(disp)));
        }
    }

    QMessageBox::information(this, QStringLiteral("成功"), QStringLiteral("保存成功"));
}

void FaceManagerDialog::onPersonSelected() {
    auto* item = person_list_->currentItem();
    if (!item) return;
    selected_person_id_ = item->data(Qt::UserRole).toString();
    name_edit_->setText(item->text());

    // 进入浏览模式
    setEditMode(false);

    // 加载库中保存的人脸图像
    QString img_path = db_->getPrimaryFaceImagePath(selected_person_id_);
    if (!img_path.isEmpty() && QFile::exists(img_path)) {
        cv::Mat img = cv::imread(img_path.toStdString());
        if (!img.empty()) {
            cv::Mat disp;
            cv::resize(img, disp, cv::Size(150, 150));
            aligned_face_label_->setPixmap(QPixmap::fromImage(cvMatToQImage(disp)));
        }
    } else {
        aligned_face_label_->clear();
    }
}

void FaceManagerDialog::onCancelClicked() {
    reject();
}

void FaceManagerDialog::onCloseClicked() {
    accept();
}

void FaceManagerDialog::onNameTextChanged(const QString& text) {
    auto* item = person_list_->currentItem();
    if (!item) return;
    item->setText(text);
}