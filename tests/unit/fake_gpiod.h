// ArduLinux - Arduino API for Linux
// Copyright (c) 2026-27 l5yth
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file fake_gpiod.h
 * @brief Control surface for the in-process libgpiod v2 fake.
 *
 * LinuxGPIOPin talks to real GPIO character devices, which unit tests cannot
 * open: /dev/gpiochip* requires hardware (or the gpio-sim kernel module plus
 * root).  fake_gpiod.cpp instead defines the handful of libgpiod symbols that
 * LinuxGPIOPin.cpp references, so the test binary links against the fake and
 * never reaches the real library.  Tests drive the error paths by setting the
 * knobs below and assert on the counters afterwards.
 *
 * @see fake_gpiod.cpp for the symbol definitions.
 */
struct FakeGpiod {
    // ─── Knobs: make a libgpiod call fail ────────────────────────────────────

    /** Make gpiod_chip_open() return NULL (chip missing / not permitted). */
    bool chip_open_fails = false;
    /** Make gpiod_chip_request_lines() return NULL (line already claimed). */
    bool request_lines_fails = false;
    /** Offset reported by gpiod_chip_get_line_offset_from_name(); -1 = no such name. */
    int name_lookup_offset = 7;
    /** Return value of gpiod_line_config_add_line_settings(). */
    int add_line_settings_ret = 0;
    /** Value reported by gpiod_line_request_get_value(); -1 = GPIOD_LINE_VALUE_ERROR. */
    int get_value_ret = 1;
    /** Return value of gpiod_line_request_set_value(); -1 signals failure. */
    int set_value_ret = 0;
    /** Return value of gpiod_line_request_reconfigure_lines(). */
    int reconfigure_ret = 0;
    /** errno the fake sets before returning a failure, so strerror() has input. */
    int fail_errno = 16 /* EBUSY */;

    // ─── Counters and captured arguments ─────────────────────────────────────

    int chip_open_count = 0;        ///< successful gpiod_chip_open() calls
    int chip_close_count = 0;       ///< gpiod_chip_close() calls with a live handle
    int chip_close_null_count = 0;  ///< gpiod_chip_close() calls with NULL
    int line_release_count = 0;     ///< gpiod_line_request_release() calls
    unsigned last_offset = 0;       ///< offset handed to add_line_settings()
    int last_direction = -1;        ///< direction handed to set_direction()
    int last_output_value = -1;     ///< value handed to set_output_value()
    int last_written = -1;          ///< value handed to set_value()
    const char *last_consumer = nullptr; ///< consumer handed to set_consumer()

    /** Restore every knob and counter to its default. Call at test start. */
    void reset() { *this = FakeGpiod(); }
};

/** The single fake instance shared by the fake symbols and the tests. */
extern FakeGpiod fake;
