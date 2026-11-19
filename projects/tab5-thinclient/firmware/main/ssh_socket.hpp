#pragma once

#include <fcntl.h>

// libssh2's non-blocking mode requires a non-blocking underlying socket too.
inline bool sshSocketSetNonblocking(int socket)
{
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}
