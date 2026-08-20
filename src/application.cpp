#include "application.h"

#include "note_window.h"
#include "resource.h"
#include "win_util.h"

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace desktopnote {
namespace {

constexpr UINT kTrayCallback = WM_APP + 1;
constexpr UINT kDeleteNote = WM_APP + 2;
constexpr UINT kNewNote = WM_APP + 3;
constexpr UINT_PTR kAutosaveTimer = 1;
constexpr UINT_PTR kIdleTrimTimer = 2;
constexpr UINT_PTR kTrayRetryTimer = 3;
constexpr UINT kAutosaveDelayMilliseconds = 500;
constexpr UINT kAutosaveRetryMilliseconds = 2000;
constexpr UINT kIdleCheckIntervalMilliseconds = 5000;
constexpr UINT kTrayRetryIntervalMilliseconds = 2000;
constexpr ULONGLONG kIdleTrimThresholdMilliseconds = 15000;

constexpr int kTrayToggle = 4001;
constexpr int kTrayNew = 4002;
constexpr int kTrayNormal = 4003;
constexpr int kTrayTopMost = 4004;
constexpr int kTrayDesktop = 4005;
constexpr int kTrayDisableClickThrough = 4006;
constexpr int kTrayUnlock = 4007;
constexpr int kTrayExit = 4008;
constexpr int kTrayRescue = 4009;
constexpr int kTrayAutoStart = 4010;
constexpr int kTrayOpenDataFolder = 4011;
constexpr int kTrayNoteCommandBase = 5000;

bool IsAutoStartEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD size = 0;
        const LONG res = RegQueryValueExW(key, L"DesktopNote", nullptr, &type, nullptr, &size);
        RegCloseKey(key);
        return res == ERROR_SUCCESS;
    }
    return false;
}

bool SetAutoStartEnabled(bool enable) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t exe_path[MAX_PATH]{};
            GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
            std::wstring command = L"\"" + std::wstring(exe_path) + L"\"";
            RegSetValueExW(key, L"DesktopNote", 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(command.c_str()),
                           static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(key, L"DesktopNote");
        }
        RegCloseKey(key);
        return true;
    }
    return false;
}

}  // namespace

Application::Application(HINSTANCE instance, std::wstring instance_identifier)
    : instance_(instance), instance_identifier_(std::move(instance_identifier)) {}

Application::~Application() {
    if (!exiting_ && state_ready_) {
        try { SaveNow(); } catch (...) {}
    }
    RemoveTrayIcon();
    for (auto& window : windows_) window->Destroy();
    windows_.clear();
    if (controller_window_) DestroyWindow(controller_window_);
    if (tray_icon_) DestroyIcon(tray_icon_);
}

bool Application::CreateControllerWindow() {
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = ControllerProcedure;
    window_class.hInstance = instance_;
    window_class.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_DESKTOPNOTE));
    window_class.hIconSm = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_DESKTOPNOTE), IMAGE_ICON,
                                                         GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    window_class.lpszClassName = kControllerClassName;
    if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    controller_window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kControllerClassName,
                                         instance_identifier_.c_str(), WS_POPUP,
                                         0, 0, 0, 0, nullptr, nullptr, instance_, this);
    if (controller_window_ && window_class.hIcon) {
        SendMessageW(controller_window_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(window_class.hIcon));
    }
    if (controller_window_ && window_class.hIconSm) {
        SendMessageW(controller_window_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(window_class.hIconSm));
    }
    taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
    return controller_window_ != nullptr;
}

int Application::Run() {
    LogDebug("Application::Run starting...");
    if (!CreateControllerWindow()) {
        LogDebug("Application::CreateControllerWindow FAILED!");
        throw std::runtime_error("cannot create DesktopNote controller window");
    }
    LogDebug("Controller window created: HWND=" + std::to_string(reinterpret_cast<uintptr_t>(controller_window_)));
    loaded_state_ = data_store_.Load();
    LogDebug("Data loaded. Note count in state: " + std::to_string(loaded_state_.notes.size()));
    CreateInitialWindows();
    LogDebug("Created windows count: " + std::to_string(windows_.size()));
    state_ready_ = true;
    AddTrayIcon();
    LogDebug("Tray icon added: " + std::string(tray_added_ ? "true" : "false"));
    if (!tray_added_) {
        SetTimer(controller_window_, kTrayRetryTimer, kTrayRetryIntervalMilliseconds, nullptr);
    }
    ShowAllWindows();
    NoteUserActivity();
    SetTimer(controller_window_, kIdleTrimTimer, kIdleCheckIntervalMilliseconds, nullptr);

    LogDebug("Entering message loop...");
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    LogDebug("Message loop exited with code: " + std::to_string(message.wParam));
    return static_cast<int>(message.wParam);
}

void Application::CreateInitialWindows() {
    auto notes = loaded_state_.notes;
    if (notes.empty()) notes = CreateDefaultState().notes;

    std::vector<std::unique_ptr<NoteWindow>> created_windows;
    NoteWindow* restored_active_window = nullptr;
    for (const auto& note : notes) {
        auto window = std::make_unique<NoteWindow>(instance_, note, NoteWindowCallbacks{
            [this](NoteWindow* active) { active_window_ = active; NoteUserActivity(); },
            [this] { ScheduleSave(); NoteUserActivity(); },
            [this] { SaveNow(); NoteUserActivity(); },
            [this](NoteWindow* target) { NoteUserActivity(); PostMessageW(controller_window_, kDeleteNote, 0, reinterpret_cast<LPARAM>(target)); },
            [this](NoteWindow* source) { NoteUserActivity(); PostMessageW(controller_window_, kNewNote, 0, reinterpret_cast<LPARAM>(source)); },
        });
        if (!window->Create()) {
            throw std::runtime_error(
                "cannot create note window for note " + note.id +
                "; existing data was not modified");
        }
        if (window->note().id == loaded_state_.last_active_note_id) {
            restored_active_window = window.get();
        }
        created_windows.push_back(std::move(window));
    }
    if (created_windows.empty()) {
        auto default_state = CreateDefaultState();
        auto window = std::make_unique<NoteWindow>(instance_, default_state.notes.front(), NoteWindowCallbacks{
            [this](NoteWindow* active) { active_window_ = active; NoteUserActivity(); },
            [this] { ScheduleSave(); NoteUserActivity(); },
            [this] { SaveNow(); NoteUserActivity(); },
            [this](NoteWindow* target) { NoteUserActivity(); PostMessageW(controller_window_, kDeleteNote, 0, reinterpret_cast<LPARAM>(target)); },
            [this](NoteWindow* source) { NoteUserActivity(); PostMessageW(controller_window_, kNewNote, 0, reinterpret_cast<LPARAM>(source)); },
        });
        if (!window->Create()) {
            throw std::runtime_error(
                "cannot create the default note window; existing data was not modified");
        }
        created_windows.push_back(std::move(window));
    }
    windows_ = std::move(created_windows);
    active_window_ = restored_active_window ? restored_active_window : (windows_.empty() ? nullptr : windows_.front().get());
    loaded_state_.notes.clear();
}

NoteWindow* Application::CreateNewNote(NoteWindow* source) {
    Note note;
    note.id = NewId();
    note.created_at_utc = UtcNowIso8601();
    note.modified_at_utc = note.created_at_utc;
    if (source) {
        source->SyncNote();
        note.window = source->note().window;
        note.window.x_dip += 30;
        note.window.y_dip += 30;
        note.window.mode = WindowMode::Normal;
        note.window.locked = false;
        note.window.click_through = false;
        note.appearance = source->note().appearance;
    }
    auto window = std::make_unique<NoteWindow>(instance_, std::move(note), NoteWindowCallbacks{
        [this](NoteWindow* active) { active_window_ = active; },
        [this] { ScheduleSave(); },
        [this] { SaveNow(); },
        [this](NoteWindow* target) { PostMessageW(controller_window_, kDeleteNote, 0, reinterpret_cast<LPARAM>(target)); },
        [this](NoteWindow* origin) { PostMessageW(controller_window_, kNewNote, 0, reinterpret_cast<LPARAM>(origin)); },
    });
    if (!window->Create()) return nullptr;
    NoteWindow* result = window.get();
    windows_.push_back(std::move(window));
    active_window_ = result;
    result->Activate();
    ScheduleSave();
    return result;
}

void Application::DeleteNote(NoteWindow* target) {
    const auto iterator = std::find_if(windows_.begin(), windows_.end(),
                                       [target](const auto& item) { return item.get() == target; });
    if (iterator == windows_.end()) return;
    if (MessageBoxW(target->hwnd(), L"确定删除此便签吗？此操作不可撤销。", L"DesktopNote",
                    MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES) return;
    data_store_.CreateBackup(L"before_delete");
    (*iterator)->Destroy();
    windows_.erase(iterator);
    active_window_ = windows_.empty() ? nullptr : windows_.back().get();
    if (windows_.empty()) CreateNewNote();
    SaveNow();
}

void Application::ActivateNote(std::size_t index) {
    if (index >= windows_.size()) return;
    active_window_ = windows_[index].get();
    active_window_->Activate();
}

void Application::ScheduleSave() {
    if (!controller_window_ || !state_ready_ || exiting_) return;
    save_pending_ = true;
    SetTimer(controller_window_, kAutosaveTimer, kAutosaveDelayMilliseconds, nullptr);
}

bool Application::SaveNow() {
    if (!controller_window_ || !state_ready_ || exiting_) return false;
    KillTimer(controller_window_, kAutosaveTimer);
    try {
        AppState state;
        state.last_active_note_id = active_window_ ? active_window_->note().id : std::string{};
        for (auto& window : windows_) {
            if (!window->SyncNote()) {
                throw std::runtime_error("cannot export rich text for saving");
            }
            state.notes.push_back(window->note());
        }
        Normalize(state);
        data_store_.Save(state);
        save_pending_ = false;
        save_error_reported_ = false;
        return true;
    } catch (const std::exception& error) {
        save_pending_ = true;
        SetTimer(controller_window_, kAutosaveTimer, kAutosaveRetryMilliseconds, nullptr);
        if (!save_error_reported_) NotifyError(L"保存失败，将自动重试", Utf8ToWide(error.what()));
        save_error_reported_ = true;
    } catch (...) {
        save_pending_ = true;
        SetTimer(controller_window_, kAutosaveTimer, kAutosaveRetryMilliseconds, nullptr);
        if (!save_error_reported_) NotifyError(L"保存失败，将自动重试", L"发生未知保存错误");
        save_error_reported_ = true;
    }
    return false;
}

void Application::Exit() {
    if (exiting_) return;
    if (!SaveNow()) return;
    exiting_ = true;
    RemoveTrayIcon();
    for (auto& window : windows_) window->Destroy();
    windows_.clear();
    DestroyWindow(controller_window_);
    controller_window_ = nullptr;
}

void Application::AddTrayIcon() {
    if (!controller_window_) {
        LogDebug("AddTrayIcon: controller_window_ is null!");
        return;
    }
    if (!tray_icon_) {
        tray_icon_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_DESKTOPNOTE), IMAGE_ICON,
                                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        if (!tray_icon_) tray_icon_ = CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
    }
    LogDebug("AddTrayIcon: tray_icon_=" + std::to_string(reinterpret_cast<uintptr_t>(tray_icon_)));

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(NOTIFYICONDATAW);
    data.hWnd = controller_window_;
    data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kTrayCallback;
    data.hIcon = tray_icon_;
    wcscpy_s(data.szTip, L"DesktopNote 桌面便签");

    Shell_NotifyIconW(NIM_DELETE, &data);

    tray_added_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    if (!tray_added_) {
        const DWORD err1 = GetLastError();
        LogDebug("AddTrayIcon: NIM_ADD with sizeof=" + std::to_string(sizeof(NOTIFYICONDATAW)) + " failed, err=" + std::to_string(err1));

        data.cbSize = NOTIFYICONDATAW_V3_SIZE;
        tray_added_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
        if (!tray_added_) {
            const DWORD err2 = GetLastError();
            LogDebug("AddTrayIcon: NIM_ADD with V3_SIZE failed, err=" + std::to_string(err2));

            data.cbSize = NOTIFYICONDATAW_V2_SIZE;
            tray_added_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
            LogDebug("AddTrayIcon: NIM_ADD with V2_SIZE result=" + std::string(tray_added_ ? "true" : "false"));
        }
    }

    if (tray_added_) {
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }
}

void Application::RemoveTrayIcon() {
    if (!tray_added_ || !controller_window_) return;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = controller_window_;
    data.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &data);
    tray_added_ = false;
}

void Application::ShowTrayMenu(POINT point) {
    HMENU menu = CreatePopupMenu();
    struct NoteMenuCommand {
        int command = 0;
        NoteWindow* window = nullptr;
    };
    std::vector<NoteMenuCommand> note_commands;
    int next_note_command = kTrayNoteCommandBase;

    // 1. Core actions
    AppendMenuW(menu, MF_STRING, kTrayNew, L"新建便签 (Ctrl+N)");
    SetMenuDefaultItem(menu, kTrayNew, FALSE);

    const bool any_visible = std::any_of(windows_.begin(), windows_.end(),
                                         [](const auto& w) { return w->IsVisible(); });
    AppendMenuW(menu, MF_STRING, kTrayToggle, any_visible ? L"隐藏全部便签" : L"显示全部便签");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // 2. Note list (Flat & direct access)
    if (!windows_.empty()) {
        HMENU notes_menu = CreatePopupMenu();
        for (std::size_t index = 0; index < windows_.size(); ++index) {
            const int activate_command = next_note_command++;
            note_commands.push_back({activate_command, windows_[index].get()});
            auto title = windows_[index]->DisplayTitle();
            if (title.size() > 28) title = title.substr(0, 28) + L"…";
            std::wstring item_text = std::to_wstring(index + 1) + L". " + title;
            const UINT flags = MF_STRING | (windows_[index].get() == active_window_ ? MF_CHECKED : 0);
            AppendMenuW(notes_menu, flags, activate_command, item_text.c_str());
        }
        std::wstring list_title = L"便签列表 (共 " + std::to_wstring(windows_.size()) + L" 篇)";
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(notes_menu), list_title.c_str());
    }

    // 3. Batch Window Modes
    HMENU mode_menu = CreatePopupMenu();
    size_t normal_count = 0, topmost_count = 0, desktop_count = 0;
    for (const auto& w : windows_) {
        if (w->note().window.mode == WindowMode::Normal) normal_count++;
        else if (w->note().window.mode == WindowMode::TopMost) topmost_count++;
        else if (w->note().window.mode == WindowMode::Desktop) desktop_count++;
    }
    AppendMenuW(mode_menu, MF_STRING | (normal_count >= topmost_count && normal_count >= desktop_count && normal_count > 0 ? MF_CHECKED : 0),
                kTrayNormal, L"普通模式");
    AppendMenuW(mode_menu, MF_STRING | (topmost_count > normal_count && topmost_count >= desktop_count ? MF_CHECKED : 0),
                kTrayTopMost, L"置顶模式 (Ctrl+T)");
    AppendMenuW(mode_menu, MF_STRING | (desktop_count > normal_count && desktop_count > topmost_count ? MF_CHECKED : 0),
                kTrayDesktop, L"嵌入桌面");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(mode_menu), L"全部窗口模式");

    // 4. Rescue & Emergency Submenu
    HMENU rescue_menu = CreatePopupMenu();
    AppendMenuW(rescue_menu, MF_STRING, kTrayRescue, L"🎯 居中召回所有便签");
    AppendMenuW(rescue_menu, MF_STRING, kTrayUnlock, L"🔓 解除全部位置锁定");
    AppendMenuW(rescue_menu, MF_STRING, kTrayDisableClickThrough, L"🖱️ 解除全部鼠标穿透");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(rescue_menu), L"应急与恢复");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // 5. System settings
    const bool auto_start = IsAutoStartEnabled();
    AppendMenuW(menu, MF_STRING | (auto_start ? MF_CHECKED : 0), kTrayAutoStart, L"开机自启动");
    AppendMenuW(menu, MF_STRING, kTrayOpenDataFolder, L"打开数据存储目录...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // 6. Exit
    AppendMenuW(menu, MF_STRING, kTrayExit, L"退出 DesktopNote");

    SetForegroundWindow(controller_window_);
    UINT menu_flags = TPM_RETURNCMD | TPM_RIGHTBUTTON;
    menu_flags |= GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    const int command = TrackPopupMenu(menu, menu_flags, point.x, point.y,
                                       0, controller_window_, nullptr);
    DestroyMenu(menu);
    PostMessageW(controller_window_, WM_NULL, 0, 0);

    const auto note_command = std::find_if(note_commands.begin(), note_commands.end(),
                                           [command](const auto& item) { return item.command == command; });
    if (note_command != note_commands.end()) {
        const auto iterator = std::find_if(
            windows_.begin(), windows_.end(),
            [target = note_command->window](const auto& item) { return item.get() == target; });
        if (iterator != windows_.end()) {
            ActivateNote(static_cast<std::size_t>(std::distance(windows_.begin(), iterator)));
        }
        return;
    }

    switch (command) {
        case kTrayToggle: ToggleAllWindows(); break;
        case kTrayNew: CreateNewNote(active_window_); break;
        case kTrayNormal: SetAllWindowModes(WindowMode::Normal); break;
        case kTrayTopMost: SetAllWindowModes(WindowMode::TopMost); break;
        case kTrayDesktop: SetAllWindowModes(WindowMode::Desktop); break;
        case kTrayDisableClickThrough: DisableAllClickThrough(); break;
        case kTrayUnlock: UnlockAllWindows(); break;
        case kTrayRescue: RescueAllWindows(); break;
        case kTrayAutoStart: SetAutoStartEnabled(!auto_start); break;
        case kTrayOpenDataFolder: OpenDataFolder(); break;
        case kTrayExit: Exit(); break;
    }
}

void Application::RescueAllWindows() {
    POINT pt{100, 100};
    HMONITOR primary = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    if (GetMonitorInfoW(primary, &monitor_info)) {
        int x = monitor_info.rcWork.left + 80;
        int y = monitor_info.rcWork.top + 80;
        for (auto& window : windows_) {
            window->SetLocked(false);
            window->SetClickThrough(false);
            window->Show(true);
            RECT rect{};
            GetWindowRect(window->hwnd(), &rect);
            const int width = std::max<int>(rect.right - rect.left, 300);
            const int height = std::max<int>(rect.bottom - rect.top, 200);
            SetWindowPos(window->hwnd(), HWND_TOP, x, y, width, height, SWP_SHOWWINDOW);
            x += 40;
            y += 40;
            if (x + width > monitor_info.rcWork.right || y + height > monitor_info.rcWork.bottom) {
                x = monitor_info.rcWork.left + 80;
                y = monitor_info.rcWork.top + 80;
            }
        }
        ScheduleSave();
    }
}

void Application::OpenDataFolder() {
    const auto& path = data_store_.root_directory();
    std::filesystem::create_directories(path);
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void Application::ShowAllWindows() {
    if (windows_.empty()) {
        CreateNewNote();
        return;
    }
    for (auto& window : windows_) window->Show(true);
    if (active_window_) {
        active_window_->Activate();
    } else if (!windows_.empty()) {
        active_window_ = windows_.front().get();
        active_window_->Activate();
    }
}

void Application::ToggleAllWindows() {
    const bool all_visible = std::all_of(windows_.begin(), windows_.end(),
                                         [](const auto& window) { return window->IsVisible(); });
    if (all_visible) for (auto& window : windows_) window->Hide();
    else ShowAllWindows();
}

void Application::SetAllWindowModes(WindowMode mode) {
    for (auto& window : windows_) window->SetWindowMode(mode);
    ScheduleSave();
}

void Application::DisableAllClickThrough() {
    for (auto& window : windows_) window->SetClickThrough(false);
    ShowAllWindows();
    ScheduleSave();
}

void Application::UnlockAllWindows() {
    for (auto& window : windows_) window->SetLocked(false);
    ShowAllWindows();
    ScheduleSave();
}

void Application::NoteUserActivity() {
    last_activity_tick_ = GetTickCount64();
}

void Application::TrimIdleMemory() {
    if (save_pending_) {
        SaveNow();
    }
    for (auto& window : windows_) {
        window->ReleaseIdleResources();
    }
    HeapCompact(GetProcessHeap(), 0);
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

void Application::NotifyError(const wchar_t* title, const std::wstring& message) {
    if (!tray_added_) return;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = controller_window_;
    data.uID = 1;
    data.uFlags = NIF_INFO;
    wcsncpy_s(data.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(data.szInfo, message.c_str(), _TRUNCATE);
    data.dwInfoFlags = NIIF_ERROR;
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

LRESULT CALLBACK Application::ControllerProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    Application* self = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<Application*>(create->lpCreateParams);
        self->controller_window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleControllerMessage(message, wparam, lparam)
                : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT Application::HandleControllerMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == taskbar_created_message_ && taskbar_created_message_ != 0) {
        tray_added_ = false;
        AddTrayIcon();
        for (auto& window : windows_) window->ReapplyDesktopMode();
        return 0;
    }
    switch (message) {
        case kTrayCallback:
            switch (LOWORD(lparam)) {
                case NIN_SELECT:
                case NIN_KEYSELECT:
                case WM_LBUTTONDBLCLK:
                    NoteUserActivity();
                    ShowAllWindows();
                    break;
                case WM_CONTEXTMENU: {
                    NoteUserActivity();
                    POINT point{GET_X_LPARAM(wparam), GET_Y_LPARAM(wparam)};
                    ShowTrayMenu(point);
                    break;
                }
                case WM_RBUTTONUP: {
                    NoteUserActivity();
                    POINT point{};
                    GetCursorPos(&point);
                    ShowTrayMenu(point);
                    break;
                }
            }
            return 0;
        case kShowExistingInstance:
            NoteUserActivity();
            ShowAllWindows();
            return 0;
        case kDeleteNote:
            NoteUserActivity();
            DeleteNote(reinterpret_cast<NoteWindow*>(lparam));
            return 0;
        case kNewNote:
            NoteUserActivity();
            CreateNewNote(reinterpret_cast<NoteWindow*>(lparam));
            return 0;
        case WM_DISPLAYCHANGE:
            for (auto& window : windows_) {
                window->ClampToVisibleWorkArea();
                window->ReapplyDesktopMode();
            }
            return 0;
        case WM_TIMER:
            if (wparam == kAutosaveTimer && save_pending_) {
                SaveNow();
            } else if (wparam == kTrayRetryTimer) {
                if (!tray_added_) {
                    AddTrayIcon();
                    if (tray_added_) {
                        KillTimer(controller_window_, kTrayRetryTimer);
                    }
                } else {
                    KillTimer(controller_window_, kTrayRetryTimer);
                }
            } else if (wparam == kIdleTrimTimer) {
                const ULONGLONG now = GetTickCount64();
                if (last_activity_tick_ != 0 && (now - last_activity_tick_ >= kIdleTrimThresholdMilliseconds)) {
                    TrimIdleMemory();
                    last_activity_tick_ = 0;
                }
            }
            return 0;
        case WM_QUERYENDSESSION:
            return SaveNow() ? TRUE : FALSE;
        case WM_CLOSE:
            Exit();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(controller_window_, message, wparam, lparam);
    }
}

}  // namespace desktopnote
