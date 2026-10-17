# Render deadline

Decision for AP-F15, 17 October 2026.

`AudioEngine::renderNextPeriod` arms one absolute deadline of two audio periods, clamped to 2–50 ms. Later mixer rounds call `startAndWaitForJobs` against that same deadline. They do not each receive a fresh 2–50 ms budget.

The wait account splits inline time and wait time (`RenderWaitAccount`). Counters distinguish a missed deadline, a wait that expired, a full queue, and a job that was not started.

This is not a hard bound on the callback. `JobQueue::run()` and `ThreadableJob::process()` are synchronous. Once they have started, the clock does not interrupt them. A plugin that never returns still occupies the audio thread. The product limit stays fail-fast: a worker that is still inside `process()` after the join timeout aborts the process. `QThread::terminate()` is not used.

A failed period publishes digital silence from a buffer that workers are not given. `ProjectRenderer` treats that period as a failed export and does not write it into the file. Normal rendering resumes only after `batchInFlight()` is false and the mixer cleanup has run once.

No callback timings were measured in this change. Do not quote the 2–50 ms clamp as a measured end-to-end deadline.
