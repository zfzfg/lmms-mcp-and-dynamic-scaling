#include <QtTest>
#include "AiDomainTypes.h"

class AiDomainContractTest : public QObject
{
	Q_OBJECT
private slots:
	void testNoteDataRoundtrip()
	{
		using namespace lmms::ai;
		NoteData n1;
		n1.noteRef = "nt_clp_1_1_0";
		n1.positionTicks = 192;
		n1.lengthTicks = 48;
		n1.midiKey = 64;
		n1.volumePercent = 85;
		n1.pan = -20;
		n1.noteType = "regular";
		n1.hasDetuning = true;

		QJsonObject json = n1.toJson();
		NoteData n2 = NoteData::fromJson(json);

		QCOMPARE(n2.noteRef, n1.noteRef);
		QCOMPARE(n2.positionTicks, n1.positionTicks);
		QCOMPARE(n2.lengthTicks, n1.lengthTicks);
		QCOMPARE(n2.midiKey, n1.midiKey);
		QCOMPARE(n2.volumePercent, n1.volumePercent);
		QCOMPARE(n2.pan, n1.pan);
		QCOMPARE(n2.noteType, n1.noteType);
		QCOMPARE(n2.hasDetuning, n1.hasDetuning);
	}

	void testDomainErrorRoundtrip()
	{
		using namespace lmms::ai;
		DomainError e1;
		e1.code = ErrorCode::RevisionConflict;
		e1.message = "Stale revision";
		e1.retryable = false;

		QJsonObject json = e1.toJson();
		DomainError e2 = DomainError::fromJson(json);

		QCOMPARE(e2.code, e1.code);
		QCOMPARE(e2.message, e1.message);
		QCOMPARE(e2.retryable, e1.retryable);
	}

	void testPermissionFlags()
	{
		using namespace lmms::ai;
		Permission p = Permission::Read | Permission::WriteProject;
		QVERIFY(hasPermission(p, Permission::Read));
		QVERIFY(hasPermission(p, Permission::WriteProject));
		QVERIFY(!hasPermission(p, Permission::Export));

		QStringList list = permissionsToStringList(p);
		QVERIFY(list.contains("read"));
		QVERIFY(list.contains("write_project"));
		QVERIFY(!list.contains("export"));

		Permission p2 = permissionsFromStringList(list);
		QCOMPARE(p2, p);
	}

	void testErrorCodeStrings()
	{
		using namespace lmms::ai;
		QCOMPARE(errorCodeToString(ErrorCode::Ok), QString("OK"));
		QCOMPARE(errorCodeToString(ErrorCode::NotReady), QString("NOT_READY"));
		QCOMPARE(errorCodeToString(ErrorCode::RevisionConflict), QString("REVISION_CONFLICT"));
		QCOMPARE(errorCodeToString(ErrorCode::PermissionDenied), QString("PERMISSION_DENIED"));

		QCOMPARE(stringToErrorCode("OK"), ErrorCode::Ok);
		QCOMPARE(stringToErrorCode("NOT_READY"), ErrorCode::NotReady);
		QCOMPARE(stringToErrorCode("REVISION_CONFLICT"), ErrorCode::RevisionConflict);
	}
};

QTEST_GUILESS_MAIN(AiDomainContractTest)
#include "AiDomainContractTest.moc"
