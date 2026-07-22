#include "MultitouchSupportABI.h"

#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define FRAMEWORK_PATH "/System/Library/PrivateFrameworks/MultitouchSupport.framework/MultitouchSupport"
#define MAX_REASONABLE_TOUCHES 32

typedef struct {
    void *framework;
    CFArrayRef devices;
    MTDeviceCreateListFn create_list;
    MTRegisterContactFrameCallbackFn register_callback;
    MTDeviceStartFn start;
    MTDeviceIsBuiltInFn is_built_in;
} Runtime;

static atomic_bool g_running = true;
static atomic_uint_fast64_t g_frame_count = 0;
static atomic_uint_fast64_t g_touch_count = 0;
static atomic_uint_fast64_t g_invalid_frame_count = 0;
static atomic_uint_fast32_t g_max_touches = 0;

static void on_signal(int signal_number) {
    (void)signal_number;
    atomic_store_explicit(&g_running, false, memory_order_relaxed);
}

static uint64_t monotonic_microseconds(void) {
    struct timespec time_value;
    if (clock_gettime(CLOCK_MONOTONIC, &time_value) != 0) {
        return 0;
    }
    return (uint64_t)time_value.tv_sec * 1000000ULL + (uint64_t)time_value.tv_nsec / 1000ULL;
}

static void update_max_touches(uint32_t candidate) {
    uint_fast32_t current = atomic_load_explicit(&g_max_touches, memory_order_relaxed);
    while (candidate > current &&
           !atomic_compare_exchange_weak_explicit(&g_max_touches,
                                                  &current,
                                                  candidate,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
    }
}

static void contact_callback(MTDeviceRef device,
                             MTTouch *touches,
                             size_t touch_count,
                             double timestamp,
                             size_t frame) {
    if (touch_count > MAX_REASONABLE_TOUCHES || (touch_count > 0 && touches == NULL)) {
        atomic_fetch_add_explicit(&g_invalid_frame_count, 1, memory_order_relaxed);
        return;
    }

    atomic_fetch_add_explicit(&g_frame_count, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_touch_count, touch_count, memory_order_relaxed);
    update_max_touches((uint32_t)touch_count);

    flockfile(stdout);
    fprintf(stdout,
            "{\"type\":\"frame\",\"capture_time_us\":%llu,\"device\":\"%p\","
            "\"frame\":%zu,\"device_timestamp\":%.9f,\"contact_count\":%zu,\"contacts\":[",
            (unsigned long long)monotonic_microseconds(),
            device,
            frame,
            timestamp,
            touch_count);

    for (size_t index = 0; index < touch_count; ++index) {
        const MTTouch *touch = &touches[index];
        if (index != 0) {
            fputc(',', stdout);
        }
        fprintf(stdout,
                "{\"id\":%d,\"state\":%d,\"x\":%.7g,\"y\":%.7g,"
                "\"vx\":%.7g,\"vy\":%.7g,\"size\":%.7g,\"angle\":%.7g,"
                "\"major\":%.7g,\"minor\":%.7g,\"density\":%.7g,"
                "\"mm_x\":%.7g,\"mm_y\":%.7g}",
                touch->identifier,
                touch->state,
                touch->normalized.position.x,
                touch->normalized.position.y,
                touch->normalized.velocity.x,
                touch->normalized.velocity.y,
                touch->size,
                touch->angle,
                touch->major_axis,
                touch->minor_axis,
                touch->density,
                touch->millimeters.position.x,
                touch->millimeters.position.y);
    }
    fputs("]}\n", stdout);
    funlockfile(stdout);
}

static void *required_symbol(void *framework, const char *name) {
    dlerror();
    void *symbol = dlsym(framework, name);
    const char *error = dlerror();
    if (error != NULL) {
        fprintf(stderr, "error: missing private symbol %s: %s\n", name, error);
        return NULL;
    }
    return symbol;
}

static bool load_runtime(Runtime *runtime) {
    memset(runtime, 0, sizeof(*runtime));
    runtime->framework = dlopen(FRAMEWORK_PATH, RTLD_NOW | RTLD_LOCAL);
    if (runtime->framework == NULL) {
        fprintf(stderr, "error: cannot load %s: %s\n", FRAMEWORK_PATH, dlerror());
        return false;
    }

    *(void **)(&runtime->create_list) = required_symbol(runtime->framework, "MTDeviceCreateList");
    *(void **)(&runtime->register_callback) =
        required_symbol(runtime->framework, "MTRegisterContactFrameCallback");
    *(void **)(&runtime->start) = required_symbol(runtime->framework, "MTDeviceStart");
    *(void **)(&runtime->is_built_in) = dlsym(runtime->framework, "MTDeviceIsBuiltIn");

    return runtime->create_list != NULL && runtime->register_callback != NULL && runtime->start != NULL;
}

static int parse_duration(int argument_count, char **arguments) {
    if (argument_count == 1) {
        return 0;
    }
    if (argument_count != 3 || strcmp(arguments[1], "--duration") != 0) {
        fprintf(stderr, "usage: %s [--duration SECONDS]\n", arguments[0]);
        return -1;
    }

    char *end = NULL;
    errno = 0;
    long duration = strtol(arguments[2], &end, 10);
    if (errno != 0 || end == arguments[2] || *end != '\0' || duration <= 0 || duration > 86400) {
        fprintf(stderr, "error: duration must be an integer from 1 to 86400\n");
        return -1;
    }
    return (int)duration;
}

int main(int argument_count, char **arguments) {
    const int duration = parse_duration(argument_count, arguments);
    if (duration < 0) {
        return 2;
    }

    Runtime runtime;
    if (!load_runtime(&runtime)) {
        return 1;
    }

    // The private framework prints a hardware-family banner to stdout while
    // enumerating on some macOS versions. Keep stdout machine-readable JSONL.
    fflush(stdout);
    const int saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout >= 0) {
        (void)dup2(STDERR_FILENO, STDOUT_FILENO);
    }
    runtime.devices = runtime.create_list();
    if (runtime.devices == NULL) {
        fprintf(stderr, "error: MTDeviceCreateList returned NULL\n");
        return 1;
    }
    CFRetain(runtime.devices);

    const CFIndex device_count = CFArrayGetCount(runtime.devices);
    fprintf(stderr, "found %ld multitouch device(s)\n", (long)device_count);
    int started_devices = 0;
    for (CFIndex index = 0; index < device_count; ++index) {
        MTDeviceRef device = (MTDeviceRef)CFArrayGetValueAtIndex(runtime.devices, index);
        const bool built_in = runtime.is_built_in == NULL || runtime.is_built_in(device);
        fprintf(stderr, "device %ld: %p built_in=%s\n", (long)index, device, built_in ? "yes" : "no");
        if (!built_in) {
            continue;
        }
        runtime.register_callback(device, contact_callback);
        runtime.start(device, 0);
        ++started_devices;
    }
    fflush(stdout);
    if (saved_stdout >= 0) {
        (void)dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
    }

    if (started_devices == 0) {
        fprintf(stderr, "error: no built-in multitouch device was available\n");
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    const uint64_t start_time = monotonic_microseconds();
    fprintf(stderr, "capturing from %d built-in device(s); press Ctrl-C to stop\n", started_devices);

    while (atomic_load_explicit(&g_running, memory_order_relaxed)) {
        usleep(100000);
        if (duration > 0 && monotonic_microseconds() - start_time >= (uint64_t)duration * 1000000ULL) {
            break;
        }
    }

    const double elapsed = (double)(monotonic_microseconds() - start_time) / 1000000.0;
    const uint64_t frames = atomic_load_explicit(&g_frame_count, memory_order_relaxed);
    fprintf(stderr,
            "summary: elapsed=%.3fs frames=%llu overall_rate=%.2fHz contacts=%llu max_contacts=%u invalid_frames=%llu\n",
            elapsed,
            (unsigned long long)frames,
            elapsed > 0.0 ? (double)frames / elapsed : 0.0,
            (unsigned long long)atomic_load_explicit(&g_touch_count, memory_order_relaxed),
            (unsigned int)atomic_load_explicit(&g_max_touches, memory_order_relaxed),
            (unsigned long long)atomic_load_explicit(&g_invalid_frame_count, memory_order_relaxed));

    // Deliberately do not call MTDeviceStop/Unregister here. Their private ABI and
    // callback-drain semantics vary by macOS release; process teardown is safer for this probe.
    return 0;
}
