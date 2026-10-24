#include "process_job.h"

#include <QProcess>

#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>

namespace animehub::platform {

struct ProcessJob::State {};

ProcessJob::ProcessJob() = default;
ProcessJob::~ProcessJob() = default;

bool ProcessJob::assign(qint64 pid) {
    // Linux has no job objects: prepareHelperProcess() tied the helper as it started.
    return pid > 0;
}

void prepareHelperProcess(QProcess& process) {
    const auto parent = getpid();
    process.setChildProcessModifier([parent] {
        // The kernel ends the helper when the thread that started it ends: AnimeHub starts its
        // helpers from the main thread. If AnimeHub died before this, the helper goes too.
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() != parent) {
            _exit(1);
        }
    });
}

} // namespace animehub::platform
