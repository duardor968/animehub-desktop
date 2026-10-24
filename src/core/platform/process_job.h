#ifndef ANIMEHUB_CORE_PLATFORM_PROCESS_JOB_H
#define ANIMEHUB_CORE_PLATFORM_PROCESS_JOB_H

#include <QtGlobal>

#include <memory>

class QProcess;

namespace animehub::platform {

// Ties helper processes (aria2) to AnimeHub's lifetime: when AnimeHub exits or crashes, the
// operating system ends them too. Players are not contained: closing AnimeHub must not stop a
// video.
class ProcessJob {
public:
    ProcessJob();
    ProcessJob(const ProcessJob&) = delete;
    ProcessJob& operator=(const ProcessJob&) = delete;
    ~ProcessJob();

    bool assign(qint64 pid);

private:
    struct State;
    std::unique_ptr<State> state_;
};

// Called before a helper starts. On Windows it opens no console window; on Linux, which has no
// job objects, it is what ties the helper to AnimeHub.
void prepareHelperProcess(QProcess& process);

} // namespace animehub::platform

#endif // ANIMEHUB_CORE_PLATFORM_PROCESS_JOB_H
