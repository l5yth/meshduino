// ArduLinux - Arduino API for Linux
// Copyright (c) 2026-27 l5yth
//
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file fake_gpiod.cpp
 * @brief In-process stand-in for the libgpiod C API, v1 and v2.
 *
 * Defines every libgpiod symbol LinuxGPIOPin.cpp references.  Because these
 * definitions come from an object file linked directly into the test binary,
 * they take precedence over the shared library, so no GPIO character device is
 * ever opened.  Behaviour is steered through the global ::fake instance.
 *
 * Which API is defined is decided by the same probe LinuxGPIOPin.h uses, so the
 * fake always matches the branch of LinuxGPIOPin.cpp that got compiled.
 *
 * The opaque libgpiod structs are given trivial definitions here; the code
 * under test only ever passes the pointers back, never dereferences them.
 */

#include "fake_gpiod.h"

#include <errno.h>
#include <gpiod.h>
#include <stdlib.h>

/** gpiod v1 defines GPIOD_LINE_BULK_MAX_LINES; v2 does not. */
#ifndef GPIOD_LINE_BULK_MAX_LINES
#define FAKE_GPIOD_V 2
#else
#define FAKE_GPIOD_V 1
#endif

FakeGpiod fake;

struct gpiod_chip { int unused; };

/// Singleton chip handle handed out by the fake; identity is all tests need.
static gpiod_chip g_chip;

extern "C" {

struct gpiod_chip *gpiod_chip_open(const char *path)
{
    (void) path;
    if (fake.chip_open_fails) {
        errno = fake.fail_errno;
        return NULL;
    }
    fake.chip_open_count++;
    return &g_chip;
}

void gpiod_chip_close(struct gpiod_chip *chip)
{
    // Mirrors libgpiod >= 2.0, which returns early on NULL rather than
    // aborting.  Counted separately so tests can prove the destructor never
    // closes a live handle twice.
    if (!chip) {
        fake.chip_close_null_count++;
        return;
    }
    fake.chip_close_count++;
}

} // extern "C"

#if FAKE_GPIOD_V == 2

// ─── libgpiod v2 ─────────────────────────────────────────────────────────────

struct gpiod_chip_info { int unused; };
struct gpiod_line_settings { int unused; };
struct gpiod_line_config { int unused; };
struct gpiod_request_config { int unused; };
struct gpiod_line_request { int unused; };

static gpiod_line_request g_request;

extern "C" {

struct gpiod_chip_info *gpiod_chip_get_info(struct gpiod_chip *chip)
{
    (void) chip;
    return NULL; // the label scan is not exercised; the /dev fast path is used
}

void gpiod_chip_info_free(struct gpiod_chip_info *info) { (void) info; }

const char *gpiod_chip_info_get_label(struct gpiod_chip_info *info)
{
    (void) info;
    return NULL;
}

int gpiod_chip_get_line_offset_from_name(struct gpiod_chip *chip, const char *name)
{
    (void) chip;
    fake.last_line_name = name;
    if (fake.name_lookup_offset < 0)
        errno = ENOENT;
    return fake.name_lookup_offset;
}

struct gpiod_line_settings *gpiod_line_settings_new(void)
{
    return (struct gpiod_line_settings *) malloc(sizeof(struct gpiod_line_settings));
}

void gpiod_line_settings_free(struct gpiod_line_settings *settings) { free(settings); }

int gpiod_line_settings_set_direction(struct gpiod_line_settings *settings,
                                      enum gpiod_line_direction direction)
{
    (void) settings;
    switch (direction) {
    case GPIOD_LINE_DIRECTION_INPUT:  fake.last_direction = FakeDirInput;  break;
    case GPIOD_LINE_DIRECTION_OUTPUT: fake.last_direction = FakeDirOutput; break;
    default:                          fake.last_direction = FakeDirAsIs;   break;
    }
    return 0;
}

int gpiod_line_settings_set_output_value(struct gpiod_line_settings *settings,
                                         enum gpiod_line_value value)
{
    (void) settings;
    fake.last_output_value = (int) value;
    return 0;
}

struct gpiod_line_config *gpiod_line_config_new(void)
{
    return (struct gpiod_line_config *) malloc(sizeof(struct gpiod_line_config));
}

void gpiod_line_config_free(struct gpiod_line_config *config) { free(config); }

int gpiod_line_config_add_line_settings(struct gpiod_line_config *config,
                                        const unsigned int *offsets, size_t num_offsets,
                                        struct gpiod_line_settings *settings)
{
    (void) config;
    (void) settings;
    if (num_offsets)
        fake.last_offset = offsets[0];
    return fake.add_line_settings_ret;
}

struct gpiod_request_config *gpiod_request_config_new(void)
{
    return (struct gpiod_request_config *) malloc(sizeof(struct gpiod_request_config));
}

void gpiod_request_config_free(struct gpiod_request_config *config) { free(config); }

void gpiod_request_config_set_consumer(struct gpiod_request_config *config,
                                       const char *consumer)
{
    (void) config;
    fake.last_consumer = consumer;
}

struct gpiod_line_request *gpiod_chip_request_lines(struct gpiod_chip *chip,
                                                    struct gpiod_request_config *req_cfg,
                                                    struct gpiod_line_config *line_cfg)
{
    (void) chip;
    (void) req_cfg;
    (void) line_cfg;
    if (fake.request_lines_fails) {
        errno = fake.fail_errno;
        return NULL;
    }
    return &g_request;
}

void gpiod_line_request_release(struct gpiod_line_request *request)
{
    (void) request;
    fake.line_release_count++;
}

enum gpiod_line_value gpiod_line_request_get_value(struct gpiod_line_request *request,
                                                   unsigned int offset)
{
    (void) request;
    (void) offset;
    if (fake.get_value_ret < 0)
        errno = fake.fail_errno;
    return (enum gpiod_line_value) fake.get_value_ret;
}

int gpiod_line_request_set_value(struct gpiod_line_request *request, unsigned int offset,
                                 enum gpiod_line_value value)
{
    (void) request;
    (void) offset;
    fake.last_written = (int) value;
    if (fake.set_value_ret != 0)
        errno = fake.fail_errno;
    return fake.set_value_ret;
}

int gpiod_line_request_reconfigure_lines(struct gpiod_line_request *request,
                                         struct gpiod_line_config *config)
{
    (void) request;
    (void) config;
    if (fake.reconfigure_ret != 0)
        errno = fake.fail_errno;
    return fake.reconfigure_ret;
}

} // extern "C"

#else

// ─── libgpiod v1 ─────────────────────────────────────────────────────────────
//
// v1 has no separate request object: gpiod_line is both the line and the
// request, and direction is expressed by which request function is called
// rather than by a settings object.

struct gpiod_line { int unused; };

static gpiod_line g_line;

extern "C" {

const char *gpiod_chip_label(struct gpiod_chip *chip)
{
    (void) chip;
    return NULL; // the label scan is not exercised; the /dev fast path is used
}

struct gpiod_line *gpiod_chip_find_line(struct gpiod_chip *chip, const char *name)
{
    (void) chip;
    fake.last_line_name = name;
    return &g_line;
}

struct gpiod_line *gpiod_chip_get_line(struct gpiod_chip *chip, unsigned int offset)
{
    (void) chip;
    fake.last_offset = offset;
    return &g_line;
}

int gpiod_line_request(struct gpiod_line *line,
                       const struct gpiod_line_request_config *config, int default_val)
{
    (void) line;
    (void) default_val;
    if (config)
        fake.last_consumer = config->consumer;
    fake.last_direction = FakeDirAsIs;
    if (fake.request_lines_fails) {
        errno = fake.fail_errno;
        return -1;
    }
    return 0;
}

void gpiod_line_release(struct gpiod_line *line)
{
    (void) line;
    fake.line_release_count++;
}

int gpiod_line_get_value(struct gpiod_line *line)
{
    (void) line;
    if (fake.get_value_ret < 0)
        errno = fake.fail_errno;
    return fake.get_value_ret;
}

int gpiod_line_set_value(struct gpiod_line *line, int value)
{
    (void) line;
    fake.last_written = value;
    if (fake.set_value_ret != 0)
        errno = fake.fail_errno;
    return fake.set_value_ret;
}

int gpiod_line_request_output(struct gpiod_line *line, const char *consumer,
                              int default_val)
{
    (void) line;
    fake.last_consumer = consumer;
    fake.last_direction = FakeDirOutput;
    fake.last_output_value = default_val;
    return fake.reconfigure_ret;
}

int gpiod_line_request_input(struct gpiod_line *line, const char *consumer)
{
    (void) line;
    fake.last_consumer = consumer;
    fake.last_direction = FakeDirInput;
    return fake.reconfigure_ret;
}

int gpiod_line_set_flags(struct gpiod_line *line, int flags)
{
    (void) line;
    (void) flags;
    return fake.reconfigure_ret;
}

} // extern "C"

#endif
