#include "FileClassify.h"

#include <algorithm>

namespace {

std::wstring utf8ToWide(const std::vector<BYTE>& bytes) {
    if (bytes.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(bytes.data()),
                                   static_cast<int>(bytes.size()), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(bytes.data()),
                         static_cast<int>(bytes.size()), w.data(), len);
    return w;
}

std::wstring ansiToWide(const std::vector<BYTE>& bytes) {
    if (bytes.empty()) return L"";
    int len = MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(bytes.data()),
                                   static_cast<int>(bytes.size()), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()),
                         w.data(), len);
    return w;
}

}  // namespace

namespace FileClassify {

bool isImageExtension(const std::wstring& ext) {
    static const std::wstring exts[] = {L".bmp", L".jpg", L".jpeg", L".png", L".gif", L".ico", L".tif", L".tiff"};
    return std::ranges::find(exts, ext) != std::end(exts);
}

bool isVideoExtension(const std::wstring& ext) {
    static const std::wstring exts[] = {L".mp4", L".m4v", L".mkv", L".avi", L".mov", L".wmv",
                                         L".webm", L".mpg", L".mpeg", L".3gp"};
    return std::ranges::find(exts, ext) != std::end(exts);
}

std::optional<std::wstring> decodeTextContent(const std::vector<BYTE>& bytes) {
    if (bytes.empty()) return L"";

    // BOM'd content is unambiguously text, so decode it before running the
    // binary heuristic below - a UTF-16LE-encoded ASCII file is *all*
    // embedded NULs (one per character) and would otherwise always get
    // misclassified as binary and never reach the UTF-16 branch.
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        return utf8ToWide(std::vector<BYTE>(bytes.begin() + 3, bytes.end()));
    }
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        return std::wstring(reinterpret_cast<const wchar_t*>(bytes.data() + 2), (bytes.size() - 2) / 2);
    }

    size_t control = 0;
    for (BYTE b : bytes) {
        if (b == 0) return std::nullopt;  // embedded NUL - treat as binary
        if (b < 0x09 || (b > 0x0D && b < 0x20)) ++control;
    }
    if (control * 20 > bytes.size()) return std::nullopt;  // >5% control chars - treat as binary

    std::wstring text = utf8ToWide(bytes);
    if (text.empty()) text = ansiToWide(bytes);
    return text;
}

}  // namespace FileClassify
