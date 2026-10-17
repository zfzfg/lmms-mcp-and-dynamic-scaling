/*
 * StabilizationTest.cpp - regressions for save, load limits, pools, profiler and waveforms
 *
 * Copyright (c) 2026 LMMS Developers
 *
 * This file is part of LMMS - https://lmms.io
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <QtEndian>
#include <QtTest>

#include "lmmsconfig.h"
#ifdef LMMS_HAVE_MCP
#include "AiJobManager.h"
#endif
#include "AudioDisplayBuffer.h"
#include "AudioEngineProfiler.h"
#include "AudioEngineWorkerThread.h"
#include "ConfigManager.h"
#include "DataFile.h"
#include "SaveTransaction.h"
#include "NoteTimeIndex.h"
#include "SampleBuffer.h"
#include "SampleFrame.h"
#include "SampleThumbnail.h"
#include "ThreadableJob.h"
#include "ThreadPool.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace lmms;

class StabilizationTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		ConfigManager::inst()->setValue("app", "disablebackup", "0");
	}

	void publishReplacesFileAndLeavesNoTransient()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("song.mmp");
		QFile original(path);
		QVERIFY(original.open(QIODevice::WriteOnly));
		original.write("old");
		original.close();

		DataFile file(DataFile::Type::SongProject);
		file.content().setAttribute("marker", "new");
		QVERIFY(file.writeFile(path, false, false));
		QVERIFY(QFileInfo::exists(path));
		QVERIFY(!QFileInfo::exists(path + ".new"));
		QVERIFY(QFileInfo::exists(path + ".bak"));
		QFile written(path);
		QVERIFY(written.open(QIODevice::ReadOnly));
		const auto bytes = written.readAll();
		QVERIFY(bytes.contains("marker"));
		QVERIFY(!bytes.contains("old"));
	}

	void lockedBackupDoesNotReplaceGoodFile()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("song.mmp");
		QFile original(path);
		QVERIFY(original.open(QIODevice::WriteOnly));
		original.write("good-project");
		original.close();
		QVERIFY(QDir().mkdir(path + ".bak"));

		DataFile file(DataFile::Type::SongProject);
		QVERIFY(!file.writeFile(path, false, false));
		QCOMPARE(file.lastPublishResult().code, QString("save.backup"));
		QFile kept(path);
		QVERIFY(kept.open(QIODevice::ReadOnly));
		QCOMPARE(kept.readAll(), QByteArray("good-project"));
	}

	void bundleResourceFailurePublishesNothing()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("bundled.mmp");
		DataFile file(DataFile::Type::SongProject);
		auto clip = file.createElement("sampleclip");
		clip.setAttribute("src", dir.filePath("missing-sample.wav"));
		file.content().appendChild(clip);
		QVERIFY(!file.writeFile(path, true, false));
		const QString bundleDir = dir.filePath("bundled");
		QVERIFY(!QFileInfo::exists(bundleDir));
		const auto leftovers = QDir(dir.path()).entryList({QStringLiteral("bundled.staging-*")}, QDir::Dirs);
		QCOMPARE(leftovers.size(), 0);
	}

	void oversizedDeclaredCompressionIsRejected()
	{
		QByteArray payload(8, '\0');
		const quint32 declared = 1024u * 1024u * 1024u;
		qToBigEndian(declared, reinterpret_cast<uchar*>(payload.data()));
		DataFile file(payload);
		QVERIFY(file.head().isNull());
		QVERIFY(file.lastLoadError().contains("exceeds"));
	}

	void threadPoolCapturesExceptionsAndRejectsZeroDetection()
	{
		QCOMPARE(ThreadPool::defaultWorkerCount() >= ThreadPool::kMinWorkers, true);
		ThreadPool pool(0, 1);
		QCOMPARE(pool.numWorkers(), ThreadPool::kMinWorkers);

		auto failing = pool.enqueue([] { throw std::runtime_error("boom"); });
		bool threw = false;
		try
		{
			failing.get();
		}
		catch (const std::runtime_error& error)
		{
			threw = QString::fromUtf8(error.what()).contains("boom");
		}
		QVERIFY(threw);

		auto ok = pool.enqueue([] { return 7; });
		QCOMPARE(ok.get(), 7);

		auto first = pool.enqueue([] {
			QThread::msleep(300);
			return 1;
		});
		QThread::msleep(50);
		auto second = pool.enqueue([] { return 2; });
		auto third = pool.enqueue([] { return 3; });
		bool queueFull = false;
		try
		{
			third.get();
		}
		catch (const ThreadPool::QueueFull&)
		{
			queueFull = true;
		}
		QVERIFY(queueFull);
		QCOMPARE(first.get(), 1);
		QCOMPARE(second.get(), 2);
	}

	void profilerCountsLockWaitSeparatelyFromSmoothedLoad()
	{
		AudioEngineProfiler profiler;
		profiler.recordSample(100, 10000, 1000);
		QCOMPARE(profiler.deadlineMisses(), static_cast<uint64_t>(1));
		QVERIFY(profiler.cpuLoad() < 50);
		QCOMPARE(profiler.lastLockWaitUs(), 10000u);
		profiler.noteBackendXrun();
		QCOMPARE(profiler.backendXruns(), static_cast<uint64_t>(1));
	}

	void displayBufferKeepsSnapshotAfterSourceIsGone()
	{
		AudioDisplayBuffer buffer;
		{
			SampleFrame source[4];
			source[0] = SampleFrame(0.25f, -0.5f);
			source[1] = SampleFrame(0.0f, 0.0f);
			source[2] = SampleFrame(1.0f, 1.0f);
			source[3] = SampleFrame(-1.0f, 0.5f);
			buffer.publish(source, 4);
		}
		SampleFrame copied[4];
		f_cnt_t frames = 0;
		QVERIFY(buffer.copyLatest(copied, 4, frames));
		QCOMPARE(frames, 4);
		QCOMPARE(copied[0].left(), 0.25f);
		QCOMPARE(copied[0].right(), -0.5f);
	}

	void waveformOriginalFallbackDoesNotUsePeakWidth()
	{
		using gui::SampleThumbnail;
		const auto choice = SampleThumbnail::chooseSource(100, 20, 64);
		QCOMPARE(choice.kind, SampleThumbnail::WaveformSource::Frames);
		QCOMPARE(choice.width, static_cast<size_t>(100));

		SampleFrame frames[2];
		frames[0] = SampleFrame(-0.25f, 0.75f);
		frames[1] = SampleFrame(0.1f, -0.2f);
		const auto peak = SampleThumbnail::peakOverFrames(frames, 2, 0, 1);
		QCOMPARE(peak.first, -0.25f);
		QCOMPARE(peak.second, 0.75f);

		for (int column = 0; column < 64; ++column)
		{
			const auto span = SampleThumbnail::columnSpan(100, column, 64);
			QVERIFY(!span.empty());
			QVERIFY(span.end <= 100);
			QVERIFY(span.begin < span.end);
		}
		QVERIFY(SampleThumbnail::columnSpan(0, 0, 64).empty());
	}

	void failedBackupCopyKeepsPreviousBackup()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("song.mmp");
		QFile original(path);
		QVERIFY(original.open(QIODevice::WriteOnly));
		original.write("good-project");
		original.close();
		QFile backup(path + ".bak");
		QVERIFY(backup.open(QIODevice::WriteOnly));
		backup.write("previous-backup");
		backup.close();
		SaveIoHooks hooks;
        hooks.failAt = SaveIoHooks::Step::CopyProjectToCandidate;
        SaveTransaction::setHooks(&hooks);
        struct ResetHooks { ~ResetHooks() { SaveTransaction::setHooks(nullptr); } } resetHooks;

		DataFile file(DataFile::Type::SongProject);
		QVERIFY(!file.writeFile(path, false, false));
		QCOMPARE(file.lastPublishResult().code, QString("save.backup"));
		QFile kept(path);
		QVERIFY(kept.open(QIODevice::ReadOnly));
		QCOMPARE(kept.readAll(), QByteArray("good-project"));
		QFile keptBackup(path + ".bak");
		QVERIFY(keptBackup.open(QIODevice::ReadOnly));
		QCOMPARE(keptBackup.readAll(), QByteArray("previous-backup"));
	}

	void displayCopyLeavesDestinationUntouchedUntilClaimed()
	{
		AudioDisplayBuffer buffer;
		SampleFrame destination[4];
		destination[0] = SampleFrame(0.5f, 0.5f);
		f_cnt_t frames = 7;
		QVERIFY(!buffer.copyLatest(destination, 4, frames));
		QCOMPARE(destination[0].left(), 0.5f);

		std::jthread writer([&](std::stop_token stop) {
			int generation = 1;
			SampleFrame source[32];
			while (!stop.stop_requested())
			{
				const auto marker = static_cast<float>(generation);
				for (auto& frame : source) { frame = SampleFrame(marker, marker); }
				buffer.publish(source, 32);
				++generation;
			}
		});
		for (int sample = 0; sample < 200; ++sample)
		{
			SampleFrame copied[32];
			f_cnt_t count = 0;
			if (!buffer.copyLatest(copied, 32, count)) { continue; }
			QCOMPARE(count, 32);
			const float marker = copied[0].left();
			for (int index = 0; index < 32; ++index)
			{
				QCOMPARE(copied[index].left(), marker);
				QCOMPARE(copied[index].right(), marker);
			}
		}
		writer.request_stop();
		writer.join();
	}

	void noteIndexFindsOverlapsAndDetuneExtent()
	{
		QCOMPARE(NoteTimeIndex::drawnTicks(10, 500), 500);
		QCOMPARE(NoteTimeIndex::drawnTicks(-3, 0), 4);
		QCOMPARE(NoteTimeIndex::drawnTicks(0, 0), 0);

		NoteTimeIndex index;
		index.rebuild({
			NoteTimeIndex::Span{0, 100, 0},
			NoteTimeIndex::Span{80, 90, 1},
			NoteTimeIndex::Span{200, 400, 2},
			NoteTimeIndex::Span{1000, 1100, 3},
		});
		std::vector<int> found;
		index.query(90, 120, found);
		std::sort(found.begin(), found.end());
		QCOMPARE(found.size(), static_cast<std::size_t>(2));
		QCOMPARE(found[0], 0);
		QCOMPARE(found[1], 1);

		index.query(100, 100, found);
		QVERIFY(std::find(found.begin(), found.end(), 0) != found.end());
		QVERIFY(std::find(found.begin(), found.end(), 2) == found.end());

		index.query(500, 600, found);
		QVERIFY(found.empty());
	}

	void thumbnailRevisionSeesMiddleFrames()
	{
		std::vector<SampleFrame> first(8, SampleFrame(0.1f, -0.2f));
		std::vector<SampleFrame> second = first;
		first[3] = SampleFrame(0.8f, 0.4f);
		second[3] = SampleFrame(-0.8f, -0.4f);
		const SampleBuffer left(std::move(first), 44100);
		const SampleBuffer right(std::move(second), 44100);
		QVERIFY(gui::SampleThumbnail::contentRevisionOf(left) != gui::SampleThumbnail::contentRevisionOf(right));
	}

	void thumbnailRevisionSeesUnsampledStride()
	{
		std::vector<SampleFrame> first(256, SampleFrame(0.0f, 0.0f));
		std::vector<SampleFrame> second = first;
		first[2] = SampleFrame(0.5f, -0.5f);
		const SampleBuffer left(std::move(first), 44100);
		const SampleBuffer right(std::move(second), 44100);
		QVERIFY(gui::SampleThumbnail::contentRevisionOf(left) != gui::SampleThumbnail::contentRevisionOf(right));
	}

#ifdef LMMS_HAVE_MCP
	void artifactFileReadsChunksAndRejectsChanges()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath("render.bin");
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		const QByteArray payload("abcdefghij");
		QVERIFY(file.write(payload) == payload.size());
		file.close();

		QCryptographicHash digest(QCryptographicHash::Sha256);
		digest.addData(payload);
		ai::AiJobManager::ArtifactFileRequest request;
		request.path = path;
		request.sha256 = QString::fromLatin1(digest.result().toHex());
		request.offset = 0;
		request.limit = 4;
		request.needHash = true;
		const auto first = ai::AiJobManager::readArtifactFile(request);
		QVERIFY(first.ok);
		QCOMPARE(first.bytes, QByteArray("abcd"));

		request.offset = 4;
		request.needHash = false;
		request.expectSize = first.size;
		request.expectMtimeMs = first.mtimeMs;
		const auto second = ai::AiJobManager::readArtifactFile(request);
		QVERIFY(second.ok);
		QCOMPARE(second.bytes, QByteArray("efgh"));

		request.offset = -1;
		request.needHash = true;
		const auto negative = ai::AiJobManager::readArtifactFile(request);
		QVERIFY(!negative.ok);

		QFile rewritten(path);
		QVERIFY(rewritten.open(QIODevice::WriteOnly | QIODevice::Truncate));
		QVERIFY(rewritten.write("short") == 5);
		rewritten.close();
		request.offset = 0;
		request.needHash = false;
		request.expectSize = first.size;
		request.expectMtimeMs = first.mtimeMs;
		const auto changed = ai::AiJobManager::readArtifactFile(request);
		QVERIFY(!changed.ok);
		QCOMPARE(changed.code, QString("modified"));
	}
#endif

	void workerBatchStaysLiveUntilProcessReturns()
	{
		std::atomic<bool> entered{false};
		std::atomic<bool> release{false};
		struct LatchJob : ThreadableJob
		{
			std::atomic<bool>* enteredFlag = nullptr;
			std::atomic<bool>* releaseFlag = nullptr;
			bool requiresProcessing() const override { return true; }
			void doProcessing() override
			{
				enteredFlag->store(true);
				while (!releaseFlag->load()) { QThread::msleep(1); }
			}
		} job;
		job.enteredFlag = &entered;
		job.releaseFlag = &release;

		AudioEngineWorkerThread worker(nullptr);
		worker.start();
		std::thread waiter;
		// Declared after the thread so an early QVERIFY destroys the guard first.
		// The guard joins before std::thread's destructor runs.
		struct JoinGuard
		{
			AudioEngineWorkerThread* worker = nullptr;
			std::atomic<bool>* releaseFlag = nullptr;
			std::thread* waiter = nullptr;
			~JoinGuard()
			{
				if (releaseFlag) { releaseFlag->store(true); }
				if (waiter && waiter->joinable()) { waiter->join(); }
				if (worker)
				{
					worker->quit();
					worker->wait(3000);
				}
			}
		} guard{&worker, &release, &waiter};

		QElapsedTimer aliveTimer;
		aliveTimer.start();
		while (AudioEngineWorkerThread::aliveWorkers() < 1 && aliveTimer.elapsed() < 2000)
		{
			QThread::msleep(1);
		}
		QVERIFY(AudioEngineWorkerThread::aliveWorkers() >= 1);

		std::vector<ThreadableJob*> items{&job};
		QVERIFY(AudioEngineWorkerThread::fillJobQueue(items));

		std::atomic<bool> firstDone{false};
		std::atomic<bool> firstOk{true};
		waiter = std::thread([&] {
			firstOk = AudioEngineWorkerThread::startAndWaitForJobs(std::chrono::milliseconds(40), false);
			firstDone = true;
		});

		QElapsedTimer enteredTimer;
		enteredTimer.start();
		while (!entered.load() && enteredTimer.elapsed() < 2000) { QThread::msleep(1); }
		QVERIFY(entered.load());
		QVERIFY(!AudioEngineWorkerThread::tryResetJobQueue());
		QVERIFY(AudioEngineWorkerThread::batchInFlight());

		QElapsedTimer secondTimer;
		secondTimer.start();
		QVERIFY(!AudioEngineWorkerThread::startAndWaitForJobs(std::chrono::milliseconds(2000), false));
		QVERIFY(secondTimer.elapsed() < 500);

		QElapsedTimer doneTimer;
		doneTimer.start();
		while (!firstDone.load() && doneTimer.elapsed() < 2000) { QThread::msleep(1); }
		QVERIFY(firstDone.load());
		QVERIFY(!firstOk.load());
		QVERIFY(AudioEngineWorkerThread::batchInFlight());
		QVERIFY(!AudioEngineWorkerThread::tryResetJobQueue());

		release.store(true);
		QElapsedTimer idleTimer;
		idleTimer.start();
		while (AudioEngineWorkerThread::batchInFlight() && idleTimer.elapsed() < 2000) { QThread::msleep(1); }
		QVERIFY(!AudioEngineWorkerThread::batchInFlight());
		QVERIFY(AudioEngineWorkerThread::tryResetJobQueue());
	}
};

QTEST_GUILESS_MAIN(StabilizationTest)
#include "StabilizationTest.moc"
