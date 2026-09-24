#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <wincodec.h>
#include <objidl.h>

#include "obj2vxp2_core.h"

#include <cerrno>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"4DMMObj2Vxp2V4";
constexpr wchar_t kWindowTitle[] = L"4DMM Model to VXP2";

enum ControlId {
    IDC_OBJ = 1001,
    IDC_OBJ_BROWSE,
    IDC_OUTPUT,
    IDC_OUTPUT_BROWSE,
    IDC_NAME,
    IDC_TYPE,
    IDC_SCALE,
    IDC_TEXTURE_V,
    IDC_TWO_SIDED,
    IDC_CENTER_BOTTOM,
    IDC_CONVERT,
    IDC_STATUS,
};

struct GuiState {
    HWND hwnd = nullptr;
    HWND obj = nullptr;
    HWND output = nullptr;
    HWND name = nullptr;
    HWND type = nullptr;
    HWND scale = nullptr;
    HWND textureV = nullptr;
    HWND twoSided = nullptr;
    HWND centerBottom = nullptr;
    HWND status = nullptr;
    HFONT font = nullptr;
    double guiScale = 1.0;
    std::filesystem::path multiLogPath;
};

std::string Utf8FromWide(const std::wstring &value)
{
    if (value.empty())
        return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring WideFromUtf8(const std::string &value)
{
    if (value.empty())
        return {};
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0)
        return std::wstring(value.begin(), value.end());
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        out.data(), needed);
    return out;
}

std::wstring GetText(HWND hwnd)
{
    const int count = GetWindowTextLengthW(hwnd);
    if (count <= 0)
        return {};
    std::wstring text(static_cast<size_t>(count + 1), L'\0');
    const int copied = GetWindowTextW(hwnd, text.data(), count + 1);
    text.resize(copied > 0 ? static_cast<size_t>(copied) : 0);
    return text;
}

void SetText(HWND hwnd, const std::wstring &text)
{
    SetWindowTextW(hwnd, text.c_str());
}

class MultiLogger {
  public:
    explicit MultiLogger(const std::filesystem::path &path) : path_(path)
    {
        if (path_.empty())
            return;
        std::error_code ec;
        if (!path_.parent_path().empty())
            std::filesystem::create_directories(path_.parent_path(), ec);
        stream_.open(path_, std::ios::out | std::ios::app | std::ios::binary);
        if (stream_) {
            SYSTEMTIME st{};
            GetLocalTime(&st);
            stream_ << "\n=== obj2vxp2 v" << obj2vxp2::kVersion << " "
                    << st.wYear << '-';
            if (st.wMonth < 10) stream_ << '0';
            stream_ << st.wMonth << '-';
            if (st.wDay < 10) stream_ << '0';
            stream_ << st.wDay << ' ';
            if (st.wHour < 10) stream_ << '0';
            stream_ << st.wHour << ':';
            if (st.wMinute < 10) stream_ << '0';
            stream_ << st.wMinute << ':';
            if (st.wSecond < 10) stream_ << '0';
            stream_ << st.wSecond << "." << st.wMilliseconds << " ===\n";
            stream_.flush();
        }
    }

    void Write(const std::string &line)
    {
        if (!stream_)
            return;
        SYSTEMTIME st{};
        GetLocalTime(&st);
        stream_ << '[';
        if (st.wHour < 10) stream_ << '0';
        stream_ << st.wHour << ':';
        if (st.wMinute < 10) stream_ << '0';
        stream_ << st.wMinute << ':';
        if (st.wSecond < 10) stream_ << '0';
        stream_ << st.wSecond << '.' << st.wMilliseconds << "] " << line << '\n';
        stream_.flush();
    }

  private:
    std::filesystem::path path_;
    std::ofstream stream_;
};

template <typename T>
void SafeRelease(T *&p)
{
    if (p != nullptr) {
        p->Release();
        p = nullptr;
    }
}

bool EncodeWicSourceToPng(IWICImagingFactory *factory, IWICBitmapSource *source,
                            std::vector<uint8_t> *out, HRESULT *result)
{
    if (factory == nullptr || source == nullptr || out == nullptr)
        return false;
    out->clear();

    IWICFormatConverter *converter = nullptr;
    IWICBitmapEncoder *encoder = nullptr;
    IWICBitmapFrameEncode *encodedFrame = nullptr;
    IPropertyBag2 *properties = nullptr;
    IStream *stream = nullptr;
    HGLOBAL hglobal = nullptr;
    HRESULT hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
        hr = converter->Initialize(source, GUID_WICPixelFormat32bppBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);

    UINT width = 0, height = 0;
    if (SUCCEEDED(hr))
        hr = converter->GetSize(&width, &height);
    if (SUCCEEDED(hr))
        hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    if (SUCCEEDED(hr))
        hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (SUCCEEDED(hr))
        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr))
        hr = encoder->CreateNewFrame(&encodedFrame, &properties);
    if (SUCCEEDED(hr))
        hr = encodedFrame->Initialize(properties);
    if (SUCCEEDED(hr))
        hr = encodedFrame->SetSize(width, height);
    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr))
        hr = encodedFrame->SetPixelFormat(&pixelFormat);
    if (SUCCEEDED(hr) && !IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppBGRA))
        hr = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    if (SUCCEEDED(hr))
        hr = encodedFrame->WriteSource(converter, nullptr);
    if (SUCCEEDED(hr))
        hr = encodedFrame->Commit();
    if (SUCCEEDED(hr))
        hr = encoder->Commit();
    if (SUCCEEDED(hr))
        hr = GetHGlobalFromStream(stream, &hglobal);
    bool ok = false;
    if (SUCCEEDED(hr) && hglobal != nullptr) {
        STATSTG stat{};
        hr = stream->Stat(&stat, STATFLAG_NONAME);
        const ULONGLONG size64 = SUCCEEDED(hr) ? stat.cbSize.QuadPart : 0;
        const void *data = GlobalLock(hglobal);
        if (data != nullptr && size64 > 0 && size64 <= static_cast<ULONGLONG>(std::numeric_limits<size_t>::max())) {
            const auto *bytes = static_cast<const uint8_t *>(data);
            out->assign(bytes, bytes + static_cast<size_t>(size64));
            ok = true;
        }
        if (data != nullptr)
            GlobalUnlock(hglobal);
    }

    SafeRelease(properties);
    SafeRelease(encodedFrame);
    SafeRelease(encoder);
    SafeRelease(converter);
    SafeRelease(stream);
    if (result != nullptr)
        *result = hr;
    return ok;
}

bool ConvertImageWic(const std::filesystem::path &path, std::vector<uint8_t> *out, std::string *error)
{
    if (out == nullptr)
        return false;
    IWICImagingFactory *factory = nullptr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *sourceFrame = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr))
        hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr))
        hr = decoder->GetFrame(0, &sourceFrame);
    const bool ok = SUCCEEDED(hr) && EncodeWicSourceToPng(factory, sourceFrame, out, &hr);
    SafeRelease(sourceFrame);
    SafeRelease(decoder);
    SafeRelease(factory);
    if (!ok && error != nullptr) {
        std::ostringstream ss;
        ss << "Windows Imaging Component failed (HRESULT 0x" << std::hex
           << static_cast<unsigned long>(hr) << ")";
        *error = ss.str();
    }
    return ok;
}

bool ConvertImageMemoryWic(const std::vector<uint8_t> &input, std::vector<uint8_t> *out, std::string *error)
{
    if (out == nullptr || input.empty())
        return false;
    IWICImagingFactory *factory = nullptr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *sourceFrame = nullptr;
    IStream *inputStream = nullptr;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, input.size());
    HRESULT hr = memory != nullptr ? S_OK : E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) {
        void *dst = GlobalLock(memory);
        if (dst == nullptr)
            hr = E_OUTOFMEMORY;
        else {
            std::memcpy(dst, input.data(), input.size());
            GlobalUnlock(memory);
        }
    }
    if (SUCCEEDED(hr)) {
        hr = CreateStreamOnHGlobal(memory, TRUE, &inputStream);
        if (SUCCEEDED(hr))
            memory = nullptr; // stream owns it now
    }
    if (SUCCEEDED(hr))
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr))
        hr = factory->CreateDecoderFromStream(inputStream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr))
        hr = decoder->GetFrame(0, &sourceFrame);
    const bool ok = SUCCEEDED(hr) && EncodeWicSourceToPng(factory, sourceFrame, out, &hr);
    SafeRelease(sourceFrame);
    SafeRelease(decoder);
    SafeRelease(factory);
    SafeRelease(inputStream);
    if (memory != nullptr)
        GlobalFree(memory);
    if (!ok && error != nullptr) {
        std::ostringstream ss;
        ss << "Windows Imaging Component memory decode failed (HRESULT 0x" << std::hex
           << static_cast<unsigned long>(hr) << ")";
        *error = ss.str();
    }
    return ok;
}

bool ParseDouble(const std::wstring &text, double *value)
{
    if (value == nullptr)
        return false;
    wchar_t *end = nullptr;
    errno = 0;
    const double parsed = std::wcstod(text.c_str(), &end);
    if (end == text.c_str() || (end != nullptr && *end != L'\0') || errno == ERANGE || !std::isfinite(parsed))
        return false;
    *value = parsed;
    return true;
}

bool RunConversion(const std::filesystem::path &inputPath, const std::filesystem::path &outputPath,
                   const std::wstring &name, obj2vxp2::AssetKind kind, double scale,
                   obj2vxp2::TextureVMode textureVMode, bool twoSidedFaces, bool centerBottom,
                   const std::filesystem::path &multiLogPath, obj2vxp2::Stats *stats, std::string *error)
{
    MultiLogger logger(multiLogPath);
    obj2vxp2::ConvertOptions options;
    options.inputPath = inputPath;
    options.outputPath = outputPath;
    options.name = Utf8FromWide(name);
    options.kind = kind;
    options.scale = scale;
    options.textureVMode = textureVMode;
    options.twoSidedFaces = twoSidedFaces;
    options.centerBottom = centerBottom;
    options.imageConverter = ConvertImageWic;
    options.imageMemoryConverter = ConvertImageMemoryWic;
    if (!multiLogPath.empty())
        options.log = [&logger](const std::string &line) { logger.Write(line); };
    return obj2vxp2::Convert(options, stats, error);
}

std::filesystem::path BrowseOpenModel(HWND owner)
{
    wchar_t file[32768] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"3D models (*.obj;*.glb;*.fbx;*.zip)\0*.obj;*.glb;*.fbx;*.zip\0Wavefront OBJ (*.obj)\0*.obj\0glTF Binary (*.glb)\0*.glb\0FBX (*.fbx)\0*.fbx\0FBX ZIP bundle (*.zip)\0*.zip\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file));
    ofn.lpstrTitle = L"Open 3D model or FBX ZIP bundle";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? std::filesystem::path(file) : std::filesystem::path();
}

std::filesystem::path BrowseSaveVxp2(HWND owner, const std::wstring &initial)
{
    wchar_t file[32768] = {};
    if (!initial.empty())
        wcsncpy_s(file, initial.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"4DMM VXP2 (*.vxp2)\0*.vxp2\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file));
    ofn.lpstrTitle = L"Save VXP2";
    ofn.lpstrDefExt = L"vxp2";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
    return GetSaveFileNameW(&ofn) ? std::filesystem::path(file) : std::filesystem::path();
}

void ApplyFont(HWND hwnd, HFONT font)
{
    if (hwnd != nullptr)
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

int Ui(const GuiState *state, int value)
{
    if (state == nullptr || !(state->guiScale > 0.0))
        return value;
    return static_cast<int>(std::lround(static_cast<double>(value) * state->guiScale));
}

HWND Label(HWND parent, const wchar_t *text, int x, int y, int w, int h, HFONT font)
{
    HWND control = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                                   x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    ApplyFont(control, font);
    return control;
}

HWND Edit(HWND parent, int id, int x, int y, int w, int h, HFONT font, const wchar_t *text = L"")
{
    HWND control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                   x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    ApplyFont(control, font);
    return control;
}

HWND Button(HWND parent, int id, const wchar_t *text, int x, int y, int w, int h, HFONT font, DWORD extra = 0)
{
    HWND control = CreateWindowExW(0, L"BUTTON", text,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | extra,
                                   x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    ApplyFont(control, font);
    return control;
}

void SetStatus(GuiState *state, const wchar_t *text)
{
    if (state != nullptr && state->status != nullptr)
        SetWindowTextW(state->status, text);
}

std::wstring InputStem(const std::filesystem::path &path)
{
    std::filesystem::path stem = path.stem();
    if (path.extension() == L".zip" || path.extension() == L".ZIP") {
        const std::wstring nestedExt = stem.extension().wstring();
        if (_wcsicmp(nestedExt.c_str(), L".fbx") == 0)
            stem = stem.stem();
    }
    return stem.wstring();
}

std::filesystem::path DefaultOutputPath(const std::filesystem::path &path)
{
    std::filesystem::path out = path.parent_path() / InputStem(path);
    out += L".vxp2";
    return out;
}

void ChooseObj(GuiState *state)
{
    const std::filesystem::path path = BrowseOpenModel(state->hwnd);
    if (path.empty())
        return;
    SetText(state->obj, path.wstring());
    if (GetText(state->name).empty())
        SetText(state->name, InputStem(path));
    if (GetText(state->output).empty()) {
        SetText(state->output, DefaultOutputPath(path).wstring());
    }
}

void ChooseOutput(GuiState *state)
{
    const std::filesystem::path path = BrowseSaveVxp2(state->hwnd, GetText(state->output));
    if (!path.empty())
        SetText(state->output, path.wstring());
}

void ConvertGui(GuiState *state)
{
    const std::wstring inputText = GetText(state->obj);
    const std::wstring outText = GetText(state->output);
    const std::wstring name = GetText(state->name);
    const std::wstring scaleText = GetText(state->scale);
    if (inputText.empty() || outText.empty() || name.empty()) {
        MessageBoxW(state->hwnd, L"Model input, output VXP2, and Name are required.", kWindowTitle, MB_ICONERROR | MB_OK);
        return;
    }
    double scale = 1.0;
    if (!ParseDouble(scaleText, &scale) || !(scale > 0.0)) {
        MessageBoxW(state->hwnd, L"Scale must be a number greater than zero.", kWindowTitle, MB_ICONERROR | MB_OK);
        return;
    }
    const LRESULT typeIndex = SendMessageW(state->type, CB_GETCURSEL, 0, 0);
    const obj2vxp2::AssetKind kind = typeIndex == 1 ? obj2vxp2::AssetKind::Actor : obj2vxp2::AssetKind::Prop;
    const LRESULT textureVIndex = SendMessageW(state->textureV, CB_GETCURSEL, 0, 0);
    obj2vxp2::TextureVMode textureVMode = obj2vxp2::TextureVMode::Auto;
    if (textureVIndex == 1)
        textureVMode = obj2vxp2::TextureVMode::Flip;
    else if (textureVIndex == 2)
        textureVMode = obj2vxp2::TextureVMode::Preserve;
    const bool twoSidedFaces = SendMessageW(state->twoSided, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool centerBottom = SendMessageW(state->centerBottom, BM_GETCHECK, 0, 0) == BST_CHECKED;

    EnableWindow(GetDlgItem(state->hwnd, IDC_CONVERT), FALSE);
    SetStatus(state, L"Converting...");
    UpdateWindow(state->hwnd);

    obj2vxp2::Stats stats;
    std::string error;
    const bool ok = RunConversion(std::filesystem::path(inputText), std::filesystem::path(outText), name,
                                  kind, scale, textureVMode, twoSidedFaces, centerBottom, state->multiLogPath, &stats, &error);
    EnableWindow(GetDlgItem(state->hwnd, IDC_CONVERT), TRUE);
    if (!ok) {
        SetStatus(state, L"Failed.");
        MessageBoxW(state->hwnd, WideFromUtf8(error).c_str(), kWindowTitle, MB_ICONERROR | MB_OK);
        return;
    }

    SetStatus(state, L"Done.");
    std::wostringstream message;
    message << L"Created " << outText << L"\n\n" << stats.triangles << L" triangles, "
            << stats.bmdlParts << L" BMDL section(s), " << stats.textures << L" truecolor texture(s).";
    MessageBoxW(state->hwnd, message.str().c_str(), kWindowTitle, MB_ICONINFORMATION | MB_OK);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    GuiState *state = reinterpret_cast<GuiState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lParam);
        state = static_cast<GuiState *>(create->lpCreateParams);
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    switch (msg) {
    case WM_CREATE: {
        NONCLIENTMETRICSW ncm{};
        ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        state->font = CreateFontIndirectW(&ncm.lfMessageFont);
        Label(hwnd, L"Model file:", Ui(state, 14), Ui(state, 17), Ui(state, 95), Ui(state, 22), state->font);
        state->obj = Edit(hwnd, IDC_OBJ, Ui(state, 112), Ui(state, 14), Ui(state, 485), Ui(state, 24), state->font);
        Button(hwnd, IDC_OBJ_BROWSE, L"Browse...", Ui(state, 608), Ui(state, 13), Ui(state, 86), Ui(state, 26), state->font);
        Label(hwnd, L"Output VXP2:", Ui(state, 14), Ui(state, 53), Ui(state, 95), Ui(state, 22), state->font);
        state->output = Edit(hwnd, IDC_OUTPUT, Ui(state, 112), Ui(state, 50), Ui(state, 485), Ui(state, 24), state->font);
        Button(hwnd, IDC_OUTPUT_BROWSE, L"Browse...", Ui(state, 608), Ui(state, 49), Ui(state, 86), Ui(state, 26), state->font);
        Label(hwnd, L"Name:", Ui(state, 14), Ui(state, 89), Ui(state, 95), Ui(state, 22), state->font);
        state->name = Edit(hwnd, IDC_NAME, Ui(state, 112), Ui(state, 86), Ui(state, 300), Ui(state, 24), state->font);
        Label(hwnd, L"Type:", Ui(state, 14), Ui(state, 125), Ui(state, 95), Ui(state, 22), state->font);
        state->type = CreateWindowExW(0, WC_COMBOBOXW, nullptr,
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                      Ui(state, 112), Ui(state, 122), Ui(state, 150), Ui(state, 150), hwnd,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TYPE)),
                                      GetModuleHandleW(nullptr), nullptr);
        ApplyFont(state->type, state->font);
        SendMessageW(state->type, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Prop"));
        SendMessageW(state->type, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Actor"));
        SendMessageW(state->type, CB_SETCURSEL, 0, 0);
        Label(hwnd, L"Scale:", Ui(state, 14), Ui(state, 161), Ui(state, 95), Ui(state, 22), state->font);
        state->scale = Edit(hwnd, IDC_SCALE, Ui(state, 112), Ui(state, 158), Ui(state, 150), Ui(state, 24), state->font, L"1.0");
        Label(hwnd, L"Texture V:", Ui(state, 14), Ui(state, 197), Ui(state, 95), Ui(state, 22), state->font);
        state->textureV = CreateWindowExW(0, WC_COMBOBOXW, nullptr,
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                          Ui(state, 112), Ui(state, 193), Ui(state, 265), Ui(state, 120), hwnd,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TEXTURE_V)),
                                          GetModuleHandleW(nullptr), nullptr);
        ApplyFont(state->textureV, state->font);
        SendMessageW(state->textureV, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto (recommended)"));
        SendMessageW(state->textureV, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Flip V"));
        SendMessageW(state->textureV, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Preserve source V"));
        SendMessageW(state->textureV, CB_SETCURSEL, 0, 0);
        state->twoSided = CreateWindowExW(0, L"BUTTON", L"Render both sides / two-sided faces",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                          Ui(state, 112), Ui(state, 225), Ui(state, 390), Ui(state, 24), hwnd,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TWO_SIDED)),
                                          GetModuleHandleW(nullptr), nullptr);
        ApplyFont(state->twoSided, state->font);
        SendMessageW(state->twoSided, BM_SETCHECK, BST_UNCHECKED, 0);
        state->centerBottom = CreateWindowExW(0, L"BUTTON", L"Center bottom on origin (recommended)",
                                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                               Ui(state, 112), Ui(state, 257), Ui(state, 390), Ui(state, 24), hwnd,
                                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CENTER_BOTTOM)),
                                               GetModuleHandleW(nullptr), nullptr);
        ApplyFont(state->centerBottom, state->font);
        SendMessageW(state->centerBottom, BM_SETCHECK, BST_CHECKED, 0);
        Button(hwnd, IDC_CONVERT, L"Create VXP2", Ui(state, 112), Ui(state, 298), Ui(state, 120), Ui(state, 30), state->font);
        state->status = Label(hwnd, L"Choose a model file.", Ui(state, 14), Ui(state, 346), Ui(state, 680), Ui(state, 24), state->font);
        return 0;
    }
    case WM_COMMAND:
        if (state == nullptr)
            break;
        switch (LOWORD(wParam)) {
        case IDC_OBJ_BROWSE:
            ChooseObj(state);
            return 0;
        case IDC_OUTPUT_BROWSE:
            ChooseOutput(state);
            return 0;
        case IDC_CONVERT:
            ConvertGui(state);
            return 0;
        default:
            break;
        }
        break;
    case WM_DESTROY:
        if (state != nullptr && state->font != nullptr) {
            DeleteObject(state->font);
            state->font = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

struct ParsedArgs {
    bool gui = true;
    bool showHelp = false;
    std::filesystem::path input;
    std::filesystem::path output;
    std::wstring name;
    obj2vxp2::AssetKind kind = obj2vxp2::AssetKind::Prop;
    double scale = 1.0;
    obj2vxp2::TextureVMode textureVMode = obj2vxp2::TextureVMode::Auto;
    bool twoSidedFaces = false;
    bool centerBottom = true;
    double guiScale = 1.0;
    std::filesystem::path multiLogPath;
    std::wstring error;
};

bool TakeValue(int argc, wchar_t **argv, int *index, std::wstring *value, const wchar_t *option, std::wstring *error)
{
    if (*index + 1 >= argc) {
        *error = std::wstring(L"Missing value after ") + option;
        return false;
    }
    *value = argv[++(*index)];
    return true;
}

ParsedArgs ParseArgs(int argc, wchar_t **argv)
{
    ParsedArgs result;
    if (argc <= 1)
        return result;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            result.showHelp = true;
            continue;
        }
        if (arg == L"-o" || arg == L"--output") {
            std::wstring value;
            if (!TakeValue(argc, argv, &i, &value, arg.c_str(), &result.error))
                return result;
            result.output = value;
            continue;
        }
        if (arg == L"--name") {
            if (!TakeValue(argc, argv, &i, &result.name, arg.c_str(), &result.error))
                return result;
            continue;
        }
        if (arg == L"--type") {
            std::wstring value;
            if (!TakeValue(argc, argv, &i, &value, arg.c_str(), &result.error))
                return result;
            if (value == L"prop")
                result.kind = obj2vxp2::AssetKind::Prop;
            else if (value == L"actor")
                result.kind = obj2vxp2::AssetKind::Actor;
            else {
                result.error = L"--type must be prop or actor";
                return result;
            }
            continue;
        }
        if (arg == L"--scale") {
            std::wstring value;
            if (!TakeValue(argc, argv, &i, &value, arg.c_str(), &result.error))
                return result;
            if (!ParseDouble(value, &result.scale)) {
                result.error = L"--scale requires a numeric value";
                return result;
            }
            continue;
        }
        if (arg == L"--flip-v") {
            result.textureVMode = obj2vxp2::TextureVMode::Flip;
            continue;
        }
        if (arg == L"--no-flip-v" || arg == L"--preserve-v") {
            result.textureVMode = obj2vxp2::TextureVMode::Preserve;
            continue;
        }
        if (arg == L"--auto-texture-v") {
            result.textureVMode = obj2vxp2::TextureVMode::Auto;
            continue;
        }
        if (arg == L"--two-sided") {
            result.twoSidedFaces = true;
            continue;
        }
        if (arg == L"--no-center-bottom") {
            result.centerBottom = false;
            continue;
        }
        if (arg == L"-gui_scale" || arg == L"--gui-scale") {
            std::wstring value;
            if (!TakeValue(argc, argv, &i, &value, arg.c_str(), &result.error))
                return result;
            if (!ParseDouble(value, &result.guiScale) || !(result.guiScale > 0.0) || result.guiScale > 9.9) {
                result.error = L"-gui_scale requires a value greater than 0 and no more than 9.9";
                return result;
            }
            continue;
        }
        if (arg == L"--multi-log") {
            std::wstring value;
            if (!TakeValue(argc, argv, &i, &value, arg.c_str(), &result.error))
                return result;
            result.multiLogPath = value;
            continue;
        }
        if (!arg.empty() && arg[0] == L'-') {
            result.error = L"Unknown option: " + arg;
            return result;
        }
        if (!result.input.empty()) {
            result.error = L"Only one model input may be specified";
            return result;
        }
        result.input = arg;
    }
    if (!result.input.empty()) {
        result.gui = false;
        if (result.output.empty())
            result.output = DefaultOutputPath(result.input);
        if (result.name.empty())
            result.name = InputStem(result.input);
    }
    return result;
}

void WriteStdError(const std::wstring &text)
{
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h != nullptr && h != INVALID_HANDLE_VALUE) {
        const std::string utf8 = Utf8FromWide(text + L"\r\n");
        DWORD written = 0;
        WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
}

int RunCli(const ParsedArgs &args)
{
    obj2vxp2::Stats stats;
    std::string error;
    if (!RunConversion(args.input, args.output, args.name, args.kind, args.scale, args.textureVMode,
                       args.twoSidedFaces, args.centerBottom, args.multiLogPath, &stats, &error)) {
        WriteStdError(L"ERROR: " + WideFromUtf8(error));
        return 2;
    }
    std::wostringstream out;
    out << L"Created " << args.output.wstring() << L"\r\n"
        << L"source_format: " << WideFromUtf8(stats.sourceFormat) << L"\r\n"
        << L"source_vertices: " << stats.sourceVertices << L"\r\n"
        << L"source_faces: " << stats.sourceFaces << L"\r\n"
        << L"bmdl_parts: " << stats.bmdlParts << L"\r\n"
        << L"bmdl_vertices: " << stats.bmdlVertices << L"\r\n"
        << L"triangles: " << stats.triangles << L"\r\n"
        << L"textures: " << stats.textures << L"\r\n"
        << L"fourcn_bytes: " << stats.fourcnBytes << L"\r\n";
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != nullptr && h != INVALID_HANDLE_VALUE) {
        const std::string utf8 = Utf8FromWide(out.str());
        DWORD written = 0;
        WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
    return 0;
}

int RunGui(HINSTANCE instance, const std::filesystem::path &multiLogPath, double guiScale)
{
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 3;

    GuiState state;
    state.multiLogPath = multiLogPath;
    state.guiScale = guiScale;

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rcWindow = {0, 0, Ui(&state, 710), Ui(&state, 390)};
    AdjustWindowRectEx(&rcWindow, style, FALSE, 0);
    HWND hwnd = CreateWindowExW(0, kWindowClass, kWindowTitle,
                                style, CW_USEDEFAULT, CW_USEDEFAULT,
                                rcWindow.right - rcWindow.left, rcWindow.bottom - rcWindow.top,
                                nullptr, nullptr, instance, &state);
    if (hwnd == nullptr)
        return 3;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}

const wchar_t *HelpText()
{
    return L"4DMM Model to VXP2 v4\n\n"
           L"GUI: obj2vxp2.exe [-gui_scale 1|2|...]\n\n"
           L"CLI: obj2vxp2.exe model.obj|model.glb|model.fbx|bundle.zip [-o out.vxp2] [--name Name] "
           L"[--type prop|actor] [--scale 1.0] [--auto-texture-v|--flip-v|--preserve-v] [--two-sided] [--no-center-bottom]\n\n"
           L"Texture V defaults to Auto: OBJ/FBX use the legacy 3DMM V correction, while GLB preserves glTF V coordinates. "
           L"--no-flip-v remains an alias for --preserve-v. FBX ZIP bundles may contain source/<model>.fbx plus textures/*.jpg/png. "
           L"Center-bottom normalization is enabled by default. -gui_scale is passed through from 4DMM "
           L"when launched with Ctrl+7. --multi-log PATH is supplied by 4DMM only when 3dmovie.exe was launched with -multi_log.";
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    // 4DMM uses application-controlled DPI and passes its authored native-tool
    // geometry scale separately through -gui_scale. Make this helper DPI-aware
    // as well so Windows does not bitmap-scale the window a second time.
    SetProcessDPIAware();

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninit = SUCCEEDED(com);

    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        if (uninit) CoUninitialize();
        return 3;
    }
    ParsedArgs args = ParseArgs(argc, argv);
    LocalFree(argv);

    int result = 0;
    if (!args.error.empty()) {
        WriteStdError(args.error);
        MessageBoxW(nullptr, args.error.c_str(), kWindowTitle, MB_ICONERROR | MB_OK);
        result = 2;
    }
    else if (args.showHelp) {
        MessageBoxW(nullptr, HelpText(), kWindowTitle, MB_OK | MB_ICONINFORMATION);
    }
    else if (args.gui) {
        result = RunGui(instance, args.multiLogPath, args.guiScale);
    }
    else {
        result = RunCli(args);
    }

    if (uninit)
        CoUninitialize();
    return result;
}
