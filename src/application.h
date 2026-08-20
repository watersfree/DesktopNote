#pragma once

#include "data_store.h"

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

namespace desktopnote {

class NoteWindow;

class Application {
public:
    static constexpr UINT kShowExistingInstance = WM_APP + 50;
    static constexpr wchar_t kControllerClassName[] = L"DesktopNote.Controller.v2";

    Application(HINSTANCE instance, std::wstring instance_identifier);
    ~Application();

    int Run();

private:
    static LRESULT CALLBACK ControllerProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleControllerMessage(UINT message, WPARAM wparam, LPARAM lparam);
    bool CreateControllerWindow();
    void CreateInitialWindows();
    NoteWindow* CreateNewNote(NoteWindow* source = nullptr);
    void DeleteNote(NoteWindow* window);
    void ActivateNote(std::size_t index);
    void ScheduleSave();
    bool SaveNow();
    void Exit();

    void AddTrayIcon();
    void RemoveTrayIcon();
    void ShowTrayMenu(POINT point);
    void ShowAllWindows();
    void ToggleAllWindows();
    void SetAllWindowModes(WindowMode mode);
    void DisableAllClickThrough();
    void UnlockAllWindows();
    void RescueAllWindows();
    void OpenDataFolder();
    void TrimIdleMemory();
    void NoteUserActivity();
    void NotifyError(const wchar_t* title, const std::wstring& message);

    HINSTANCE instance_ = nullptr;
    std::wstring instance_identifier_;
    HWND controller_window_ = nullptr;
    HICON tray_icon_ = nullptr;
    UINT taskbar_created_message_ = 0;
    DataStore data_store_;
    AppState loaded_state_;
    std::vector<std::unique_ptr<NoteWindow>> windows_;
    NoteWindow* active_window_ = nullptr;
    ULONGLONG last_activity_tick_ = 0;
    bool tray_added_ = false;
    bool save_pending_ = false;
    bool save_error_reported_ = false;
    bool state_ready_ = false;
    bool exiting_ = false;
};

}  // namespace desktopnote
