include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(ardulinux-tests
    test_gpio.cpp
    test_spi.cpp
    test_spi_stubs.cpp
    test_linux_spi.cpp
    test_serial.cpp
    test_sim_serial.cpp
    test_sim_i2c.cpp
    test_millis.cpp
    test_linux_common.cpp
    test_vfs.cpp
    test_itoa.cpp
    test_utility.cpp
    test_logging.cpp
    test_fs.cpp
    test_xdg_dirs.cpp
    test_app_info.cpp
    # Always use simulated I2C for tests (no real hardware needed)
    ${CMAKE_SOURCE_DIR}/cores/ardulinux/simulated/SimHardwareI2C.cpp
)

target_link_libraries(ardulinux-tests PRIVATE ardulinux-base Catch2::Catch2WithMain)

# Coverage: instrument test sources and link the gcov runtime.
# PRIVATE so Catch2::Catch2WithMain is not affected.
if(CMAKE_BUILD_TYPE STREQUAL "Coverage")
    target_compile_options(ardulinux-tests PRIVATE --coverage -O0 -g)
    target_link_options(ardulinux-tests PRIVATE --coverage)
endif()

include(Catch)
catch_discover_tests(ardulinux-tests)

# ─── Print::printf tests ─────────────────────────────────────────────────────
# printf() lives only in the ArduLinux shadow Print.h (upstream has none).  This
# separate executable puts cores/ardulinux ahead of cores/arduino/api on the
# include path so test_printf.cpp compiles against the shadow (the rest of the
# build is API-first).  It links ardulinux-base for arduino::Print's vtable and
# out-of-line members; those symbols match the shadow's view because printf is a
# non-virtual inline addition, so class layout, vtable and typeinfo are unchanged.
add_executable(ardulinux-printf-tests test_printf.cpp)
target_include_directories(ardulinux-printf-tests BEFORE PRIVATE
    ${CMAKE_SOURCE_DIR}/cores/ardulinux        # shadow Print.h (with printf) wins
    ${CMAKE_SOURCE_DIR}/cores/arduino/api      # String.h / Printable.h it includes
)
target_compile_definitions(ardulinux-printf-tests PRIVATE HOST)
target_link_libraries(ardulinux-printf-tests PRIVATE ardulinux-base Catch2::Catch2WithMain)
if(CMAKE_BUILD_TYPE STREQUAL "Coverage")
    target_compile_options(ardulinux-printf-tests PRIVATE --coverage -O0 -g)
    target_link_options(ardulinux-printf-tests PRIVATE --coverage)
endif()
catch_discover_tests(ardulinux-printf-tests)

# ─── LinuxHardwareI2C buffer tests ───────────────────────────────────────────
# These tests exercise the RX buffer logic and the signed/unsigned underflow
# guard in LinuxHardwareI2C::read() and ::available().  They do not touch real
# I2C hardware; the buffer variables are pre-populated via extern declarations.
#
# A separate executable is required because LinuxHardwareI2C.cpp and
# SimHardwareI2C.cpp both define the global arduino::Wire instance, and they
# cannot be linked together.  This target is only built when LIBGPIOD_FOUND
# (which is when LinuxHardwareI2C.cpp is available and libi2c-dev is present).
if(LIBGPIOD_FOUND)
    add_executable(ardulinux-i2c-tests
        test_linux_i2c.cpp
        ${CMAKE_SOURCE_DIR}/cores/ardulinux/linux/LinuxHardwareI2C.cpp
    )
    target_link_libraries(ardulinux-i2c-tests PRIVATE ardulinux-base Catch2::Catch2WithMain)
    target_include_directories(ardulinux-i2c-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/cores/ardulinux/linux
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Coverage")
        target_compile_options(ardulinux-i2c-tests PRIVATE --coverage -O0 -g)
        target_link_options(ardulinux-i2c-tests PRIVATE --coverage)
    endif()
    catch_discover_tests(ardulinux-i2c-tests)
endif()

# ─── LinuxGPIOPin tests ──────────────────────────────────────────────────────
# LinuxGPIOPin drives real GPIO character devices, which a unit test cannot
# open (/dev/gpiochip* needs hardware, or gpio-sim plus root).  fake_gpiod.cpp
# defines the libgpiod symbols LinuxGPIOPin.cpp references; because those
# definitions come from an object file linked directly into this executable,
# they win over the shared library and no device is ever touched.
#
# A separate executable is required so the fake's gpiod_* symbols cannot
# displace the real ones in ardulinux-tests, which links libgpiod for the I2C
# and SPI paths.  Built only when LIBGPIOD_FOUND, since LinuxGPIOPin.cpp is
# guarded by ARDULINUX_HARDWARE and needs <gpiod.h>.
if(LIBGPIOD_FOUND)
    add_executable(ardulinux-gpio-tests
        test_linux_gpio.cpp
        fake_gpiod.cpp
        ${CMAKE_SOURCE_DIR}/cores/ardulinux/linux/gpio/LinuxGPIOPin.cpp
    )
    target_link_libraries(ardulinux-gpio-tests PRIVATE ardulinux-base Catch2::Catch2WithMain)
    target_include_directories(ardulinux-gpio-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/cores/ardulinux
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Coverage")
        target_compile_options(ardulinux-gpio-tests PRIVATE --coverage -O0 -g)
        target_link_options(ardulinux-gpio-tests PRIVATE --coverage)
    endif()
    catch_discover_tests(ardulinux-gpio-tests)
endif()
