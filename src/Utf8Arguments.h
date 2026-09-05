#pragma once
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

// MSVCRT's narrow argv uses the system code page and can corrupt Chinese paths.
// Convert the original UTF-16 command line to UTF-8, then use filesystem::u8path.
class Utf8Arguments {
public:
    Utf8Arguments(int argc, char** argv) {
#ifdef _WIN32
        (void)argc; (void)argv;
        int count = 0;
        auto wide = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!wide) throw std::runtime_error("cannot read Unicode command line");
        try {
            for (int i = 0; i < count; ++i) {
                const int length = static_cast<int>(wcslen(wide[i]));
                const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide[i], length, nullptr, 0, nullptr, nullptr);
                std::string s(bytes, '\0');
                if (bytes) WideCharToMultiByte(CP_UTF8, 0, wide[i], length, s.data(), bytes, nullptr, nullptr);
                strings_.push_back(std::move(s));
            }
        } catch (...) { LocalFree(wide); throw; }
        LocalFree(wide);
#else
        for (int i = 0; i < argc; ++i) strings_.emplace_back(argv[i]);
#endif
        for (auto& value : strings_) args_.push_back(value.data());
    }
    int count() const { return static_cast<int>(args_.size()); }
    char** data() { return args_.data(); }
private:
    std::vector<std::string> strings_;
    std::vector<char*> args_;
};
