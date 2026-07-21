#include <catch2/catch_test_macros.hpp>
#include <framewright/LogLevel.h>

// setLogLevel/getLogLevel are process-global (see LogLevel.h), so every case
// here restores the default afterwards to avoid bleeding state into whichever
// test runs next.
namespace {
struct RestoreLogLevel {
    framewright::LogLevel saved = framewright::getLogLevel();
    ~RestoreLogLevel() { framewright::setLogLevel(saved); }
};
} // namespace

TEST_CASE("LogLevel defaults to Error", "[loglevel]") {
    // Nothing has touched the global yet at process start, but other test
    // cases may have run first, so just check the type round-trips rather
    // than assuming we're first.
    RestoreLogLevel restore;
    framewright::setLogLevel(framewright::LogLevel::Error);
    CHECK(framewright::getLogLevel() == framewright::LogLevel::Error);
}

TEST_CASE("setLogLevel/getLogLevel round-trip every level", "[loglevel]") {
    RestoreLogLevel restore;

    framewright::setLogLevel(framewright::LogLevel::Quiet);
    CHECK(framewright::getLogLevel() == framewright::LogLevel::Quiet);

    framewright::setLogLevel(framewright::LogLevel::Error);
    CHECK(framewright::getLogLevel() == framewright::LogLevel::Error);

    framewright::setLogLevel(framewright::LogLevel::Warning);
    CHECK(framewright::getLogLevel() == framewright::LogLevel::Warning);

    framewright::setLogLevel(framewright::LogLevel::Info);
    CHECK(framewright::getLogLevel() == framewright::LogLevel::Info);
}

TEST_CASE("detail::log gates on the current level", "[loglevel]") {
    RestoreLogLevel restore;

    framewright::setLogLevel(framewright::LogLevel::Warning);

    // At or below the current level: returns the real stream (std::cerr).
    CHECK(&framewright::detail::log(framewright::LogLevel::Error) == &std::cerr);
    CHECK(&framewright::detail::log(framewright::LogLevel::Warning) == &std::cerr);

    // Above the current level: returns a discard stream, not std::cerr.
    CHECK(&framewright::detail::log(framewright::LogLevel::Info) != &std::cerr);
}

TEST_CASE("detail::log at Quiet discards everything", "[loglevel]") {
    RestoreLogLevel restore;

    framewright::setLogLevel(framewright::LogLevel::Quiet);
    CHECK(&framewright::detail::log(framewright::LogLevel::Error) != &std::cerr);
    CHECK(&framewright::detail::log(framewright::LogLevel::Warning) != &std::cerr);
    CHECK(&framewright::detail::log(framewright::LogLevel::Info) != &std::cerr);
}

TEST_CASE("detail::log at Info allows every level through", "[loglevel]") {
    RestoreLogLevel restore;

    framewright::setLogLevel(framewright::LogLevel::Info);
    CHECK(&framewright::detail::log(framewright::LogLevel::Error) == &std::cerr);
    CHECK(&framewright::detail::log(framewright::LogLevel::Warning) == &std::cerr);
    CHECK(&framewright::detail::log(framewright::LogLevel::Info) == &std::cerr);
}

// The discard stream must actually be writable and silently swallow output,
// not just be "not std::cerr" -- a null pointer or a broken stream would also
// satisfy the inequality checks above.
TEST_CASE("detail::log discard stream accepts writes without failing", "[loglevel]") {
    RestoreLogLevel restore;

    framewright::setLogLevel(framewright::LogLevel::Quiet);
    std::ostream& discarded = framewright::detail::log(framewright::LogLevel::Error);
    discarded << "this should go nowhere" << std::endl;
    CHECK(discarded.good());
}
