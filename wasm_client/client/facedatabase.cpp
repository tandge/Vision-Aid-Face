#include "facedatabase.h"
#include <QSqlError>
#include <QSqlRecord>
#include <QDebug>
#include <QByteArray>
#include <QFile>
#include <QFileInfo>

FaceDatabase::FaceDatabase(const QString& db_path) {
    db_ = QSqlDatabase::addDatabase("QSQLITE", QStringLiteral("face_conn_%1").arg(reinterpret_cast<quintptr>(this)));
    db_.setDatabaseName(db_path);
    if (!db_.open()) {
        qWarning() << "Failed to open face DB:" << db_.lastError().text();
        return;
    }
    db_.exec(QStringLiteral("PRAGMA foreign_keys = ON;"));
    initSchema();
}

FaceDatabase::~FaceDatabase() {
    QString connName = db_.connectionName();
    db_.close();
    QSqlDatabase::removeDatabase(connName);
}

bool FaceDatabase::initSchema() {
    // 从 exe 同目录下的 db/table.txt 读取建表脚本
    QFileInfo dbInfo(db_.databaseName());
    QString schemaPath = dbInfo.absolutePath() + QStringLiteral("/db/table.txt");

    QFile file(schemaPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Cannot open schema file:" << schemaPath;
        return false;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    // 按分号分割成多条 SQL 语句
    QStringList statements = content.split(';', Qt::SkipEmptyParts);
    QSqlQuery query(db_);

    for (QString stmt : statements) {
        stmt = stmt.trimmed();
        if (stmt.isEmpty()) continue;
        // 跳过纯注释片段
        if (stmt.startsWith("--")) continue;

        if (!query.exec(stmt)) {
            QString err = query.lastError().text();
            // 忽略表/索引已存在的错误，其他错误打印警告
            if (!err.contains("already exists", Qt::CaseInsensitive)) {
                qWarning() << "Schema exec error:" << err;
            }
        }
    }
    return true;
}

bool FaceDatabase::addPerson(const PersonInfo& info) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO persons (person_id, name) VALUES (:pid, :name)"));
    query.bindValue(QStringLiteral(":pid"), info.person_id);
    query.bindValue(QStringLiteral(":name"), info.name);
    if (!query.exec()) {
        qWarning() << "addPerson error:" << query.lastError().text();
        return false;
    }
    return true;
}

bool FaceDatabase::deletePerson(const QString& person_id) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("DELETE FROM persons WHERE person_id = :pid"));
    query.bindValue(QStringLiteral(":pid"), person_id);
    return query.exec();
}

bool FaceDatabase::updatePersonName(const QString& person_id, const QString& name) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("UPDATE persons SET name = :name, update_time = CURRENT_TIMESTAMP WHERE person_id = :pid"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":pid"), person_id);
    return query.exec();
}

std::vector<PersonInfo> FaceDatabase::getAllPersons() {
    std::vector<PersonInfo> list;
    QSqlQuery query(db_);
    if (!query.exec(QStringLiteral("SELECT person_id, name FROM persons WHERE is_deleted = 0 ORDER BY create_time DESC"))) {
        return list;
    }
    while (query.next()) {
        PersonInfo p;
        p.person_id = query.value(0).toString();
        p.name = query.value(1).toString();
        list.push_back(std::move(p));
    }
    return list;
}

bool FaceDatabase::addFaceFeature(const QString& person_id,
                                  const std::vector<float>& feature,
                                  const QString& image_path,
                                  float quality_score,
                                  bool is_primary) {
    QByteArray blob(reinterpret_cast<const char*>(feature.data()),
                    static_cast<int>(feature.size() * sizeof(float)));
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("INSERT INTO face_features (person_id, feature, image_path, quality_score, is_primary) "
                   "VALUES (:pid, :feat, :img, :qs, :prim)"));
    query.bindValue(QStringLiteral(":pid"), person_id);
    query.bindValue(QStringLiteral(":feat"), blob);
    query.bindValue(QStringLiteral(":img"), image_path);
    query.bindValue(QStringLiteral(":qs"), quality_score);
    query.bindValue(QStringLiteral(":prim"), is_primary ? 1 : 0);
    if (!query.exec()) {
        qWarning() << "addFaceFeature error:" << query.lastError().text();
        return false;
    }
    return true;
}

bool FaceDatabase::deleteFaceFeatures(const QString& person_id) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral("DELETE FROM face_features WHERE person_id = :pid"));
    query.bindValue(QStringLiteral(":pid"), person_id);
    return query.exec();
}

QString FaceDatabase::getPrimaryFaceImagePath(const QString& person_id) {
    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "SELECT image_path FROM face_features WHERE person_id = :pid AND is_primary = 1 LIMIT 1"));
    query.bindValue(QStringLiteral(":pid"), person_id);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    // fallback: get any face image for this person
    query.prepare(QStringLiteral(
        "SELECT image_path FROM face_features WHERE person_id = :pid LIMIT 1"));
    query.bindValue(QStringLiteral(":pid"), person_id);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return QString();
}

std::vector<FaceRecord> FaceDatabase::getAllFaceFeatures() {
    std::vector<FaceRecord> list;
    QSqlQuery query(db_);
    query.prepare(QStringLiteral(
        "SELECT f.feature_id, f.person_id, p.name, f.feature "
        "FROM face_features f JOIN persons p ON f.person_id = p.person_id"
    ));
    if (!query.exec()) return list;
    while (query.next()) {
        FaceRecord r;
        r.feature_id = query.value(0).toString();
        r.person_id = query.value(1).toString();
        r.person_name = query.value(2).toString();
        QByteArray blob = query.value(3).toByteArray();
        const float* ptr = reinterpret_cast<const float*>(blob.constData());
        int count = blob.size() / static_cast<int>(sizeof(float));
        r.feature.assign(ptr, ptr + count);
        list.push_back(std::move(r));
    }
    return list;
}