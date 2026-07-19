#pragma once

#include <iostream>

namespace framewright {

enum class LogLevel { Quiet, Error, Warning, Info };

/// Set the library-wide log level. Default is Error (silent unless something fails).
///
/// @warning This is process-global state shared by every reader and writer,
/// and it is not synchronised. Calling it while another thread is logging is
/// a data race. Set it once during start-up, before creating readers or
/// writers, rather than toggling it at runtime.
void setLogLevel(LogLevel level);

/// Get the current log level.
LogLevel getLogLevel();

namespace detail {

/// Returns the output stream if the message level is at or below the current
/// log level, otherwise returns a null stream that discards output.
std::ostream& log(LogLevel level);

} // namespace detail

} // namespace framewright
