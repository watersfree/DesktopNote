#pragma once

#include "app_state.h"

#include <windows.h>
#include <d2d1.h>
#include <richedit.h>
#include <textserv.h>

#include <atomic>
#include <functional>
#include <optional>
#include <string>

namespace desktopnote {

class RichEditHost final : public ITextHost2 {
public:
    RichEditHost(HWND window, std::function<void()> on_change);
    ~RichEditHost();

    RichEditHost(const RichEditHost&) = delete;
    RichEditHost& operator=(const RichEditHost&) = delete;

    bool Initialize(const Appearance& appearance);
    void SetBounds(const RECT& bounds);
    HRESULT Draw(ID2D1RenderTarget* render_target);
    LRESULT ForwardMessage(UINT message, WPARAM wparam, LPARAM lparam);

    bool LoadRtfBase64(const std::string& content);
    std::optional<std::string> SaveRtfBase64();
    std::wstring PlainText() const;
    void ApplyFontFamily(const std::wstring& family);
    void ApplyFontSize(double size_dip);
    void ApplyFontColor(std::uint32_t color);
    void ApplyParagraphSpacing(double spacing_dip);
    void SetReadOnly(bool read_only);
    void ToggleCaretBlink();

    static constexpr UINT_PTR kCaretTimerId = 9101;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HDC TxGetDC() override;
    INT TxReleaseDC(HDC hdc) override;
    BOOL TxShowScrollBar(INT bar, BOOL show) override;
    BOOL TxEnableScrollBar(INT flags, INT arrows) override;
    BOOL TxSetScrollRange(INT bar, LONG minimum, INT maximum, BOOL redraw) override;
    BOOL TxSetScrollPos(INT bar, INT position, BOOL redraw) override;
    void TxInvalidateRect(LPCRECT rect, BOOL erase) override;
    void TxViewChange(BOOL update) override;
    BOOL TxCreateCaret(HBITMAP bitmap, INT width, INT height) override;
    BOOL TxShowCaret(BOOL show) override;
    BOOL TxSetCaretPos(INT x, INT y) override;
    BOOL TxSetTimer(UINT id, UINT timeout) override;
    void TxKillTimer(UINT id) override;
    void TxScrollWindowEx(INT dx, INT dy, LPCRECT scroll, LPCRECT clip, HRGN update_region,
                          LPRECT update, UINT flags) override;
    void TxSetCapture(BOOL capture) override;
    void TxSetFocus() override;
    void TxSetCursor(HCURSOR cursor, BOOL text) override;
    BOOL TxScreenToClient(LPPOINT point) override;
    BOOL TxClientToScreen(LPPOINT point) override;
    HRESULT TxActivate(LONG* old_state) override;
    HRESULT TxDeactivate(LONG new_state) override;
    HRESULT TxGetClientRect(LPRECT rect) override;
    HRESULT TxGetViewInset(LPRECT rect) override;
    HRESULT TxGetCharFormat(const CHARFORMATW** format) override;
    HRESULT TxGetParaFormat(const PARAFORMAT** format) override;
    COLORREF TxGetSysColor(int index) override;
    HRESULT TxGetBackStyle(TXTBACKSTYLE* style) override;
    HRESULT TxGetMaxLength(DWORD* length) override;
    HRESULT TxGetScrollBars(DWORD* scroll_bars) override;
    HRESULT TxGetPasswordChar(TCHAR* character) override;
    HRESULT TxGetAcceleratorPos(LONG* position) override;
    HRESULT TxGetExtent(LPSIZEL extent) override;
    HRESULT OnTxCharFormatChange(const CHARFORMATW* format) override;
    HRESULT OnTxParaFormatChange(const PARAFORMAT* format) override;
    HRESULT TxGetPropertyBits(DWORD mask, DWORD* bits) override;
    HRESULT TxNotify(DWORD notification, void* data) override;
    HIMC TxImmGetContext() override;
    void TxImmReleaseContext(HIMC context) override;
    HRESULT TxGetSelectionBarWidth(LONG* width) override;

    BOOL TxIsDoubleClickPending() override;
    HRESULT TxGetWindow(HWND* window) override;
    HRESULT TxSetForegroundWindow() override;
    HPALETTE TxGetPalette() override;
    HRESULT TxGetEastAsianFlags(LONG* flags) override;
    HCURSOR TxSetCursor2(HCURSOR cursor, BOOL text) override;
    void TxFreeTextServicesNotification() override;
    HRESULT TxGetEditStyle(DWORD item, DWORD* data) override;
    HRESULT TxGetWindowStyles(DWORD* style, DWORD* extended_style) override;
    HRESULT TxShowDropCaret(BOOL show, HDC hdc, LPCRECT rect) override;
    HRESULT TxDestroyCaret() override;
    HRESULT TxGetHorzExtent(LONG* extent) override;

private:
    void UpdateDefaultFormats(const Appearance& appearance);
    void ApplyCharacterFormat(CHARFORMAT2W& format);

    HWND window_ = nullptr;
    HMODULE rich_edit_module_ = nullptr;
    IUnknown* text_unknown_ = nullptr;
    ITextServices2* text_services_ = nullptr;
    const IID* iid_text_host_ = nullptr;
    const IID* iid_text_host2_ = nullptr;
    RECT bounds_{};
    CHARFORMATW character_format_{};
    PARAFORMAT2 paragraph_format_{};
    std::function<void()> on_change_;
    std::atomic<ULONG> references_{1};
    bool suppress_notifications_ = false;
    bool read_only_ = false;
    bool caret_show_ = false;
    bool caret_blink_on_ = false;
    int caret_x_ = 0;
    int caret_y_ = 0;
    int caret_width_ = 2;
    int caret_height_ = 18;
    std::uint32_t font_color_ = 0x1E293B;
};

}  // namespace desktopnote
