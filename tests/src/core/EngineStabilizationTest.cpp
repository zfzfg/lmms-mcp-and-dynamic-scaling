#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <atomic>
#include <thread>
#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "AudioBusHandle.h"
#include "AutomationClip.h"
#include "ConfigManager.h"
#include "DataFile.h"
#include "Engine.h"
#include "NoteTimeIndex.h"
#include "RecoveryIdentity.h"
#include "Song.h"

using namespace lmms;
namespace
{
struct ProbeState
{
    std::atomic<int> destroyed{0};
    std::atomic<QThread*> destructionThread{nullptr};
};
struct Probe : PlayHandle
{
    std::shared_ptr<AudioBusHandle> bus;
    std::shared_ptr<ProbeState> state;
    bool bound;
    std::atomic<bool> finished{false};
    Probe(std::shared_ptr<AudioBusHandle> audioBus, std::shared_ptr<ProbeState> result, bool affinity)
        : PlayHandle(Type::SamplePlayHandle), bus(std::move(audioBus)), state(std::move(result)), bound(affinity)
    { setAudioBusHandle(bus.get()); setUsesBuffer(false); }
    ~Probe() override { state->destructionThread.store(QThread::currentThread()); ++state->destroyed; }
    bool affinityMatters() const override { return bound; }
    bool isFinished() const override { return finished.load(); }
    bool isFromTrack(const Track*) const override { return false; }
    void play(SampleFrame*) override { finished.store(true); }
};
void renderOnCallerThread(AudioEngine* engine)
{
    std::jthread render([&]() { engine->renderNextPeriod(); });
}
}
class EngineStabilizationTest : public QObject
{
    Q_OBJECT
    QTemporaryDir directory;
private slots:
    void initTestCase()
    {
        QVERIFY(directory.isValid());
        QDir::addSearchPath("data", QFINDTESTDATA("../../../data"));
        auto* config = ConfigManager::inst();
        config->loadConfigFile(directory.filePath("test-config.xml"));
        config->setWorkingDir(directory.path());
        Engine::init(true);
        Engine::audioEngine()->audioDev()->stopProcessing();
    }
    void cleanupTestCase() { Engine::destroy(); }
    void saveReloadAndBundleCloneKeepTheDocument()
    {
        DataFile original(DataFile::Type::SongProject);
        auto notes = original.createElement("projectnotes");
        notes.appendChild(original.createCDATASection("<!DOCTYPE HTML PUBLIC 'notes'><p>kept</p>"));
        original.content().appendChild(notes);
        const QString path = directory.filePath("roundtrip.mmp");
        QVERIFY(original.writeFile(path, false, false));
        DataFile loaded(path);
        QVERIFY2(loaded.lastLoadError().isEmpty(), qPrintable(loaded.lastLoadError()));
        QVERIFY(loaded.toByteArray().contains("<!DOCTYPE HTML"));
        QVERIFY(original.writeFile(directory.filePath("roundtrip.mmpz"), false, false));
        DataFile compressed(directory.filePath("roundtrip.mmpz"));
        QVERIFY2(compressed.lastLoadError().isEmpty(), qPrintable(compressed.lastLoadError()));
        const auto before = original.toByteArray();
        QVERIFY(original.writeFile(directory.filePath("export.mmp"), true, false));
        QCOMPARE(original.toByteArray(), before);
        DataFile bundled(directory.filePath("export/export.mmp"));
        QVERIFY2(bundled.lastLoadError().isEmpty(), qPrintable(bundled.lastLoadError()));
    }
    void negativeDetuningSurvivesTheProductionLoader()
    {
        QDomDocument document;
        QVERIFY(document.setContent(QStringLiteral("<automationclip len='100'><time pos='-50' value='0' outValue='0'/><time pos='80' value='1' outValue='1'/></automationclip>")));
        AutomationClip clip(nullptr);
        clip.loadSettings(document.documentElement());
        QCOMPARE(clip.getTimeMap().firstKey(), -50);
        int start = 0, end = 0;
        NoteTimeIndex::spanBounds(1000, 10, clip.getTimeMap().firstKey(), clip.getTimeMap().lastKey(), 0, &start, &end);
        QCOMPARE(start, 950); QCOMPARE(end, 1080);
    }
    void finishedHandlesAreDrainedWithoutModelEdits()
    {
        auto* engine = Engine::audioEngine();
        auto bus = std::make_shared<AudioBusHandle>("release-test", false);
        auto state = std::make_shared<ProbeState>();
        QVERIFY(engine->addPlayHandle(new Probe(bus, state, false)));
        // No event processing or model edit occurs until the render thread has returned.
        for (int i = 0; i < 8 && state->destroyed.load() == 0; ++i)
        {
            renderOnCallerThread(engine);
            QTRY_VERIFY(!AudioEngineWorkerThread::batchInFlight());
            QCoreApplication::processEvents();
        }
        QTRY_COMPARE(state->destroyed.load(), 1);
        QCOMPARE(state->destructionThread.load(), QThread::currentThread());
    }
    void fullReleaseHandoffRetainsOwnershipUntilTheOwnerRuns()
    {
        auto* engine = Engine::audioEngine();
        auto bus = std::make_shared<AudioBusHandle>("release-overflow", false);
        auto state = std::make_shared<ProbeState>();
        int accepted = 0, rejected = 0;
        for (int batch : {500, 500, 100})
        {
            for (int i = 0; i < batch; ++i)
            {
                if (engine->addPlayHandle(new Probe(bus, state, false))) { ++accepted; }
                else { ++rejected; }
            }
            renderOnCallerThread(engine);
            QElapsedTimer wait; wait.start();
            while (AudioEngineWorkerThread::batchInFlight() && wait.elapsed() < 3000) { QThread::msleep(1); }
        }
        // No owner-thread event was dispatched during the producer burst.
        QCOMPARE(state->destroyed.load(), rejected);
        for (int attempt = 0; attempt < 32 && state->destroyed.load() < accepted + rejected; ++attempt)
        {
            QTest::qWait(20);
            renderOnCallerThread(engine);
        }
        QTRY_COMPARE(state->destroyed.load(), accepted + rejected);
        QCOMPARE(state->destructionThread.load(), QThread::currentThread());
    }
    void affinityBoundHandleStaysOnItsOwnerThread()
    {
        auto* engine = Engine::audioEngine();
        auto bus = std::make_shared<AudioBusHandle>("affinity-test", false);
        auto state = std::make_shared<ProbeState>();
        auto* probe = new Probe(bus, state, true);
        QVERIFY(engine->addPlayHandle(probe));
        renderOnCallerThread(engine);
        QTRY_VERIFY(!AudioEngineWorkerThread::batchInFlight());
        QCOMPARE(state->destroyed.load(), 0);
        engine->removePlayHandle(probe);
        QCOMPARE(state->destroyed.load(), 1);
        QCOMPARE(state->destructionThread.load(), QThread::currentThread());
    }
    void failedPeriodsHaveIndependentSilentOutputAndSnapshot()
    {
        auto* engine = Engine::audioEngine();
        auto old = engine->renderNextPeriod();
        QTRY_VERIFY(!AudioEngineWorkerThread::batchInFlight());
        QVERIFY(!engine->lastPeriodFailed());
        for (auto& frame : std::span(const_cast<SampleFrame*>(old.data()), old.size())) { frame = SampleFrame(0.75f, -0.25f); }
        struct Latch : ThreadableJob
        {
            std::atomic<bool> release{false};
            bool requiresProcessing() const override { return true; }
            void doProcessing() override { while (!release.load()) { QThread::msleep(1); } }
        } job;
        AudioEngineWorkerThread worker(nullptr);
        worker.start();
        struct Join
        {
            Latch& job; AudioEngineWorkerThread& worker;
            ~Join()
            {
                job.release.store(true);
                worker.quit();
                if (!worker.wait(3000)) { std::abort(); }
                QElapsedTimer timer; timer.start();
                while (AudioEngineWorkerThread::batchInFlight() && timer.elapsed() < 3000) { QThread::msleep(1); }
                if (AudioEngineWorkerThread::batchInFlight()) { std::abort(); }
            }
        } join{job, worker};
        QTRY_VERIFY(AudioEngineWorkerThread::aliveWorkers() > 0);
        QVERIFY(AudioEngineWorkerThread::tryResetJobQueue());
        QVERIFY(AudioEngineWorkerThread::addJob(&job));
        QVERIFY(!AudioEngineWorkerThread::startAndWaitForJobs(std::chrono::milliseconds(2), false));
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto failed = engine->renderNextPeriod();
            QVERIFY(engine->lastPeriodFailed());
            QVERIFY(failed.data() != old.data());
            for (const auto& frame : failed) { QCOMPARE(frame.left(), 0.f); QCOMPARE(frame.right(), 0.f); }
            std::vector<SampleFrame> snapshot(failed.size()); f_cnt_t frames = 0;
            QVERIFY(engine->displayBuffer().copyLatest(snapshot.data(), snapshot.size(), frames));
            QCOMPARE(static_cast<size_t>(frames), failed.size());
            for (const auto& frame : snapshot) { QCOMPARE(frame.left(), 0.f); QCOMPARE(frame.right(), 0.f); }
        }
    }
    void recoverySourceSurvivesUntilADurableSave()
    {
        const QString source = directory.filePath("recover-source.mmp");
        DataFile project(DataFile::Type::SongProject);
        QVERIFY(project.writeFile(source, false, false));
        QString error;
        QVERIFY(RecoveryIdentity::tryClaim(source, RecoveryIdentity::currentProcess(), &error));
        ConfigManager::inst()->retainRecoverySource(source);
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(!Engine::getSong()->saveProjectFile(directory.filePath("missing-parent/fail.mmp"), false, false));
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(Engine::getSong()->saveProjectFile(directory.filePath("protected.mmp"), false, false));
        QVERIFY(!QFileInfo::exists(source));
    }
};
QTEST_MAIN(EngineStabilizationTest)
#include "EngineStabilizationTest.moc"
