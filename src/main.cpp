#include "application.h"
#include "win_util.h"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>

#include <exception>
#include <string>

namespace {

HWND WaitForExistingController(const std::wstring& instance_identifier) {
    constexpr ULONGLONG timeout_milliseconds = 5000;
    constexpr DWORD retry_milliseconds = 50;
    const ULONGLONG deadline = GetTickCount64() + timeout_milliseconds;
    do {
        if (HWND existing = FindWindowW(
                desktopnote::Application::kControllerClassName,
                instance_identifier.c_str())) {
            return existing;
        }
        Sleep(retry_milliseconds);
    } while (GetTickCount64() < deadline);
    return nullptr;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    desktopnote::LogDebug("=== DesktopNote starting ===");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);

    const std::wstring mutex_name = desktopnote::DesktopNoteInstanceIdentifier();
    HANDLE mutex = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
    if (!mutex) {
        desktopnote::LogDebug("CreateMutexW failed");
        if (SUCCEEDED(com_result)) CoUninitialize();
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        desktopnote::LogDebug("Second instance detected, finding controller window...");
        HWND existing = WaitForExistingController(mutex_name);
        if (existing) {
            DWORD existing_process = 0;
            GetWindowThreadProcessId(existing, &existing_process);
            if (existing_process != 0) AllowSetForegroundWindow(existing_process);
            desktopnote::LogDebug("Found existing controller window, posting show message");
            PostMessageW(existing, desktopnote::Application::kShowExistingInstance, 0, 0);
            CloseHandle(mutex);
            if (SUCCEEDED(com_result)) CoUninitialize();
            return 0;
        }
        desktopnote::LogDebug("Existing controller window not found; refusing to start a concurrent writer.");
        MessageBoxW(nullptr, L"DesktopNote 已在运行，但暂时无法响应。请稍后重试。",
                    L"DesktopNote", MB_OK | MB_ICONWARNING);
        CloseHandle(mutex);
        if (SUCCEEDED(com_result)) CoUninitialize();
        return 1;
    }

    int exit_code = 1;
    try {
        desktopnote::LogDebug("Creating Application object...");
        desktopnote::Application application(instance, mutex_name);
        desktopnote::LogDebug("Running Application.Run()...");
        exit_code = application.Run();
        desktopnote::LogDebug("Application.Run() returned with exit_code=" + std::to_string(exit_code));
    } catch (const std::exception& error) {
        desktopnote::LogDebug("Exception caught in wWinMain: " + std::string(error.what()));
        std::wstring message;
        try { message = desktopnote::Utf8ToWide(error.what()); }
        catch (...) { message = L"DesktopNote 启动失败"; }
        MessageBoxW(nullptr, message.c_str(), L"DesktopNote", MB_OK | MB_ICONERROR);
    } catch (...) {
        desktopnote::LogDebug("Unknown exception caught in wWinMain");
        MessageBoxW(nullptr, L"DesktopNote 发生未知严重错误", L"DesktopNote", MB_OK | MB_ICONERROR);
    }

    CloseHandle(mutex);
    if (SUCCEEDED(com_result)) CoUninitialize();
    return exit_code;
}
