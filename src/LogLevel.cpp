#include "cvffmpeg/LogLevel.h"

namespace cvffmpeg {

namespace {

LogLevel g_logLevel = LogLevel::Error;

class NullBuffer : public std::streambuf {
  protected:
    int overflow(int c) override { return c; }
};

NullBuffer g_nullBuffer;
std::ostream g_nullStream(&g_nullBuffer);

} // namespace

void setLogLevel(LogLevel level) { g_logLevel = level; }

LogLevel getLogLevel() { return g_logLevel; }

namespace detail {

std::ostream& log(LogLevel level) {
    if (level <= g_logLevel) {
        return std::cerr;
    }
    return g_nullStream;
}

} // namespace detail

} // namespace cvffmpeg
