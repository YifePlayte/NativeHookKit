#include "nhk/process.h"

#include "nhk/text.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

namespace nhk::process {

bool SelfCommandLine(char* buffer, size_t size) {
    if (buffer == nullptr || size < 2u) return false;
    const int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t result;
    do {
        result = read(fd, buffer, size - 1u);
    } while (result < 0 && errno == EINTR);
    close(fd);
    if (result <= 0) return false;
    // cmdline is NUL-separated; argv[0] ends at the first NUL.
    size_t length = 0u;
    while (length < static_cast<size_t>(result) && buffer[length] != '\0') {
        ++length;
    }
    buffer[length] = '\0';
    return true;
}

bool SelfCommandLineIs(const char* name) {
    char buffer[64]{};
    return SelfCommandLine(buffer, sizeof(buffer)) &&
            text::StringsEqual(buffer, name);
}

bool SelfExecutable(char* buffer, size_t size) {
    if (buffer == nullptr || size < 2u) return false;
    const ssize_t length = readlink("/proc/self/exe", buffer, size - 1u);
    if (length <= 0 || static_cast<size_t>(length) >= size) return false;
    buffer[length] = '\0';
    return true;
}

bool SelfExecutableIs(const char* path) {
    char buffer[128]{};
    return SelfExecutable(buffer, sizeof(buffer)) &&
            text::StringsEqual(buffer, path);
}

}  // namespace nhk::process
