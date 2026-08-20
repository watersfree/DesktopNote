#include "rich_edit_host.h"

#include "base64.h"
#include "win_util.h"

#include <imm.h>
#include <oleauto.h>
#include <strsafe.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace desktopnote {
namespace {

struct InputStream {
    const std::vector<std::uint8_t>* bytes = nullptr;
    std::size_t position = 0;
};

struct OutputStream {
    std::vector<std::uint8_t> bytes;
    bool failed = false;
};

DWORD CALLBACK ReadStream(DWORD_PTR cookie, LPBYTE buffer, LONG capacity, LONG* copied) {
    auto& stream = *reinterpret_cast<InputStream*>(cookie);
    const auto remaining = stream.bytes->size() - stream.position;
    const auto count = std::min<std::size_t>(remaining, static_cast<std::size_t>(capacity));
    if (count > 0) std::memcpy(buffer, stream.bytes->data() + stream.position, count);
    stream.position += count;
    *copied = static_cast<LONG>(count);
    return 0;
}

DWORD CALLBACK WriteStream(DWORD_PTR cookie, LPBYTE buffer, LONG count, LONG* copied) noexcept {
    auto& stream = *reinterpret_cast<OutputStream*>(cookie);
    try {
        stream.bytes.insert(stream.bytes.end(), buffer, buffer + count);
        *copied = count;
        return 0;
    } catch (...) {
        stream.failed = true;
        *copied = 0;
        return ERROR_OUTOFMEMORY;
    }
}

}  // namespace

RichEditHost::RichEditHost(HWND window, std::function<void()> on_change)
    : window_(window), on_change_(std::move(on_change)) {}

RichEditHost::~RichEditHost() {
    if (text_services_) {
        text_services_->OnTxUIDeactivate();
        text_services_->OnTxInPlaceDeactivate();
        text_services_->Release();
        text_services_ = nullptr;
    }
    if (text_unknown_) {
        text_unknown_->Release();
        text_unknown_ = nullptr;
    }
    if (rich_edit_module_) {
        FreeLibrary(rich_edit_module_);
        rich_edit_module_ = nullptr;
    }
}

bool RichEditHost::Initialize(const Appearance& appearance) {
    rich_edit_module_ = LoadLibraryW(L"Msftedit.dll");
    if (!rich_edit_module_) return false;

    const auto create_text_services = reinterpret_cast<PCreateTextServices>(
        GetProcAddress(rich_edit_module_, "CreateTextServices"));
    const auto iid_text_services2 = reinterpret_cast<const IID*>(
        GetProcAddress(rich_edit_module_, "IID_ITextServices2"));
    const auto iid_text_services = reinterpret_cast<const IID*>(
        GetProcAddress(rich_edit_module_, "IID_ITextServices"));
    iid_text_host_ = reinterpret_cast<const IID*>(GetProcAddress(rich_edit_module_, "IID_ITextHost"));
    iid_text_host2_ = reinterpret_cast<const IID*>(GetProcAddress(rich_edit_module_, "IID_ITextHost2"));
    if (!create_text_services || (!iid_text_services2 && !iid_text_services) || !iid_text_host_ || !iid_text_host2_) {
        return false;
    }

    UpdateDefaultFormats(appearance);
    if (FAILED(create_text_services(nullptr, this, &text_unknown_)) || !text_unknown_) return false;
    const IID* target_iid = iid_text_services2 ? iid_text_services2 : iid_text_services;
    if (!target_iid || FAILED(text_unknown_->QueryInterface(*target_iid, reinterpret_cast<void**>(&text_services_))) ||
        !text_services_) {
        return false;
    }

    const DWORD mask = TXTBIT_RICHTEXT | TXTBIT_MULTILINE | TXTBIT_WORDWRAP |
                       TXTBIT_SAVESELECTION | TXTBIT_ADVANCEDINPUT | TXTBIT_D2DDWRITE |
                       TXTBIT_D2DPIXELSNAPPED | TXTBIT_BACKSTYLECHANGE |
                       TXTBIT_CHARFORMATCHANGE | TXTBIT_PARAFORMATCHANGE;
    text_services_->OnTxPropertyBitsChange(mask, mask);
    text_services_->OnTxInPlaceActivate(&bounds_);
    text_services_->OnTxUIActivate();

    font_color_ = appearance.font_color;
    LRESULT ignored = 0;
    text_services_->TxSendMessage(EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE, &ignored);
    text_services_->TxSendMessage(EM_SETBKGNDCOLOR, FALSE, RgbToColorRef(appearance.background_color), &ignored);
    text_services_->TxSendMessage(EM_SETCHARFORMAT, SCF_DEFAULT,
                                  reinterpret_cast<LPARAM>(&character_format_), &ignored);
    text_services_->TxSendMessage(EM_SETPARAFORMAT, 0,
                                  reinterpret_cast<LPARAM>(&paragraph_format_), &ignored);
    return true;
}

void RichEditHost::SetBounds(const RECT& bounds) {
    bounds_ = bounds;
    if (text_services_) {
        text_services_->OnTxInPlaceActivate(&bounds_);
        text_services_->OnTxPropertyBitsChange(TXTBIT_CLIENTRECTCHANGE, TXTBIT_CLIENTRECTCHANGE);
    }
}

HRESULT RichEditHost::Draw(ID2D1RenderTarget* render_target) {
    if (!text_services_ || !render_target) return E_POINTER;
    const RECTL bounds{bounds_.left, bounds_.top, bounds_.right, bounds_.bottom};
    const HRESULT hr = text_services_->TxDrawD2D(render_target, &bounds, nullptr, TXTVIEW_ACTIVE);
    const bool has_focus = (GetFocus() == window_);
    if (has_focus && caret_show_ && caret_blink_on_ && !read_only_) {
        const UINT dpi = DpiForWindowOrSystem(window_);
        const float pixel_to_dip = 96.0F / static_cast<float>(dpi ? dpi : 96);
        const float x = static_cast<float>(caret_x_) * pixel_to_dip;
        const float y = static_cast<float>(caret_y_) * pixel_to_dip;
        const float w = std::max(1.8F, static_cast<float>(caret_width_) * pixel_to_dip);
        const float h = std::max(14.0F, static_cast<float>(caret_height_) * pixel_to_dip);
        ID2D1SolidColorBrush* caret_brush = nullptr;
        if (SUCCEEDED(render_target->CreateSolidColorBrush(
                D2DColor(font_color_, 0.95F), &caret_brush))) {
            render_target->FillRectangle(D2D1::RectF(x, y, x + w, y + h), caret_brush);
            caret_brush->Release();
        }
    }
    return hr;
}

LRESULT RichEditHost::ForwardMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (!text_services_) return 0;
    if (message == WM_KEYDOWN || message == WM_CHAR || message == WM_IME_CHAR ||
        message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_SETFOCUS) {
        caret_blink_on_ = true;
    } else if (message == WM_KILLFOCUS) {
        caret_show_ = false;
        caret_blink_on_ = false;
        KillTimer(window_, kCaretTimerId);
        SendMessageW(window_, WM_PAINT, 0, 0);
    }
    LRESULT result = 0;
    text_services_->TxSendMessage(message, wparam, lparam, &result);
    return result;
}

void RichEditHost::ToggleCaretBlink() {
    if (GetFocus() == window_ && caret_show_ && !read_only_) {
        caret_blink_on_ = !caret_blink_on_;
        SendMessageW(window_, WM_PAINT, 0, 0);
    }
}

bool RichEditHost::LoadRtfBase64(const std::string& content) {
    if (!text_services_) return false;
    suppress_notifications_ = true;
    LRESULT result = 0;
    if (content.empty()) {
        text_services_->TxSetText(L"");
        suppress_notifications_ = false;
        return true;
    }
    try {
        const auto bytes = DecodeBase64(content);
        InputStream input{&bytes, 0};
        EDITSTREAM stream{reinterpret_cast<DWORD_PTR>(&input), 0, ReadStream};
        text_services_->TxSendMessage(EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&stream), &result);
        suppress_notifications_ = false;
        return stream.dwError == 0;
    } catch (...) {
        text_services_->TxSetText(L"");
        suppress_notifications_ = false;
        return false;
    }
}

std::optional<std::string> RichEditHost::SaveRtfBase64() {
    if (!text_services_) return std::nullopt;
    OutputStream output;
    EDITSTREAM stream{reinterpret_cast<DWORD_PTR>(&output), 0, WriteStream};
    LRESULT ignored = 0;
    const HRESULT result = text_services_->TxSendMessage(
        EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&stream), &ignored);
    if (FAILED(result) || stream.dwError != 0 || output.failed) return std::nullopt;
    try {
        return EncodeBase64(output.bytes);
    } catch (...) {
        return std::nullopt;
    }
}

std::wstring RichEditHost::PlainText() const {
    if (!text_services_) return {};
    BSTR text = nullptr;
    if (FAILED(text_services_->TxGetText(&text)) || !text) return {};
    std::wstring result(text, SysStringLen(text));
    SysFreeString(text);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
    return result;
}

void RichEditHost::ApplyCharacterFormat(CHARFORMAT2W& format) {
    if (!text_services_) return;
    LRESULT ignored = 0;
    const HRESULT result = text_services_->TxSendMessage(
        EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format), &ignored);
    if (SUCCEEDED(result) && on_change_) on_change_();
    InvalidateRect(window_, &bounds_, FALSE);
    SendMessageW(window_, WM_PAINT, 0, 0);
}

void RichEditHost::ApplyFontFamily(const std::wstring& family) {
    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_FACE;
    StringCchCopyW(format.szFaceName, LF_FACESIZE, family.c_str());
    ApplyCharacterFormat(format);
}

void RichEditHost::ApplyFontSize(double size_dip) {
    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_SIZE;
    format.yHeight = static_cast<LONG>(std::clamp(size_dip, 8.0, 96.0) * 15.0);
    ApplyCharacterFormat(format);
}

void RichEditHost::ApplyFontColor(std::uint32_t color) {
    font_color_ = color;
    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR;
    format.crTextColor = RgbToColorRef(color);
    ApplyCharacterFormat(format);
}

void RichEditHost::ApplyParagraphSpacing(double spacing_dip) {
    if (!text_services_) return;
    const LONG spacing_twips = static_cast<LONG>(std::clamp(spacing_dip, 0.0, 48.0) * 15.0);
    PARAFORMAT2 format{};
    format.cbSize = sizeof(format);
    format.dwMask = PFM_SPACEAFTER;
    format.dySpaceAfter = spacing_twips;
    LRESULT ignored = 0;
    CHARRANGE previous_selection{};
    text_services_->TxSendMessage(
        EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&previous_selection), &ignored);
    CHARRANGE all_text{0, -1};
    text_services_->TxSendMessage(
        EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all_text), &ignored);
    const HRESULT result = text_services_->TxSendMessage(
        EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&format), &ignored);
    text_services_->TxSendMessage(
        EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&previous_selection), &ignored);
    paragraph_format_.dwMask |= PFM_SPACEAFTER;
    paragraph_format_.dySpaceAfter = spacing_twips;
    if (SUCCEEDED(result) && on_change_) on_change_();
    InvalidateRect(window_, &bounds_, FALSE);
    SendMessageW(window_, WM_PAINT, 0, 0);
}

void RichEditHost::SetReadOnly(bool read_only) {
    read_only_ = read_only;
    LRESULT ignored = 0;
    if (text_services_) text_services_->TxSendMessage(EM_SETREADONLY, read_only, 0, &ignored);
}

void RichEditHost::UpdateDefaultFormats(const Appearance& appearance) {
    character_format_ = {};
    character_format_.cbSize = sizeof(character_format_);
    character_format_.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR;
    character_format_.yHeight = static_cast<LONG>(appearance.font_size_dip * 15.0);
    character_format_.crTextColor = RgbToColorRef(appearance.font_color);
    StringCchCopyW(character_format_.szFaceName, LF_FACESIZE, appearance.font_family.c_str());

    paragraph_format_ = {};
    paragraph_format_.cbSize = sizeof(paragraph_format_);
    paragraph_format_.dwMask = PFM_SPACEAFTER;
    paragraph_format_.dySpaceAfter = static_cast<LONG>(appearance.paragraph_spacing_dip * 15.0);
}

HRESULT RichEditHost::QueryInterface(REFIID riid, void** object) {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || (iid_text_host_ && IsEqualIID(riid, *iid_text_host_)) ||
        (iid_text_host2_ && IsEqualIID(riid, *iid_text_host2_))) {
        *object = static_cast<ITextHost2*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG RichEditHost::AddRef() { return ++references_; }
ULONG RichEditHost::Release() { return references_ > 1 ? --references_ : 1; }

HDC RichEditHost::TxGetDC() { return GetDC(window_); }
INT RichEditHost::TxReleaseDC(HDC hdc) { return ReleaseDC(window_, hdc); }
BOOL RichEditHost::TxShowScrollBar(INT, BOOL) { return TRUE; }
BOOL RichEditHost::TxEnableScrollBar(INT, INT) { return TRUE; }
BOOL RichEditHost::TxSetScrollRange(INT, LONG, INT, BOOL) { return TRUE; }
BOOL RichEditHost::TxSetScrollPos(INT, INT, BOOL) { return TRUE; }
void RichEditHost::TxInvalidateRect(LPCRECT rect, BOOL erase) {
    InvalidateRect(window_, rect, erase);
    SendMessageW(window_, WM_PAINT, 0, 0);
}
void RichEditHost::TxViewChange(BOOL update) {
    InvalidateRect(window_, &bounds_, update);
    SendMessageW(window_, WM_PAINT, 0, 0);
}
BOOL RichEditHost::TxCreateCaret(HBITMAP, INT width, INT height) {
    caret_width_ = std::max(width, 2);
    caret_height_ = std::max(height, 14);
    return TRUE;
}
BOOL RichEditHost::TxShowCaret(BOOL show) {
    caret_show_ = (show != FALSE);
    caret_blink_on_ = caret_show_;
    if (caret_show_) {
        UINT blink_time = GetCaretBlinkTime();
        if (blink_time == 0) blink_time = 530;
        SetTimer(window_, kCaretTimerId, blink_time, nullptr);
    } else {
        KillTimer(window_, kCaretTimerId);
    }
    SendMessageW(window_, WM_PAINT, 0, 0);
    return TRUE;
}
BOOL RichEditHost::TxSetCaretPos(INT x, INT y) {
    caret_x_ = x;
    caret_y_ = y;
    caret_blink_on_ = true;
    SendMessageW(window_, WM_PAINT, 0, 0);
    return TRUE;
}
BOOL RichEditHost::TxSetTimer(UINT id, UINT timeout) { return SetTimer(window_, id, timeout, nullptr) != 0; }
void RichEditHost::TxKillTimer(UINT id) { KillTimer(window_, id); }
void RichEditHost::TxScrollWindowEx(INT dx, INT dy, LPCRECT scroll, LPCRECT clip, HRGN region,
                                    LPRECT update, UINT flags) {
    ScrollWindowEx(window_, dx, dy, scroll, clip, region, update, flags);
}
void RichEditHost::TxSetCapture(BOOL capture) {
    if (capture) SetCapture(window_);
    else ReleaseCapture();
}
void RichEditHost::TxSetFocus() { SetFocus(window_); }
void RichEditHost::TxSetCursor(HCURSOR cursor, BOOL) { SetCursor(cursor); }
BOOL RichEditHost::TxScreenToClient(LPPOINT point) { return ScreenToClient(window_, point); }
BOOL RichEditHost::TxClientToScreen(LPPOINT point) { return ClientToScreen(window_, point); }
HRESULT RichEditHost::TxActivate(LONG* old_state) { if (old_state) *old_state = 0; return S_OK; }
HRESULT RichEditHost::TxDeactivate(LONG) { return S_OK; }
HRESULT RichEditHost::TxGetClientRect(LPRECT rect) { if (!rect) return E_POINTER; *rect = bounds_; return S_OK; }
HRESULT RichEditHost::TxGetViewInset(LPRECT rect) { if (!rect) return E_POINTER; SetRectEmpty(rect); return S_OK; }
HRESULT RichEditHost::TxGetCharFormat(const CHARFORMATW** format) {
    if (!format) return E_POINTER; *format = &character_format_; return S_OK;
}
HRESULT RichEditHost::TxGetParaFormat(const PARAFORMAT** format) {
    if (!format) return E_POINTER; *format = reinterpret_cast<const PARAFORMAT*>(&paragraph_format_); return S_OK;
}
COLORREF RichEditHost::TxGetSysColor(int index) { return GetSysColor(index); }
HRESULT RichEditHost::TxGetBackStyle(TXTBACKSTYLE* style) {
    if (!style) return E_POINTER; *style = TXTBACK_TRANSPARENT; return S_OK;
}
HRESULT RichEditHost::TxGetMaxLength(DWORD* length) { if (!length) return E_POINTER; *length = 0x7FFFFFFE; return S_OK; }
HRESULT RichEditHost::TxGetScrollBars(DWORD* bars) { if (!bars) return E_POINTER; *bars = 0; return S_OK; }
HRESULT RichEditHost::TxGetPasswordChar(TCHAR* character) { if (!character) return E_POINTER; *character = 0; return S_OK; }
HRESULT RichEditHost::TxGetAcceleratorPos(LONG* position) { if (!position) return E_POINTER; *position = -1; return S_OK; }
HRESULT RichEditHost::TxGetExtent(LPSIZEL extent) {
    if (!extent) return E_POINTER;
    const UINT dpi = DpiForWindowOrSystem(window_);
    extent->cx = MulDiv(bounds_.right - bounds_.left, 2540, dpi);
    extent->cy = MulDiv(bounds_.bottom - bounds_.top, 2540, dpi);
    return S_OK;
}
HRESULT RichEditHost::OnTxCharFormatChange(const CHARFORMATW* format) {
    if (format) character_format_ = *format; return S_OK;
}
HRESULT RichEditHost::OnTxParaFormatChange(const PARAFORMAT* format) {
    if (format) std::memcpy(&paragraph_format_, format, std::min<size_t>(format->cbSize, sizeof(paragraph_format_)));
    return S_OK;
}
HRESULT RichEditHost::TxGetPropertyBits(DWORD mask, DWORD* bits) {
    if (!bits) return E_POINTER;
    DWORD value = TXTBIT_RICHTEXT | TXTBIT_MULTILINE | TXTBIT_WORDWRAP |
                  TXTBIT_SAVESELECTION | TXTBIT_ADVANCEDINPUT | TXTBIT_D2DDWRITE |
                  TXTBIT_D2DPIXELSNAPPED;
    if (read_only_) value |= TXTBIT_READONLY;
    *bits = value & mask;
    return S_OK;
}
HRESULT RichEditHost::TxNotify(DWORD notification, void*) {
    if (!suppress_notifications_ && notification == EN_CHANGE && on_change_) on_change_();
    return S_OK;
}
HIMC RichEditHost::TxImmGetContext() { return ImmGetContext(window_); }
void RichEditHost::TxImmReleaseContext(HIMC context) { ImmReleaseContext(window_, context); }
HRESULT RichEditHost::TxGetSelectionBarWidth(LONG* width) { if (!width) return E_POINTER; *width = 0; return S_OK; }

BOOL RichEditHost::TxIsDoubleClickPending() {
    MSG message{};
    return PeekMessageW(&message, window_, WM_LBUTTONDBLCLK, WM_LBUTTONDBLCLK, PM_NOREMOVE);
}
HRESULT RichEditHost::TxGetWindow(HWND* window) { if (!window) return E_POINTER; *window = window_; return S_OK; }
HRESULT RichEditHost::TxSetForegroundWindow() { return SetForegroundWindow(window_) ? S_OK : S_FALSE; }
HPALETTE RichEditHost::TxGetPalette() { return nullptr; }
HRESULT RichEditHost::TxGetEastAsianFlags(LONG* flags) { if (!flags) return E_POINTER; *flags = 0; return S_OK; }
HCURSOR RichEditHost::TxSetCursor2(HCURSOR cursor, BOOL text) { TxSetCursor(cursor, text); return cursor; }
void RichEditHost::TxFreeTextServicesNotification() {}
HRESULT RichEditHost::TxGetEditStyle(DWORD, DWORD* data) { if (!data) return E_POINTER; *data = 0; return S_OK; }
HRESULT RichEditHost::TxGetWindowStyles(DWORD* style, DWORD* extended_style) {
    if (!style || !extended_style) return E_POINTER;
    *style = WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
    *extended_style = 0;
    return S_OK;
}
HRESULT RichEditHost::TxShowDropCaret(BOOL show, HDC, LPCRECT) { return TxShowCaret(show) ? S_OK : S_FALSE; }
HRESULT RichEditHost::TxDestroyCaret() {
    caret_show_ = false;
    caret_blink_on_ = false;
    KillTimer(window_, kCaretTimerId);
    SendMessageW(window_, WM_PAINT, 0, 0);
    return S_OK;
}
HRESULT RichEditHost::TxGetHorzExtent(LONG* extent) { if (!extent) return E_POINTER; *extent = 0; return S_OK; }

}  // namespace desktopnote
