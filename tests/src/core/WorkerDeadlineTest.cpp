#include <QtTest>
#include <QProcess>
#include <memory>
#include <stdexcept>
#include <cstdlib>
#include <thread>
#include <vector>
#include "AudioEngineWorkerThread.h"

using namespace lmms;
using namespace std::chrono_literals;
namespace
{
struct Job : ThreadableJob
{
    int delay = 0;
    bool throws = false;
    std::atomic<int> calls{0};
    bool requiresProcessing() const override { return true; }
    void doProcessing() override
    {
        ++calls;
        if (delay) { std::this_thread::sleep_for(std::chrono::milliseconds(delay)); }
        if (throws) { throw std::runtime_error("injected job failure"); }
    }
};
struct Workers
{
    std::vector<std::unique_ptr<AudioEngineWorkerThread>> workers;
    explicit Workers(int count)
    {
        for (int i = 0; i < count; ++i)
        {
            auto worker = std::make_unique<AudioEngineWorkerThread>(nullptr);
            worker->start();
            workers.push_back(std::move(worker));
        }
    }
    ~Workers()
    {
        for (auto& worker : workers) { worker->quit(); }
        for (auto& worker : workers) { if (!worker->wait(3000)) { std::abort(); } }
    }
};
}
class WorkerDeadlineTest : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        AudioEngineWorkerThread::initializeQueueOnce();
        QVERIFY(AudioEngineWorkerThread::resetProcessStateForTests());
    }
    void lateInlineCompletionIsFailure()
    {
        Job slow;
        slow.delay = 15;
        QVERIFY(AudioEngineWorkerThread::addJob(&slow));
        QVERIFY(!AudioEngineWorkerThread::startAndWaitForJobs(2ms));
        QCOMPARE(slow.calls.load(), 1);
        QVERIFY(!AudioEngineWorkerThread::batchInFlight());
        QVERIFY(AudioEngineWorkerThread::lastWaitTimedOut());
        QVERIFY(AudioEngineWorkerThread::tryResetJobQueue());
    }
    void expiredInlineBatchDoesNotStrandJobs()
    {
        Job job;
        QVERIFY(AudioEngineWorkerThread::addJob(&job));
        AudioEngineWorkerThread::armRenderDeadline(std::chrono::steady_clock::now() - 1ms);
        QVERIFY(!AudioEngineWorkerThread::startAndWaitForJobs());
        QCOMPARE(job.calls.load(), 0);
        QVERIFY(!AudioEngineWorkerThread::batchInFlight());
        QVERIFY(AudioEngineWorkerThread::tryResetJobQueue());
        AudioEngineWorkerThread::armRenderDeadline({});
    }
    void noNewInlineJobStartsAfterTheDeadline()
    {
        Job slow, following; slow.delay = 15;
        QVERIFY(AudioEngineWorkerThread::addJob(&slow));
        QVERIFY(AudioEngineWorkerThread::addJob(&following));
        QVERIFY(!AudioEngineWorkerThread::startAndWaitForJobs(2ms));
        QCOMPARE(slow.calls.load(), 1); QCOMPARE(following.calls.load(), 0);
        QVERIFY(!AudioEngineWorkerThread::batchInFlight());
        QVERIFY(AudioEngineWorkerThread::tryResetJobQueue());
    }
    void shutdownCanCancelAnUnstartedGeneration()
    {
        Job pending;
        QVERIFY(AudioEngineWorkerThread::addJob(&pending));
        QVERIFY(AudioEngineWorkerThread::batchInFlight());
        AudioEngineWorkerThread::cancelPendingJobs();
        QCOMPARE(pending.calls.load(), 0);
        QCOMPARE(pending.state(), ThreadableJob::ProcessingState::Done);
        QVERIFY(!AudioEngineWorkerThread::batchInFlight());
        QVERIFY(AudioEngineWorkerThread::tryResetJobQueue());
    }
    void queueOverflowAndThrowStillDrainEveryReservation()
    {
        std::vector<std::unique_ptr<Job>> jobs;
        for (size_t i = 0; i < AudioEngineWorkerThread::JobQueue::JOB_QUEUE_SIZE; ++i)
        {
            jobs.push_back(std::make_unique<Job>());
            QVERIFY(AudioEngineWorkerThread::addJob(jobs.back().get()));
        }
        Job overflow; overflow.throws = true;
        QVERIFY(!AudioEngineWorkerThread::addJob(&overflow));
        overflow.queue();
        QVERIFY(!AudioEngineWorkerThread::processInlineJob(&overflow));
        QVERIFY(!AudioEngineWorkerThread::startAndWaitForJobs(500ms));
        QVERIFY(!AudioEngineWorkerThread::batchInFlight());
        for (const auto& job : jobs) { QCOMPARE(job->calls.load(), 1); }
        QVERIFY(AudioEngineWorkerThread::tryResetJobQueue());
    }
    void throwingJobs_data()
    {
        QTest::addColumn<int>("count");
        QTest::newRow("inline") << 0;
        QTest::newRow("one-worker") << 1;
        QTest::newRow("three-workers") << 3;
    }
    void throwingJobs()
    {
        QFETCH(int, count);
        Job failing, following;
        failing.throws = true;
        Workers workers(count);
        QTRY_COMPARE(AudioEngineWorkerThread::aliveWorkers(), count);
        QVERIFY(AudioEngineWorkerThread::addJob(&failing));
        QVERIFY(AudioEngineWorkerThread::addJob(&following));
        QVERIFY(!AudioEngineWorkerThread::startAndWaitForJobs(500ms, count == 0));
        QCOMPARE(failing.calls.load(), 1);
        QCOMPARE(failing.state(), ThreadableJob::ProcessingState::Done);
        QCOMPARE(following.calls.load(), 1);
        QVERIFY(AudioEngineWorkerThread::batchFailed());
        QVERIFY(!AudioEngineWorkerThread::batchInFlight());
        QVERIFY(AudioEngineWorkerThread::tryResetJobQueue());
        following.reset();
        QVERIFY(AudioEngineWorkerThread::addJob(&following));
        QVERIFY(AudioEngineWorkerThread::startAndWaitForJobs(500ms, count == 0));
        QCOMPARE(following.calls.load(), 2);
    }
    void finiteWorkerTimeoutKeepsTheBatchLive()
    {
        Job slow;
        slow.delay = 60;
        Workers workers(1);
        QTRY_COMPARE(AudioEngineWorkerThread::aliveWorkers(), 1);
        QVERIFY(AudioEngineWorkerThread::addJob(&slow));
        QVERIFY(!AudioEngineWorkerThread::startAndWaitForJobs(2ms, false));
        QVERIFY(AudioEngineWorkerThread::batchInFlight());
        QVERIFY(!AudioEngineWorkerThread::tryResetJobQueue());
        QTRY_VERIFY(!AudioEngineWorkerThread::batchInFlight());
        QVERIFY(AudioEngineWorkerThread::tryResetJobQueue());
    }
    void neverReturningWorkerIsContainedInAChild()
    {
        QProcess child;
        QElapsedTimer elapsed; elapsed.start();
        child.start(QCoreApplication::applicationFilePath(), {"--hang-worker"});
        QVERIFY(child.waitForStarted());
        QVERIFY(child.waitForFinished(7000));
        QVERIFY(child.exitCode() != 0);
        QVERIFY(elapsed.elapsed() >= 2500);
    }
};
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--hang-worker"))
    {
#ifdef _MSC_VER
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
        struct HangingJob : ThreadableJob
        {
            std::atomic<bool> entered{false};
            bool requiresProcessing() const override { return true; }
            void doProcessing() override { entered.store(true); for (;;) { QThread::msleep(1); } }
        } job;
        auto* worker = new AudioEngineWorkerThread(nullptr);
        worker->start();
        QElapsedTimer timer; timer.start();
        while (AudioEngineWorkerThread::aliveWorkers() == 0 && timer.elapsed() < 2000) { QThread::msleep(1); }
        if (AudioEngineWorkerThread::aliveWorkers() == 0) { return 2; }
        AudioEngineWorkerThread::addJob(&job);
        AudioEngineWorkerThread::startAndWaitForJobs(20ms, false);
        timer.restart();
        while (!job.entered.load() && timer.elapsed() < 2000) { QThread::msleep(1); }
        if (!job.entered.load()) { std::_Exit(2); }
        delete worker;
        return 3;
    }
    WorkerDeadlineTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "WorkerDeadlineTest.moc"
