#include "Error.h"
#include <cstdint>
#include <sstream>
#include <cstring>

namespace galay::kernel
{
const char* error_string[] = {
    "Connection disconnected",
    "Event notified but source not ready",
    "Parameter invalid",
    "Failed to receive data",
    "Failed to send data",
    "Failed to accept connection",
    "Failed to connect to remote host",
    "Failed to bind socket address",
    "Failed to listen on socket",
    "Failed to open file",
    "Failed to read file",
    "Failed to write file",
    "Failed to get file status",
    "Failed to sync file",
    "Failed to seek file",
    "Operation timeout",
    "Not running on IO scheduler"
};

bool IOError::contains(uint64_t error, IOErrorCode code)
{
    uint32_t io_error_code = error & 0xffffffff;
    return static_cast<uint32_t>(code) == io_error_code;
}

IOError::IOError(IOErrorCode io_error_code, uint32_t system_code)
    : m_code(makeErrorCode(io_error_code, system_code))
{
}

uint64_t IOError::code() const
{
    return m_code;
}

std::string IOError::message() const
{
    uint32_t io_error_code = m_code & 0xffffffff;
    uint32_t system_code = m_code >> 32;
    std::stringstream str;
    str << error_string[io_error_code];
    if(system_code != 0) {
        str << " (sys: " << strerror(system_code) << ")";
    } else {
        str << " (sys: no error)";
    }
    return str.str();
}

void IOError::reset()
{
    m_code = 0;
}

uint64_t IOError::makeErrorCode(IOErrorCode io_error_code, uint32_t system_code)
{
    uint64_t ret = system_code;
    ret = ret << 32;
    return ret | static_cast<uint32_t>(io_error_code);
}
}