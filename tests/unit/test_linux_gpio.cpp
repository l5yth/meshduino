// ArduLinux - Arduino API for Linux
// Copyright (c) 2026-27 l5yth
//
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_linux_gpio.cpp
 * @brief Unit tests for LinuxGPIOPin against a faked libgpiod v2.
 *
 * Every libgpiod call LinuxGPIOPin makes is served by fake_gpiod.cpp, so these
 * tests exercise the real LinuxGPIOPin.cpp source without a GPIO character
 * device.  The chip label "null" is used throughout: find_chip_by_label() tries
 * "/dev/<label>" first, and /dev/null is always present and readable, so the
 * fast path is taken and the fake's gpiod_chip_open() gets control without a
 * /dev scan.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <stdexcept>

#include "fake_gpiod.h"
#include "linux/gpio/LinuxGPIOPin.h"

/** Chip label that resolves to /dev/null, i.e. always openable. */
static const char *kChip = "null";

/**
 * Exposes LinuxGPIOPin's protected hardware hooks to the tests.
 *
 * readPinHardware(), writePin() and setPinMode() are protected on GPIOPin;
 * production code reaches them through digitalRead()/digitalWrite().  Calling
 * them directly keeps each test focused on one gpiod interaction.
 */
class TestableGPIOPin : public LinuxGPIOPin
{
public:
    /** Construct via the by-offset overload. */
    TestableGPIOPin(pin_size_t n, const char *chipLabel, int lineNum)
        : LinuxGPIOPin(n, chipLabel, lineNum, "test-pin")
    {
        setSilent();
    }

    /** Construct via the by-name overload. */
    TestableGPIOPin(pin_size_t n, const char *chipLabel, const char *lineName)
        : LinuxGPIOPin(n, chipLabel, lineName, "test-pin")
    {
        setSilent();
    }

    /** @return the raw hardware level, propagating any error LinuxGPIOPin throws. */
    PinStatus callReadPinHardware() { return readPinHardware(); }
    /** Drive the pin, propagating any error LinuxGPIOPin throws. */
    void callWritePin(PinStatus s) { writePin(s); }
    /** Reconfigure direction/bias. */
    void callSetPinMode(PinMode m) { setPinMode(m); }
};

// ─── Construction: the happy paths ───────────────────────────────────────────

TEST_CASE("LinuxGPIOPin acquires a line by offset", "[linuxgpio]")
{
    fake.reset();
    TestableGPIOPin pin(3, kChip, 20);

    CHECK(fake.chip_open_count == 1);
    CHECK(fake.last_offset == 20u);
    CHECK(fake.last_consumer != nullptr);
    CHECK(pin.getPinNum() == 3);
}

TEST_CASE("LinuxGPIOPin acquires a line by name", "[linuxgpio]")
{
    fake.reset();
    fake.name_lookup_offset = 11;
    TestableGPIOPin pin(4, kChip, "GPIO11");

    CHECK(fake.chip_open_count == 1);
    CHECK(fake.last_line_name != nullptr);
#if GPIOD_V == 2
    // v2 resolves the name to an offset itself, and must use that rather than
    // the Arduino pin number.  v1 hands the name to libgpiod and has no offset.
    CHECK(fake.last_offset == 11u);
#endif
}

// ─── Construction: the chip cannot be opened ─────────────────────────────────

TEST_CASE("LinuxGPIOPin throws when the chip cannot be opened (by offset)", "[linuxgpio]")
{
    fake.reset();
    fake.chip_open_fails = true;
    CHECK_THROWS_AS(TestableGPIOPin(5, kChip, 20), std::invalid_argument);
}

TEST_CASE("LinuxGPIOPin throws when the chip cannot be opened (by name)", "[linuxgpio]")
{
    fake.reset();
    fake.chip_open_fails = true;
    CHECK_THROWS_AS(TestableGPIOPin(5, kChip, "GPIO20"), std::invalid_argument);
}

// ─── REGRESSION: a claimed line must throw, never return NULL ────────────────
//
// gpiod_chip_request_lines() returns NULL when the line is already held by
// another consumer -- e.g. dtparam=i2s=on reserving GPIO18-21 for the kernel.
// Returning that NULL let it reach gpiod_line_request_reconfigure_lines(),
// which aborts on `assert(request)` with no indication of which pin failed.

TEST_CASE("LinuxGPIOPin throws when the line request fails (by offset)", "[linuxgpio][regression]")
{
    fake.reset();
    fake.request_lines_fails = true;
    CHECK_THROWS_AS(TestableGPIOPin(6, kChip, 20), std::invalid_argument);
}

TEST_CASE("LinuxGPIOPin throws when the line request fails (by name)", "[linuxgpio][regression]")
{
    fake.reset();
    fake.request_lines_fails = true;
    CHECK_THROWS_AS(TestableGPIOPin(6, kChip, "GPIO20"), std::invalid_argument);
}

#if GPIOD_V == 2  // v1's acquisition message names neither the line nor the chip (G4)
TEST_CASE("failed line request names the line and chip", "[linuxgpio][regression]")
{
    fake.reset();
    fake.request_lines_fails = true;
    try {
        TestableGPIOPin pin(6, kChip, 20);
        FAIL("expected std::invalid_argument");
    } catch (const std::invalid_argument &e) {
        // The operator has to learn *which* pin is unavailable from the message.
        // v1's message names neither; see ACCEPTANCE.md known gap G4.
        CHECK_THAT(e.what(), Catch::Matchers::ContainsSubstring("20"));
        CHECK_THAT(e.what(), Catch::Matchers::ContainsSubstring(kChip));
    }
}
#endif

// REGRESSION: gpiod_chip_get_line_offset_from_name() returns -1 for an unknown
// name.  Assigned straight into the unsigned `offset` member that became a
// request for line 4294967295, so a typo'd line name produced a confusing
// acquisition failure instead of naming the missing line.  Note that the line
// request is left *succeeding* here: the throw must come from the name lookup
// alone, or this test would pass without the check being present.
#if GPIOD_V == 2  // v1 has no name-to-offset lookup; libgpiod resolves the name itself
TEST_CASE("an unknown line name is reported rather than requested as offset -1",
          "[linuxgpio][regression]")
{
    fake.reset();
    fake.name_lookup_offset = -1;      // libgpiod reports ENOENT for unknown names
    fake.request_lines_fails = false;  // the request would otherwise succeed

    CHECK_THROWS_AS(TestableGPIOPin(7, kChip, "NO_SUCH_LINE"), std::invalid_argument);
    // Nothing may be handed to libgpiod once the name lookup has failed.
    CHECK(fake.last_offset != 4294967295u);
}
#endif

#if GPIOD_V == 2  // v1 has no name-to-offset lookup
TEST_CASE("an unknown line name is named in the error", "[linuxgpio][regression]")
{
    fake.reset();
    fake.name_lookup_offset = -1;
    try {
        TestableGPIOPin pin(7, kChip, "NO_SUCH_LINE");
        FAIL("expected std::invalid_argument");
    } catch (const std::invalid_argument &e) {
        CHECK_THAT(e.what(), Catch::Matchers::ContainsSubstring("NO_SUCH_LINE"));
    }
}
#endif

#if GPIOD_V == 2  // gpiod_line_config is a v2 concept
TEST_CASE("a rejected line config still fails the construction", "[linuxgpio]")
{
    fake.reset();
    fake.add_line_settings_ret = -1;
    fake.request_lines_fails = true;
    CHECK_THROWS_AS(TestableGPIOPin(8, kChip, 20), std::invalid_argument);
}
#endif

// ─── REGRESSION: the chip handle must be closed exactly once ─────────────────
//
// The v2 getLine() closes the chip as soon as the line request succeeds, since
// a v2 request handle outlives the chip.  The destructor must not close it a
// second time.

TEST_CASE("the chip handle is closed exactly once over the pin's lifetime",
          "[linuxgpio][regression]")
{
    fake.reset();
    {
        TestableGPIOPin pin(9, kChip, 20);
        CHECK(fake.chip_open_count == 1);
    } // destructor runs here

    CHECK(fake.chip_close_count == 1);   // never twice on a live handle
    CHECK(fake.line_release_count == 1); // the line is released once
#if GPIOD_V == 2
    // v2's getLine() already closed the chip and nulled the member, because a
    // v2 request outlives its chip, so the destructor's close is the NULL
    // no-op libgpiod >= 2.0 documents.  v1 keeps the chip open until then.
    CHECK(fake.chip_close_null_count == 1);
#else
    CHECK(fake.chip_close_null_count == 0);
#endif
}

TEST_CASE("a failed line request closes the chip before throwing", "[linuxgpio][regression]")
{
    fake.reset();
    fake.request_lines_fails = true;
    CHECK_THROWS_AS(TestableGPIOPin(10, kChip, 20), std::invalid_argument);
    // The throw must not leak the chip handle it opened.
    CHECK(fake.chip_close_count == 1);
}

// ─── readPinHardware ─────────────────────────────────────────────────────────

TEST_CASE("readPinHardware maps gpiod values onto PinStatus", "[linuxgpio]")
{
    fake.reset();
    TestableGPIOPin pin(11, kChip, 20);

    fake.get_value_ret = 0;
    CHECK(pin.callReadPinHardware() == LOW);

    fake.get_value_ret = 1;
    CHECK(pin.callReadPinHardware() == HIGH);
}

// REGRESSION: a read error used to be handled by assert(), which is compiled
// out under NDEBUG.  Release builds then returned GPIOD_LINE_VALUE_ERROR (-1)
// as if it were a PinStatus; refreshState() cached it and fired a phantom ISR.
TEST_CASE("readPinHardware throws instead of returning GPIOD_LINE_VALUE_ERROR",
          "[linuxgpio][regression]")
{
    fake.reset();
    TestableGPIOPin pin(12, kChip, 20);

    fake.get_value_ret = -1; // GPIOD_LINE_VALUE_ERROR
    CHECK_THROWS_AS(pin.callReadPinHardware(), std::runtime_error);
}

TEST_CASE("a read error names the line", "[linuxgpio][regression]")
{
    fake.reset();
    TestableGPIOPin pin(13, kChip, 20);
    fake.get_value_ret = -1;

    try {
        pin.callReadPinHardware();
        FAIL("expected std::runtime_error");
    } catch (const std::runtime_error &e) {
        CHECK_THAT(e.what(), Catch::Matchers::ContainsSubstring("test-pin"));
#if GPIOD_V == 2
        // Only v2 knows the line offset; v1 addresses the line by handle.
        CHECK_THAT(e.what(), Catch::Matchers::ContainsSubstring("20"));
#endif
    }
}

// ─── writePin ────────────────────────────────────────────────────────────────

TEST_CASE("writePin drives the line and promotes the pin to OUTPUT", "[linuxgpio]")
{
    fake.reset();
    TestableGPIOPin pin(14, kChip, 20);
    REQUIRE(pin.getPinMode() != OUTPUT); // defaults to INPUT_PULLUP

    pin.callWritePin(HIGH);

    CHECK(pin.getPinMode() == OUTPUT); // auto-promoted for callers that forget
    CHECK(fake.last_written == (int) HIGH);

    pin.callWritePin(LOW);
    CHECK(fake.last_written == (int) LOW);
}

// REGRESSION: a write error used to be handled by assert(), so under NDEBUG a
// rejected write was silently discarded and the cached pin state went stale.
TEST_CASE("writePin throws when the line cannot be driven", "[linuxgpio][regression]")
{
    fake.reset();
    TestableGPIOPin pin(15, kChip, 20);

    fake.set_value_ret = -1;
    CHECK_THROWS_AS(pin.callWritePin(HIGH), std::runtime_error);
}

TEST_CASE("a write error names the line", "[linuxgpio][regression]")
{
    fake.reset();
    TestableGPIOPin pin(16, kChip, 20);
    fake.set_value_ret = -1;

    try {
        pin.callWritePin(HIGH);
        FAIL("expected std::runtime_error");
    } catch (const std::runtime_error &e) {
        CHECK_THAT(e.what(), Catch::Matchers::ContainsSubstring("test-pin"));
#if GPIOD_V == 2
        // Only v2 knows the line offset; v1 addresses the line by handle.
        CHECK_THAT(e.what(), Catch::Matchers::ContainsSubstring("20"));
#endif
    }
}

// ─── setPinMode ──────────────────────────────────────────────────────────────

TEST_CASE("setPinMode(OUTPUT) reconfigures direction and seeds the output value",
          "[linuxgpio]")
{
    fake.reset();
    TestableGPIOPin pin(17, kChip, 20);

    pin.callSetPinMode(OUTPUT);

    CHECK(fake.last_direction == FakeDirOutput);
    // The seed is the *cached* status, not a fresh read: GPIOPin::setPinMode()
    // sets mode=OUTPUT first, after which refreshState() skips the hardware.
    // GPIOPin's cached status defaults to HIGH.
    CHECK(fake.last_output_value == (int) HIGH);
    CHECK(pin.getPinMode() == OUTPUT);
}

TEST_CASE("setPinMode(INPUT) reconfigures direction to input", "[linuxgpio]")
{
    fake.reset();
    TestableGPIOPin pin(18, kChip, 20);

    pin.callSetPinMode(INPUT);

    CHECK(fake.last_direction == FakeDirInput);
    CHECK(pin.getPinMode() == INPUT);
}

TEST_CASE("setPinMode survives a rejected reconfigure", "[linuxgpio]")
{
    fake.reset();
    TestableGPIOPin pin(19, kChip, 20);

    fake.reconfigure_ret = -1;
    fake.add_line_settings_ret = -1;
    // Both failures are logged, not thrown: this runs on writePin()'s
    // auto-promotion path, where throwing would turn a recoverable
    // reconfiguration into a lost write.
    CHECK_NOTHROW(pin.callSetPinMode(INPUT));
}

// REGRESSION: setPinMode() caches the new mode before reconfiguring the line
// and used not to undo it on failure.  `mode` is what gates refreshState(): a
// stale OUTPUT stops all hardware reads, so digitalRead() would return the last
// cached level forever even as the pin changed state.
#if GPIOD_V == 2  // the mode rollback is on the v2 reconfigure path (G4)
TEST_CASE("a rejected reconfigure leaves the cached mode untouched",
          "[linuxgpio][regression]")
{
    fake.reset();
    TestableGPIOPin pin(26, kChip, 20);
    const PinMode before = pin.getPinMode();

    fake.reconfigure_ret = -1;
    pin.callSetPinMode(OUTPUT);

    // The line kept its old direction, so the cache must report the old mode.
    CHECK(pin.getPinMode() == before);
}
#endif

#if GPIOD_V == 2  // the mode rollback is on the v2 reconfigure path (G4)
TEST_CASE("a pin whose promotion to OUTPUT was rejected still reads its hardware",
          "[linuxgpio][regression]")
{
    fake.reset();
    TestableGPIOPin pin(27, kChip, 20);

    // The kernel refuses to make this line an output.
    fake.reconfigure_ret = -1;
    fake.set_value_ret = -1;
    CHECK_THROWS_AS(pin.callWritePin(HIGH), std::runtime_error);

    // refreshState() must still poll: the pin is not an OUTPUT, whatever the
    // failed promotion tried to record.
    fake.reconfigure_ret = 0;
    fake.get_value_ret = 0;
    CHECK(pin.readPin() == LOW);
}
#endif

// REGRESSION: the by-offset overload assigned its int argument straight into
// the unsigned `offset` member, so a negative sentinel became a request for
// line 4294967295.
TEST_CASE("a negative line offset is rejected rather than wrapped", "[linuxgpio][regression]")
{
    fake.reset();
    fake.request_lines_fails = false;  // the request would otherwise succeed

    CHECK_THROWS_AS(TestableGPIOPin(28, kChip, -1), std::invalid_argument);
    CHECK(fake.last_offset != 4294967295u);
}

// ─── Chip lookup: the /dev scan fallback ─────────────────────────────────────
//
// find_chip_by_label() tries "/dev/<label>" first and only scans /dev when that
// path does not exist, so that a kernel label ("pinctrl-rp1") works as well as a
// device name ("gpiochip0").  A label matching neither must fail cleanly rather
// than reach libgpiod with a bad handle.

TEST_CASE("an unknown chip label falls back to scanning /dev and then throws",
          "[linuxgpio]")
{
    fake.reset();
    // No /dev entry by this name, so the scandir path runs over real /dev.
    // No chip carries this label, so the scan finds no match either way.  The
    // number of chips opened while scanning is deliberately not asserted: it is
    // 0 on a host with no GPIO and non-zero on a real board, and both are
    // correct.
    CHECK_THROWS_AS(TestableGPIOPin(20, "ardulinux-no-such-chip", 20),
                    std::invalid_argument);
}

TEST_CASE("an unknown chip label throws on the by-name overload too", "[linuxgpio]")
{
    fake.reset();
    CHECK_THROWS_AS(TestableGPIOPin(21, "ardulinux-no-such-chip", "GPIO20"),
                    std::invalid_argument);
}

#if GPIOD_V == 2  // gpiod_line_config is a v2 concept
TEST_CASE("a rejected line config is reported on the by-name overload", "[linuxgpio]")
{
    fake.reset();
    fake.add_line_settings_ret = -1;
    fake.request_lines_fails = true;
    CHECK_THROWS_AS(TestableGPIOPin(22, kChip, "GPIO20"), std::invalid_argument);
}
#endif

// ─── Polymorphic destruction ─────────────────────────────────────────────────
//
// Applications hand pins to gpioBind(new LinuxGPIOPin(...)), which stores and
// later deletes them through a GPIOPin*.  Exercise that path explicitly: it is
// the one production uses, and it reaches the virtual deleting destructor.

TEST_CASE("deleting through a GPIOPin* releases the line and closes the chip once",
          "[linuxgpio][regression]")
{
    fake.reset();
    GPIOPin *pin = new TestableGPIOPin(23, kChip, 20);
    delete pin;

    CHECK(fake.line_release_count == 1);
    CHECK(fake.chip_close_count == 1);
}

TEST_CASE("deleting a plain LinuxGPIOPin releases its line", "[linuxgpio]")
{
    fake.reset();
    // The README's usage is gpioBind(new LinuxGPIOPin(...)), i.e. the most
    // derived type is LinuxGPIOPin itself; that reaches a different destructor
    // variant than deleting a subclass through a base pointer.
    LinuxGPIOPin *pin = new LinuxGPIOPin(24, kChip, 20, "plain-pin");
    delete pin;

    CHECK(fake.line_release_count == 1);
    CHECK(fake.chip_close_count == 1);
}

// REGRESSION: writePin() used to cache the new level via GPIOPin::writePin()
// *before* driving the hardware, so a rejected write left the cache claiming a
// value the pin never took.  That is unrecoverable rather than merely wrong:
// the auto-promotion to OUTPUT means refreshState() no longer re-reads the
// hardware, so digitalRead() returns the phantom value for the life of the
// process.
TEST_CASE("a failed write leaves the cached pin state untouched",
          "[linuxgpio][regression]")
{
    fake.reset();
    TestableGPIOPin pin(25, kChip, 20);

    fake.set_value_ret = 0;
    pin.callWritePin(LOW);
    REQUIRE(pin.readPin() == LOW);

    // The kernel now rejects the write; the cache must not move to HIGH.
    fake.set_value_ret = -1;
    CHECK_THROWS_AS(pin.callWritePin(HIGH), std::runtime_error);
    CHECK(pin.readPin() == LOW);
}

// REGRESSION: a failed gpiod_line_config_add_line_settings() means the settings
// never reached the config, so the line did not change direction -- even if the
// reconfigure that follows happens to return success.  The mode cache must roll
// back on either failure, independently.
#if GPIOD_V == 2  // gpiod_line_config is a v2 concept
TEST_CASE("a rejected line config rolls the cached mode back too",
          "[linuxgpio][regression]")
{
    fake.reset();
    TestableGPIOPin pin(29, kChip, 20);
    const PinMode before = pin.getPinMode();

    fake.add_line_settings_ret = -1; // settings never make it into the config
    fake.reconfigure_ret = 0;        // ...but the reconfigure still reports success
    pin.callSetPinMode(OUTPUT);

    CHECK(pin.getPinMode() == before);
}
#endif
