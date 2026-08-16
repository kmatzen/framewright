#pragma once

// popen/pclose are POSIX; MSVC provides the same thing under a leading
// underscore. Only the tests shell out to ffprobe, so this stays test-only
// rather than living in the library.
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif
