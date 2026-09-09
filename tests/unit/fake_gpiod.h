// ArduLinux - Arduino API for Linux
// Copyright (c) 2026-27 l5yth
//
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file fake_gpiod.h
 * @brief Control surface for the in-process libgpiod fake.
 *
 * LinuxGPIOPin talks to real GPIO character devices, which unit tests cannot
 * open: /dev/gpiochip* requires hardware (or the gpio-sim kernel module plus
 * root).  fake_gpiod.cpp instead defines the libgpiod symbols that
 * LinuxGPIOPin.cpp references, so the test binary links against the fake and
 * never reaches the real library.  Tests drive the error paths by setting the
 * knobs below and assert on the counters afterwards.
 *
 * The fake implements **both** library APIs, selected by the same probe
 * LinuxGPIOPin.h uses, because the two are installed on different machines that
 * matter: CI runs libgpiod 1.x (Ubuntu ships 1.6.3) while the deployment
 * targets run 2.x.  Where the two APIs express the same intent differently, the
 * fake normalises it -- see FakeDirection -- so a test asserting on behavior
 * common to both versions needs no `#if`.
 *
 * @see fake_gpiod.cpp for the symbol definitions.
 */

/**
 * Line direction, normalised across the two libgpiod APIs.
 *
 * v2 expresses direction as a gpiod_line_settings property; v1 expresses it by
 * which request function is called.  The fake records this enum either way.
 */
enum FakeDirection {
    FakeDirUnset = -1, ///< nothing has configured a direction yet
    FakeDirAsIs = 0,   ///< requested without changing the line's direction
    FakeDirInput = 1,  ///< configured as an input
    FakeDirOutput = 2, ///< configured as an output
};

/** Knobs and counters for the fake libgpiod used by the LinuxGPIOPin tests. */
struct FakeGpiod {
    // ─── Knobs: make a libgpiod call fail ────────────────────────────────────

    /** Make the chip open fail (chip missing or not permitted). */
    bool chip_open_fails = false;
    /**
     * Make line acquisition fail, i.e. the line is already claimed.
     *
     * Normalised across versions: v2's gpiod_chip_request_lines() returns NULL,
     * v1's gpiod_line_request() returns -1.
     */
    bool request_lines_fails = false;
    /** Offset reported by gpiod_chip_get_line_offset_from_name(); -1 = no such name. (v2) */
    int name_lookup_offset = 7;
    /** Return value of gpiod_line_config_add_line_settings(). (v2) */
    int add_line_settings_ret = 0;
    /** Value reported by the line read; -1 = the library's error sentinel. */
    int get_value_ret = 1;
    /** Return value of the line write; non-zero signals failure. */
    int set_value_ret = 0;
    /** Return value of gpiod_line_request_reconfigure_lines(). (v2) */
    int reconfigure_ret = 0;
    /** errno the fake sets before returning a failure, so strerror() has input. */
    int fail_errno = 16 /* EBUSY */;

    // ─── Counters and captured arguments ─────────────────────────────────────

    int chip_open_count = 0;        ///< successful chip opens
    int chip_close_count = 0;       ///< chip closes with a live handle
    int chip_close_null_count = 0;  ///< chip closes with NULL (v2 only; see getLine)
    int line_release_count = 0;     ///< line releases
    unsigned last_offset = 0;       ///< offset the line was requested at
    const char *last_line_name = nullptr; ///< name the line was looked up by
    int last_direction = FakeDirUnset;    ///< normalised direction last configured
    int last_output_value = -1;     ///< output value last seeded
    int last_written = -1;          ///< value last written to the line
    const char *last_consumer = nullptr;  ///< consumer label last set

    /** Restore every knob and counter to its default. Call at test start. */
    void reset() { *this = FakeGpiod(); }
};

/** The single fake instance shared by the fake symbols and the tests. */
extern FakeGpiod fake;
