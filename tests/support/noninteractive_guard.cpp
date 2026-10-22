#ifdef _WIN32
#include <windows.h>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#include <initializer_list>
#endif

namespace {

// Linked directly into each noninteractive executable so this runs before its
// Qt-generated main creates QCoreApplication or QGuiApplication.
struct NoninteractiveGuard final {
    NoninteractiveGuard() noexcept {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#ifdef _MSC_VER
        _set_error_mode(_OUT_TO_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        for (const int report : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
            _CrtSetReportMode(report, _CRTDBG_MODE_FILE);
            _CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
        }
#endif
    }
};

const NoninteractiveGuard noninteractiveGuard;

} // namespace
#endif
