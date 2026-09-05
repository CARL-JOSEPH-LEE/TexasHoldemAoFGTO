#pragma once
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace aof2 {
inline bool same_file_path(const std::filesystem::path& first, const std::filesystem::path& second) {
    std::error_code error;
    if (std::filesystem::equivalent(first, second, error) && !error) return true;
    auto a = std::filesystem::weakly_canonical(std::filesystem::absolute(first)).lexically_normal();
    auto b = std::filesystem::weakly_canonical(std::filesystem::absolute(second)).lexically_normal();
#ifdef _WIN32
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
    return a == b;
#endif
}
inline bool output_paths_overlap(const std::string& first, const std::string& second) {
    if (first.empty() || second.empty()) return false;
    const auto a = std::filesystem::u8path(first), b = std::filesystem::u8path(second);
    auto at = a, bt = b; at += ".tmp"; bt += ".tmp";
    return same_file_path(a, b) || same_file_path(at, b) || same_file_path(a, bt);
}
// Source and destination must be in the same directory/filesystem.
inline void atomic_replace(const std::filesystem::path& source, const std::filesystem::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("cannot atomically replace output file (Windows error " + std::to_string(GetLastError()) + ")");
#else
    std::filesystem::rename(source, destination);
#endif
}
}
