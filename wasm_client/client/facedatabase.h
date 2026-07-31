#ifndef FACEDATABASE_H
#define FACEDATABASE_H

#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <vector>

struct PersonInfo {
    QString person_id;
    QString name;
};

struct FaceRecord {
    QString feature_id;
    QString person_id;
    QString person_name;
    std::vector<float> feature;
};

class FaceDatabase {
public:
    explicit FaceDatabase(const QString& db_path);
    ~FaceDatabase();

    bool addPerson(const PersonInfo& info);
    bool deletePerson(const QString& person_id);
    bool updatePersonName(const QString& person_id, const QString& name);
    std::vector<PersonInfo> getAllPersons();

    bool addFaceFeature(const QString& person_id,
                        const std::vector<float>& feature,
                        const QString& image_path = QString(),
                        float quality_score = 0.0f,
                        bool is_primary = false);
    bool deleteFaceFeatures(const QString& person_id);
    std::vector<FaceRecord> getAllFaceFeatures();

    QString getPrimaryFaceImagePath(const QString& person_id);

private:
    bool initSchema();
    QSqlDatabase db_;
};

#endif // FACEDATABASE_H