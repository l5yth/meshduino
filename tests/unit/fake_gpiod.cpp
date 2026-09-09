// ArduLinux - Arduino API for Linux
// Copyright (c) 2026-27 l5yth
//
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file fake_gpiod.cpp
 * @brief In-process stand-in for the libgpiod v2 C API.
 *
 * Defines every libgpiod symbol LinuxGPIOPin.cpp references.  Because these
 * definitions live in an object file linked directly into the test binary,
 * they take precedence over the shared library, so no GPIO character device is
 * ever opened.  Behaviour is steered through the global ::fake instance.
 *
 * The opaque libgpiod structs are given trivial definitions here; the code
 * under test only ever passes the pointers back, never dereferences them.
 */

#include "fake_gpiod.h"

#include <errno.h>
#include <gpiod.h>
#include <stdlib.h>

FakeGpiod fake;

struct gpiod_chip { int unused; };
struct gpiod_chip_info { int unused; };
struct gpiod_line_settings { int unused; };
struct gpiod_line_config { int unused; };
struct gpiod_request_config { int unused; };
struct gpiod_line_request { int unused; };

/// Singleton handles handed out by the fake; identity is all the tests need.
static gpiod_chip g_chip;
static gpiod_line_request g_request;

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

struct gpiod_chip_info *gpiod_chip_get_info(struct gpiod_chip *chip)
{
    (void) chip;
    return NULL; // label scan is not exercised; the /dev fast path is used
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
    (void) name;
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
    fake.last_direction = (int) direction;
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
