#include <QXmlStreamReader>
#include <QtEndian>
#include <QtTest>
#include <QDirIterator>
#include <QFile>

#include "ProjectCodec.h"

using namespace lmms;

class ProjectCodecTest : public QObject
{
	Q_OBJECT
private slots:
	void repositoryDocumentsAndLegacyDoctypes()
	{
		const QString root = QFINDTESTDATA("../../../data");
		QVERIFY(!root.isEmpty());
		int inspected = 0;
		QDirIterator files(root, {"*.mmp", "*.mpt", "*.xpf"}, QDir::Files, QDirIterator::Subdirectories);
		while (files.hasNext())
		{
			QFile file(files.next());
			QVERIFY(file.open(QIODevice::ReadOnly));
			const auto result = ProjectCodec::inspectXml(file.readAll());
			QVERIFY2(result.status == ProjectCodecStatus::Ok, qPrintable(file.fileName() + ": " + result.message));
			++inspected;
		}
		QVERIFY(inspected > 100);
	}

	void declarationsAreParsedRatherThanSearched()
	{
		const QByteArray xml = "<!DOCTYPE lmms-project><lmms-project>"
			"<!-- <!ENTITY harmless> --><![CDATA[<!DOCTYPE HTML PUBLIC 'notes'>]]></lmms-project>";
		QCOMPARE(ProjectCodec::inspectXml(xml).status, ProjectCodecStatus::Ok);
		const auto decoded = ProjectCodec::decodeQtCompressed(qCompress(xml));
		QCOMPARE(decoded.status, ProjectCodecStatus::Ok);
		QCOMPARE(ProjectCodec::inspectXml(decoded.bytes).status, ProjectCodecStatus::Ok);
		QVERIFY(ProjectCodec::inspectXml("<!DOCTYPE lmms-project SYSTEM 'file:///missing'><lmms-project/>").status != ProjectCodecStatus::Ok);
		QVERIFY(ProjectCodec::inspectXml("<!DOCTYPE lmms-project [<!ENTITY x 'text'>]><lmms-project>&x;</lmms-project>").status != ProjectCodecStatus::Ok);
	}

	void init()
	{
		ProjectCodec::setLimits(ProjectCodecLimits{});
	}

	void xmlErrorKeepsLineAndColumn()
	{
		const QByteArray xml = "<?xml version=\"1.0\"?>\n<lmms-project>\n<broken>\n";
		const auto result = ProjectCodec::inspectXml(xml);
		QCOMPARE(result.status, ProjectCodecStatus::Xml);
		QVERIFY(result.line > 0);
	}

	void utf8BomIsXml()
	{
		QByteArray xml;
		xml.append(static_cast<char>(0xEF));
		xml.append(static_cast<char>(0xBB));
		xml.append(static_cast<char>(0xBF));
		xml.append("<lmms-project/>");
		QCOMPARE(ProjectCodec::classify(xml), ProjectBytesKind::Xml);
		QCOMPARE(ProjectCodec::inspectXml(xml).status, ProjectCodecStatus::Ok);
	}

    void utf16BomIsXml_data()
    {
        QTest::addColumn<bool>("little");
        QTest::newRow("little-endian") << true;
        QTest::newRow("big-endian") << false;
    }
    void utf16BomIsXml()
    {
        QFETCH(bool, little);
        const QString text = QStringLiteral("<!DOCTYPE lmms-project><lmms-project/>");
        QByteArray xml;
        for (const QChar unit : QString(QChar(0xfeff)) + text)
        {
            const auto value = unit.unicode();
            xml.append(static_cast<char>(little ? value & 0xff : value >> 8));
            xml.append(static_cast<char>(little ? value >> 8 : value & 0xff));
        }
        QCOMPARE(ProjectCodec::classify(xml), ProjectBytesKind::Xml);
        QCOMPARE(ProjectCodec::inspectXml(xml).status, ProjectCodecStatus::Ok);
    }

	void shortAndBadHeadersAreNotXmlSizes()
	{
		QCOMPARE(ProjectCodec::classify(QByteArray("<?x")), ProjectBytesKind::Xml);
		QByteArray binary(3, '\0');
		QCOMPARE(ProjectCodec::decodeQtCompressed(binary).status, ProjectCodecStatus::Truncated);
		QByteArray bad(8, '\0');
		QCOMPARE(ProjectCodec::decodeQtCompressed(bad).status, ProjectCodecStatus::Header);
	}

	void declaredOversizeIsRejectedBeforeInflate()
	{
		QByteArray payload(8, '\0');
		qToBigEndian<quint32>(1024u * 1024u * 1024u, reinterpret_cast<uchar*>(payload.data()));
		payload[4] = static_cast<char>(0x78);
		payload[5] = static_cast<char>(0x9c);
		const auto result = ProjectCodec::decodeQtCompressed(payload);
		QCOMPARE(result.status, ProjectCodecStatus::Budget);
		QCOMPARE(result.peakDecodedBytes, 0);
	}

	void expansionPastASmallDeclarationStops()
	{
		const QByteArray plain(4096, 'x');
		QByteArray compressed = qCompress(plain);
		qToBigEndian<quint32>(32u, reinterpret_cast<uchar*>(compressed.data()));
		ProjectCodecLimits limits;
		limits.maxDecodedBytes = 256 * 1024;
		ProjectCodec::setLimits(limits);
		const auto result = ProjectCodec::decodeQtCompressed(compressed);
		QVERIFY(result.status == ProjectCodecStatus::Budget || result.status == ProjectCodecStatus::Checksum
			|| result.status == ProjectCodecStatus::Unavailable);
		QVERIFY(result.peakDecodedBytes <= 32);
	}

	void checksumMismatchIsDistinct()
	{
		QByteArray compressed = qCompress(QByteArray("hello lmms"));
		compressed[compressed.size() - 1] = static_cast<char>(compressed.back() ^ 0xFF);
		const auto result = ProjectCodec::decodeQtCompressed(compressed);
		QVERIFY(result.status == ProjectCodecStatus::Checksum || result.status == ProjectCodecStatus::Truncated
			|| result.status == ProjectCodecStatus::Unavailable);
	}

	void depthAndElementBudgets()
	{
		ProjectCodecLimits limits;
		limits.maxDepth = 3;
		limits.maxElements = 100;
		ProjectCodec::setLimits(limits);
		const auto deep = ProjectCodec::inspectXml(QByteArray("<a><b><c><d/></c></b></a>"));
		QCOMPARE(deep.status, ProjectCodecStatus::XmlBudget);
		QByteArray wide = "<root>";
		for (int index = 0; index < 5; ++index) { wide += "<n/>"; }
		wide += "</root>";
		limits.maxDepth = 8;
		limits.maxElements = 3;
		ProjectCodec::setLimits(limits);
		QCOMPARE(ProjectCodec::inspectXml(wide).status, ProjectCodecStatus::XmlBudget);
	}

	void validCompressedRoundTrip()
	{
		const QByteArray xml = "<lmms-project version=\"1\" type=\"song\"/>";
		const auto decoded = ProjectCodec::decodeQtCompressed(qCompress(xml));
		if (decoded.status == ProjectCodecStatus::Unavailable)
		{
			QSKIP("zlib is not linked in this build");
		}
		QCOMPARE(decoded.status, ProjectCodecStatus::Ok);
		QCOMPARE(decoded.bytes, xml);
	}

	void chunkBoundariesAndLargeXmlRoundTrip()
	{
		for (int size : {65535, 65536, 65537, 2 * 1024 * 1024})
		{
			const QByteArray xml = "<lmms-project>" + QByteArray(size, 'x') + "</lmms-project>";
			const auto decoded = ProjectCodec::decodeQtCompressed(qCompress(xml));
			QCOMPARE(decoded.status, ProjectCodecStatus::Ok);
			QCOMPARE(decoded.bytes, xml);
			QCOMPARE(ProjectCodec::inspectXml(decoded.bytes).status, ProjectCodecStatus::Ok);
		}
	}
};

QTEST_GUILESS_MAIN(ProjectCodecTest)
#include "ProjectCodecTest.moc"
