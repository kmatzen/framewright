#pragma once

#include <iostream>

namespace cvffmpeg {

enum class LogLevel { Quiet, Error, Warning, Info };

/// Set the library-wide log level. Default is Error (silent unless something fails).
void setLogLevel(LogLevel level);

/// Get the current log level.
LogLevel getLogLevel();

namespace detail {

/// Returns the output stream if the message level is at or below the current
/// log level, otherwise returns a null stream that discards output.
std::ostream& log(LogLevel level);

} // namespace detail

} // namespace cvffmpeg
