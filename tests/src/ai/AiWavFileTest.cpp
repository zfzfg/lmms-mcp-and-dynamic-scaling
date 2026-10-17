#include "AiWavFile.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
class AiWavFileTest : public QObject
{
	Q_OBJECT
	static QByteArray wav()
	{
		QByteArray bytes(44 + 400, '\0');
		auto u16 = [&](int at, quint16 value) { qToLittleEndian(value, reinterpret_cast<uchar*>(bytes.data() + at)); };
		auto u32 = [&](int at, quint32 value) { qToLittleEndian(value, reinterpret_cast<uchar*>(bytes.data() + at)); };
		bytes.replace(0, 4, "RIFF");
		u32(4, bytes.size() - 8);
		bytes.replace(8, 8, "WAVEfmt ");
		u32(16, 16);
		u16(20, 1);
		u16(22, 2);
		u32(24, 44100);
		u32(28, 176400);
		u16(32, 4);
		u16(34, 16);
		bytes.replace(36, 4, "data");
		u32(40, 400);
		return bytes;
	}
private slots:
	void validatesRealFramesAndRequestedSettings()
	{
		QTemporaryDir temp;
		QFile file(temp.filePath("audio.wav"));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(wav());
		file.close();
		lmms::ai::WavInfo info;
		QVERIFY(!lmms::ai::inspectWavFile(file.fileName(), info, 44100, 16).hasError());
		QCOMPARE(info.channels, 2);
		QCOMPARE(info.dataBytes, qint64(400));
		QVERIFY(info.durationSeconds > 0);
		QVERIFY(lmms::ai::inspectWavFile(file.fileName(), info, 48000, 16).hasError());
	}
	void rejectsTruncationAndFakeHeader()
	{
		QTemporaryDir temp;
		QFile file(temp.filePath("audio.wav"));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(wav().left(44));
		file.close();
		lmms::ai::WavInfo info;
		QVERIFY(lmms::ai::inspectWavFile(file.fileName(), info).hasError());
		auto bytes = wav();
		bytes[32] = 0;
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(bytes);
		file.close();
		QVERIFY(lmms::ai::inspectWavFile(file.fileName(), info).hasError());
	}
};
QTEST_GUILESS_MAIN(AiWavFileTest)
#include "AiWavFileTest.moc"
