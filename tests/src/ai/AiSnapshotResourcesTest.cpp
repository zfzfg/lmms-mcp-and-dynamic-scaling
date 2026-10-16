#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

#include "AiProjectSnapshot.h"

class AiSnapshotResourcesTest : public QObject
{
	Q_OBJECT
private slots:
	void copiesAndHashesResources()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		QVERIFY(QDir(dir.path()).mkdir("resources"));
		QFile source(dir.filePath("source.wav"));
		QVERIFY(source.open(QIODevice::WriteOnly));
		const QByteArray content("sample data");
		QCOMPARE(source.write(content), qint64(content.size()));
		source.close();
		const auto info = QFileInfo(source.fileName());
		QJsonObject entry{{"sourcePath", source.fileName()}, {"fileName", "frozen.wav"}, {"bytes", info.size()},
			{"modifiedAt", info.lastModified().toMSecsSinceEpoch()}};
		QFile manifest(dir.filePath("manifest.json"));
		QVERIFY(manifest.open(QIODevice::WriteOnly));
		manifest.write(
			QJsonDocument(QJsonObject{{"snapshotId", "snapshot-test"}, {"resources", QJsonArray{entry}}}).toJson());
		manifest.close();
		const auto error = lmms::ai::AiProjectSnapshot::materializeResources(manifest.fileName());
		QVERIFY2(!error.hasError(), qPrintable(error.message));
		QFile copy(dir.filePath("resources/frozen.wav"));
		QVERIFY(copy.open(QIODevice::ReadOnly));
		QCOMPARE(copy.readAll(), content);
		QVERIFY(manifest.open(QIODevice::ReadOnly));
		const auto result = QJsonDocument::fromJson(manifest.readAll()).object();
		QVERIFY(result["materialized"].toBool());
		const auto resource = result["resources"].toArray().first().toObject();
		QVERIFY(!resource.contains("sourcePath"));
		QCOMPARE(resource["sha256"].toString(),
			QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex()));
	}
	void rejectsChangedResource()
	{
		QTemporaryDir dir;
		QVERIFY(QDir(dir.path()).mkdir("resources"));
		QFile source(dir.filePath("source.wav"));
		QVERIFY(source.open(QIODevice::WriteOnly));
		source.write("changed");
		source.close();
		QFile manifest(dir.filePath("manifest.json"));
		QVERIFY(manifest.open(QIODevice::WriteOnly));
		manifest.write(QJsonDocument(QJsonObject{{"snapshotId", "test"},
										 {"resources",
											 QJsonArray{QJsonObject{{"sourcePath", source.fileName()},
												 {"fileName", "frozen.wav"}, {"bytes", 1}, {"modifiedAt", 0}}}}})
				.toJson());
		manifest.close();
		const auto error = lmms::ai::AiProjectSnapshot::materializeResources(manifest.fileName());
		QCOMPARE(error.code, lmms::ai::ErrorCode::AssetNotFound);
		QVERIFY(!QFile::exists(dir.filePath("resources/frozen.wav")));
	}
	void rejectsMalformedManifest()
	{
		QTemporaryDir dir;
		QFile manifest(dir.filePath("manifest.json"));
		QVERIFY(manifest.open(QIODevice::WriteOnly));
		manifest.write("{}");
		manifest.close();
		QCOMPARE(lmms::ai::AiProjectSnapshot::materializeResources(manifest.fileName()).code,
			lmms::ai::ErrorCode::ValidationError);
	}
};
QTEST_GUILESS_MAIN(AiSnapshotResourcesTest)
#include "AiSnapshotResourcesTest.moc"
