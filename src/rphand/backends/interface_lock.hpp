#pragma once

#include <cerrno>
#include <fcntl.h>
#include <net/if.h>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace rphand::detail {
// SDK sessions share the interface; OTA holds the same file exclusively.
// Keep the legacy name so the standalone DH116S updater participates too.
class InterfaceLock {
public:
    InterfaceLock() = default;
    ~InterfaceLock() { close(); }
    InterfaceLock(const InterfaceLock&) = delete;
    InterfaceLock& operator=(const InterfaceLock&) = delete;

    bool open(const std::string& name) {
        close();
        if (name.empty() || name.size() >= IFNAMSIZ || name.find('/') != std::string::npos) {
            errno = EINVAL;
            return false;
        }
        const auto path = "/tmp/dh116s-ota-" + name + ".lock";
        const int flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
        fd_ = ::open(path.c_str(), flags);
        if (fd_ < 0 && errno == ENOENT) {
            fd_ = ::open(path.c_str(), flags | O_CREAT | O_EXCL, 0644);
            if (fd_ < 0 && errno == EEXIST) fd_ = ::open(path.c_str(), flags);
        }
        if (fd_ < 0) return false;
        struct stat info{};
        if (fstat(fd_, &info) != 0) return fail();
        if (!S_ISREG(info.st_mode)) {
            errno = EINVAL;
            return fail();
        }
        if (flock(fd_, LOCK_SH | LOCK_NB) != 0) return fail();
        return true;
    }

    void close() {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }

private:
    bool fail() {
        const int error = errno;
        close();
        errno = error;
        return false;
    }
    int fd_ = -1;
};
}  // namespace rphand::detail
