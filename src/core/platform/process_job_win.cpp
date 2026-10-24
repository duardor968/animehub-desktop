#include "process_job.h"

#include <QProcess>

#include <qt_windows.h>

namespace animehub::platform {

struct ProcessJob::State {
    HANDLE job = nullptr;
};

ProcessJob::ProcessJob() : state_(std::make_unique<State>()) {
    state_->job = CreateJobObjectW(nullptr, nullptr);
    if (state_->job == nullptr) {
        return;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(state_->job, JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits))) {
        CloseHandle(state_->job);
        state_->job = nullptr;
    }
}

ProcessJob::~ProcessJob() {
    if (state_->job != nullptr) {
        CloseHandle(state_->job);
    }
}

bool ProcessJob::assign(qint64 pid) {
    if (state_->job == nullptr || pid <= 0) {
        return false;
    }
    HANDLE process =
        OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return false;
    }
    const bool assigned = AssignProcessToJobObject(state_->job, process) != FALSE;
    CloseHandle(process);
    return assigned;
}

void prepareHelperProcess(QProcess& process) {
    process.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });
}

} // namespace animehub::platform
