# Render deadline

Decision for AP-F15, 17 October 2026.

`AudioEngine::renderNextPeriod` arms one absolute deadline of two audio periods, clamped to 2–50 ms. Later mixer rounds call `startAndWaitForJobs` against that same deadline. They do not each receive a fresh 2–50 ms budget.

The wait account splits inline time and wait time (`RenderWaitAccount`). Counters distinguish a missed deadline, a wait that expired, a full queue, and a job that was not started.

This is not a hard bound on the callback. `JobQueue::run()` and `ThreadableJob::process()` are synchronous. Once they have started, the clock does not interrupt them. A plugin that never returns still occupies the audio thread. The product limit stays fail-fast: a worker that is still inside `process()` after the join timeout aborts the process. `QThread::terminate()` is not used.

A failed period publishes digital silence from a buffer that workers are not given. `ProjectRenderer` treats that period as a failed export and does not write it into the file. Normal rendering resumes only after `batchInFlight()` is false and the mixer cleanup has run once.

No callback timings were measured in this change. Do not quote the 2–50 ms clamp as a measured end-to-end deadline.

Follow-up on `64bc33825`: settled and on-time are separate conditions. A late completed wait fails, as does a captured job exception. Inline scanning stops starting further jobs after expiry; if no worker can drain the remaining slots, they are canceled and accounted before recycling. A final deadline check precedes successful output publication. Queue exceptions always finish the reservation and set terminal job state. `WorkerDeadlineTest` covers these paths with 0/1/3 workers, overflow and next-generation reset in Release/Debug; whole-callback timings and hung plugins remain unmeasured.

The audio-device stop helper retains its 30-second join timeout but now aborts the process instead of terminating the callback thread. Worker joins retain their 3-second timeout. These are shutdown limits, not callback deadlines. Shutdown cancels remaining unclaimed queue slots only after producer stop and worker joins.
