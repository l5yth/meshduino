// ArduLinux - Arduino API for Linux
// Copyright (c) 2011-19 Arduino LLC.
// Copyright (c) 2020-23 Geeksville Industries, LLC
// Copyright (c) 2024-26 jp-bennett
// Copyright (c) 2026-27 l5yth
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#ifdef ARDULINUX_HARDWARE

#include "linux/gpio/LinuxGPIOPin.h"
#include "AppInfo.h"
#include <assert.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <dirent.h>
#include <unistd.h>

// Consumer label passed to libgpiod — uses the application name so the
// correct process is visible in /sys/kernel/debug/gpio and similar tools.
#define consumer ardulinuxAppName

static bool chip_is_gpiochip_device(const char *path) {
  char *realname, *sysfsp, devpath[64];
	struct stat statbuf;
	bool ret = false;
	int rv;

	rv = lstat(path, &statbuf);
	if (rv)
		goto out;

	/*
	 * Is it a symbolic link? We have to resolve it before checking
	 * the rest.
	 */
	realname = S_ISLNK(statbuf.st_mode) ? realpath(path, NULL) :
					      strdup(path);
	if (realname == NULL)
		goto out;

	rv = stat(realname, &statbuf);
	if (rv)
		goto out_free_realname;

	/* Is it a character device? */
	if (!S_ISCHR(statbuf.st_mode)) {
		errno = ENOTTY;
		goto out_free_realname;
	}

	/* Is the device associated with the GPIO subsystem? */
	snprintf(devpath, sizeof(devpath), "/sys/dev/char/%u:%u/subsystem",
		 major(statbuf.st_rdev), minor(statbuf.st_rdev));

	sysfsp = realpath(devpath, NULL);
	if (!sysfsp)
		goto out_free_realname;

	/*
	 * In glibc, if any of the underlying readlink() calls fail (which is
	 * perfectly normal when resolving paths), errno is not cleared.
	 */
	errno = 0;

	if (strcmp(sysfsp, "/sys/bus/gpio") != 0) {
		/* This is a character device but not the one we're after. */
		errno = ENODEV;
		goto out_free_sysfsp;
	}

	ret = true;

out_free_sysfsp:
	free(sysfsp);
out_free_realname:
	free(realname);
out:
	errno = 0;
	return ret;
}

static int chip_dir_filter(const struct dirent *entry)
{
	bool is_chip;
	char *path;
	int ret;

	ret = asprintf(&path, "/dev/%s", entry->d_name);
	if (ret < 0)
		return 0;

	is_chip = chip_is_gpiochip_device(path);
	free(path);
	return !!is_chip;
}

static struct gpiod_chip *chip_open_by_name(const char *name)
{
	struct gpiod_chip *chip;
	char *path;
	int ret;

	ret = asprintf(&path, "/dev/%s", name);
	if (ret < 0)
		return NULL;

	chip = gpiod_chip_open(path);
	free(path);

	return chip;
}

// Open the gpiod chip whose label matches `chipLabel`.  First tries
// "/dev/<chipLabel>" as a device basename (covers the gpiochip0 case);
// falls back to scanning /dev/ and comparing each chip's reported label
// (covers passing a kernel label like "pinctrl-rp1" or "pinctrl-bcm2835").
// Caller owns the returned chip; returns NULL if no chip matches.
static struct gpiod_chip *find_chip_by_label(const char *chipLabel)
{
	std::string path = "/dev/";
	path += chipLabel;
	if (access(path.c_str(), R_OK) == 0)
		return chip_open_by_name(chipLabel);

	struct dirent **entries;
	int num_chips = scandir("/dev/", &entries, chip_dir_filter, alphasort);
	if (num_chips <= 0)
		return NULL;

	struct gpiod_chip *match = NULL;
	for (int i = 0; i < num_chips; i++) {
		if (!match) {
			struct gpiod_chip *c = chip_open_by_name(entries[i]->d_name);
			if (c) {
#if GPIOD_V == 2
				struct gpiod_chip_info *info = gpiod_chip_get_info(c);
				const char *label = info ? gpiod_chip_info_get_label(info) : NULL;
				bool hit = label && strcmp(label, chipLabel) == 0;
				if (info) gpiod_chip_info_free(info);
#else
				const char *label = gpiod_chip_label(c);
				bool hit = label && strcmp(label, chipLabel) == 0;
#endif
				if (hit) {
					match = c;
					log(SysGPIO, LogDebug,
					    "find_chip_by_label(%s): scan matched %s",
					    chipLabel, entries[i]->d_name);
				} else
					gpiod_chip_close(c);
			}
		}
		free(entries[i]);
	}
	free(entries);
	return match;
}

/**
 * Try to find the specified linux gpio line, throw exception if not found
 */
gpiod_line *LinuxGPIOPin::getLine(const char *chipLabel, const char *linuxPinName) {
  chip = find_chip_by_label(chipLabel);
  if (!chip)
    throw std::invalid_argument("GPIO chip not found");

#if GPIOD_V == 2
	struct gpiod_line_settings *settings;
	struct gpiod_line_config *line_cfg;
	struct gpiod_request_config *req_cfg = NULL;
	struct gpiod_line_request *line = NULL;
	// Returns -1 (ENOENT) for an unknown name; assigning that to the unsigned
	// member would request offset 4294967295 instead of reporting the typo.
	int named_offset = gpiod_chip_get_line_offset_from_name(chip, linuxPinName);
	if (named_offset < 0) {
		gpiod_chip_close(chip);
		chip = NULL;
		char msg[128];
		snprintf(msg, sizeof(msg), "Error, no GPIO line named '%s' on %s",
			linuxPinName ? linuxPinName : "?", chipLabel ? chipLabel : "?");
		throw std::invalid_argument(msg);
	}
	offset = (unsigned int) named_offset;
	settings = gpiod_line_settings_new();
	gpiod_line_settings_set_direction(settings, GPIOD_LINE_REQUEST_DIRECTION_AS_IS);
	line_cfg = gpiod_line_config_new();
	int cfg_ret = gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);
	if (cfg_ret != 0)
		log(SysGPIO, LogError, "gpiod_line_config_add_line_settings failed: %d", cfg_ret);
	req_cfg = gpiod_request_config_new();
	gpiod_request_config_set_consumer(req_cfg, consumer);
	line = gpiod_chip_request_lines(chip, req_cfg, line_cfg);

	gpiod_request_config_free(req_cfg);
	gpiod_line_config_free(line_cfg);
	gpiod_line_settings_free(settings);
	gpiod_chip_close(chip);
	chip = NULL;  // prevent double-close in ~LinuxGPIOPin()
	// gpiod_chip_request_lines() returns NULL when the line cannot be acquired
	// (e.g. already claimed by another process or a kernel driver). Fail loudly
	// here instead of returning NULL and asserting later in a reconfigure call.
	if (!line) {
		char msg[128];
		snprintf(msg, sizeof(msg),
			"Error, cannot acquire GPIO line '%s' on %s (already in use?)",
			linuxPinName ? linuxPinName : "?", chipLabel ? chipLabel : "?");
		throw std::invalid_argument(msg);
	}
	return line;
#else
	auto line = gpiod_chip_find_line(chip, linuxPinName);
	struct gpiod_line_request_config request = {
		consumer, GPIOD_LINE_REQUEST_DIRECTION_AS_IS, 0};
	auto result = gpiod_line_request(line, &request, 0);
	if(result != 0) {
		throw std::invalid_argument("Error, cannot open GPIO chip");
	}
	return line;
#endif
}

/**
 * Try to find the specified linux gpio line, throw exception if not found
 */
gpiod_line *LinuxGPIOPin::getLine(const char *chipLabel, const int linuxPinNum) {
  chip = find_chip_by_label(chipLabel);
  if (!chip)
    throw std::invalid_argument("GPIO chip not found");

  // Guard before either library path: a negative offset (a config parser's
  // "unset" sentinel, say) is unsigned on both sides -- v2's `offset` member
  // and v1's gpiod_chip_get_line() -- so it would wrap to 4294967295 and be
  // requested as if it were a real line.  Deliberately outside the #if: the
  // check needs no version-specific API.
  if (linuxPinNum < 0) {
    gpiod_chip_close(chip);
    chip = NULL;
    char msg[128];
    snprintf(msg, sizeof(msg), "Error, invalid GPIO line offset %d on %s",
             linuxPinNum, chipLabel ? chipLabel : "?");
    throw std::invalid_argument(msg);
  }

#if GPIOD_V == 2
	struct gpiod_line_settings *settings;
	struct gpiod_line_config *line_cfg;
	struct gpiod_request_config *req_cfg = NULL;
	struct gpiod_line_request *line = NULL;
	offset = (unsigned int) linuxPinNum;
	settings = gpiod_line_settings_new();
	gpiod_line_settings_set_direction(settings, GPIOD_LINE_REQUEST_DIRECTION_AS_IS);
	line_cfg = gpiod_line_config_new();
	int cfg_ret = gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);
	if (cfg_ret != 0)
		log(SysGPIO, LogError, "gpiod_line_config_add_line_settings failed: %d", cfg_ret);
	req_cfg = gpiod_request_config_new();
	gpiod_request_config_set_consumer(req_cfg, consumer);
	line = gpiod_chip_request_lines(chip, req_cfg, line_cfg);

	gpiod_request_config_free(req_cfg);
	gpiod_line_config_free(line_cfg);
	gpiod_line_settings_free(settings);
	gpiod_chip_close(chip);
	chip = NULL;  // prevent double-close in ~LinuxGPIOPin()
	// gpiod_chip_request_lines() returns NULL when the line cannot be acquired
	// (e.g. already claimed by another process or a kernel driver). Fail loudly
	// here instead of returning NULL and asserting later in a reconfigure call.
	if (!line) {
		char msg[128];
		snprintf(msg, sizeof(msg),
			"Error, cannot acquire GPIO line %d on %s (already in use?)",
			linuxPinNum, chipLabel ? chipLabel : "?");
		throw std::invalid_argument(msg);
	}
	return line;
#else
	// The negative guard above makes this conversion safe; make it explicit so
	// the intent is not mistaken for the sign bug that guard exists to prevent.
	auto line = gpiod_chip_get_line(chip, (unsigned int) linuxPinNum);

	struct gpiod_line_request_config request = {
		consumer, GPIOD_LINE_REQUEST_DIRECTION_AS_IS, 0};
	auto result = gpiod_line_request(line, &request, 0);
	if(result != 0) {
		throw std::invalid_argument("Error, cannot open GPIO chip");
	}
	return line;
#endif
}

/**
 * Create a pin given a linux chip label and pin name
 */
LinuxGPIOPin::LinuxGPIOPin(pin_size_t n, const char *chipLabel,
                           const char *linuxPinName,
                           const char *ardulinuxPinName)
    : GPIOPin(n, ardulinuxPinName ? ardulinuxPinName : linuxPinName) {
  line = getLine(chipLabel, linuxPinName);
}

LinuxGPIOPin::LinuxGPIOPin(pin_size_t n, const char *chipLabel,
                           const int linuxPinNum,
                           const char *ardulinuxPinName)
    : GPIOPin(n, ardulinuxPinName) {
  line = getLine(chipLabel, linuxPinNum);
}

LinuxGPIOPin::~LinuxGPIOPin() { 
    gpiod_line_release(line); 
    gpiod_chip_close(chip);
}

/**
 * Report a libgpiod failure on this line as an exception.
 *
 * assert() is not usable here: it is compiled out under NDEBUG, which is what
 * release builds define, so a runtime gpiod error would go unreported.
 */
void LinuxGPIOPin::throwLineError(const char *op) const {
  char msg[160];
#if GPIOD_V == 2
  snprintf(msg, sizeof(msg), "Error, cannot %s GPIO line %u ('%s', pin %u): %s",
           op, offset, getName(), (unsigned) getPinNum(), strerror(errno));
#else
  snprintf(msg, sizeof(msg), "Error, cannot %s GPIO '%s' (pin %u): %s",
           op, getName(), (unsigned) getPinNum(), strerror(errno));
#endif
  log(SysGPIO, LogError, "%s", msg);
  throw std::runtime_error(msg);
}

/// Read the low level hardware for this pin
PinStatus LinuxGPIOPin::readPinHardware() {
    int res = gpiod_line_get_value(line);
    // gpiod reports failure as GPIOD_LINE_VALUE_ERROR (-1). Returning it would
    // cache -1 as the pin state and fire a phantom ISR from refreshState().
    if (res != 0 && res != 1)
        throwLineError("read");

    // log(SysGPIO, LogDebug, "readPinHardware(%s, %d)", getName(), res); 
    return (PinStatus) res;
}

void LinuxGPIOPin::writePin(PinStatus s) {
  // some libraries have been observed failing to set the pin mode to output.
  if (GPIOPin::getPinMode() != OUTPUT)
	setPinMode(OUTPUT);

  // Drive the hardware before caching. GPIOPin::writePin() records `s` as the
  // pin's state, and once the mode is OUTPUT refreshState() stops re-reading
  // the hardware, so a value cached for a write that never landed would be
  // returned by digitalRead() forever.
  int res = gpiod_line_set_value(line, s);
  if (res != 0)
    throwLineError("write");

  GPIOPin::writePin(s); // update status
}

void LinuxGPIOPin::setPinMode(PinMode m) {
#if GPIOD_V == 2
  // Cache the mode up front: the output-value seed below reads readPin(), which
  // must return the cached level rather than touching the hardware. If the
  // reconfigure then fails, the cache is rolled back to `previous` -- leaving it
  // moved would gate refreshState() on a direction the line does not have.
  const PinMode previous = GPIOPin::getPinMode();
#endif
  GPIOPin::setPinMode(m);
#if GPIOD_V == 1
  // The gpiod call below does not play well with an already claimed GPIO
  // So we release it first.
  gpiod_line_release(line);

  // Set direction, if output use the current pinstate as the output value
  if (m == OUTPUT) {
    gpiod_line_request_output(line, consumer, readPin());
  } else {
    gpiod_line_request_input(line, consumer);

	if (m == INPUT_PULLUP) {
		auto error = gpiod_line_set_flags(line, GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP);
		if (error != 0) {
			char buf[1024];
			strcpy(buf, strerror(errno));
			printf("%d --> %s\n", errno, buf);
		}
	} else if (m == INPUT_PULLDOWN) {
		auto error = gpiod_line_set_flags(line, GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN);
		if (error != 0) {
			char buf[1024];
			strcpy(buf, strerror(errno));
			printf("%d --> %s\n", errno, buf);
		}
	}
  }
#else
	struct gpiod_line_settings *settings;
	struct gpiod_line_config *line_cfg;
	int ret;
	settings = gpiod_line_settings_new();
	if (m == OUTPUT) {
		gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
		gpiod_line_settings_set_output_value(settings, (gpiod_line_value) readPin());
	} else {
		gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
	}
	line_cfg = gpiod_line_config_new();
	ret = gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);
	int add_ret = ret;
	if (add_ret != 0)
		log(SysGPIO, LogError, "gpiod_line_config_add_line_settings failed: %d", add_ret);
	ret = gpiod_line_request_reconfigure_lines(line, line_cfg);
	if (ret != 0)
		log(SysGPIO, LogError, "gpiod_line_request_reconfigure_lines failed: %d", ret);
	// Either failure means the line kept its old direction, so the cache must
	// too.  add_line_settings is checked in its own right rather than trusting
	// the reconfigure to fail on an empty config: the two are independent.
	if (add_ret != 0 || ret != 0)
		GPIOPin::setPinMode(previous);

	gpiod_line_config_free(line_cfg);
	gpiod_line_settings_free(settings);


#endif
}

#undef consumer

#endif
