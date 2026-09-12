// SPDX-License-Identifier: Apache-2.0
#pragma once

// Minimal stdio/posix replacements for the framework's tt::file helpers,
// which are not exported to external apps.

#include <cstdio>
#include <string>
#include <sys/stat.h>

inline bool fileIsFile(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

inline bool fileIsDirectory(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

inline bool fileFindOrCreateDirectory(const std::string& path, mode_t mode) {
    if (path.empty()) return false;
    if (fileIsDirectory(path)) return true;
    std::string partial;
    size_t i = 0;
    while (i < path.size()) {
        auto next = path.find('/', i + 1);
        if (next == std::string::npos) next = path.size();
        partial = path.substr(0, next);
        if (!partial.empty() && !fileIsDirectory(partial)) {
            if (::mkdir(partial.c_str(), mode) != 0 && !fileIsDirectory(partial)) {
                return false;
            }
        }
        i = next;
    }
    return fileIsDirectory(path);
}

inline std::string fileChildPath(const std::string& folder, const std::string& name) {
    if (folder.empty() || folder.back() == '/') return folder + name;
    return folder + "/" + name;
}

inline std::string fileLastPathSegment(const std::string& path) {
    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
