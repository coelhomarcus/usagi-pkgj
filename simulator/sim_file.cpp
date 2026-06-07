// sim_file.cpp
// Provides POSIX implementations of file.hpp functions that are not already
// present in simulator.cpp (which covers the basic I/O primitives).
//
// Functions provided here:
//   pkgi_list_dir_contents  — list directory entries
//   pkgi_get_size           — stat a file for its size
//   pkgi_get_inode_type     — stat a path to classify it
//   pkgi_append             — open a file in append mode

#include "file.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::string> pkgi_list_dir_contents(const std::string& path)
{
    std::vector<std::string> result;

    DIR* d = opendir(path.c_str());
    if (!d)
        return result; // empty vector on missing/inaccessible directory

    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr)
    {
        const std::string name = ent->d_name;
        if (name == "." || name == "..")
            continue;
        result.push_back(name);
    }
    closedir(d);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
int64_t pkgi_get_size(const char* path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    return static_cast<int64_t>(st.st_size);
}

// ─────────────────────────────────────────────────────────────────────────────
InodeType pkgi_get_inode_type(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return InodeType::NotExist;
    if (S_ISDIR(st.st_mode))
        return InodeType::Directory;
    return InodeType::File;
}

// ─────────────────────────────────────────────────────────────────────────────
// Open (or create) a file for appending; returns a void* handle compatible
// with pkgi_write / pkgi_close.
void* pkgi_append(const char* path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return nullptr;
    return reinterpret_cast<void*>(static_cast<intptr_t>(fd));
}
