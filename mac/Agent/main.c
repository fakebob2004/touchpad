#include "MultitouchSupportABI.h"
#include "TouchFrame.h"

#include <CoreFoundation/CoreFoundation.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define FRAMEWORK_PATH "/System/Library/PrivateFrameworks/MultitouchSupport.framework/MultitouchSupport"
#define QUEUE_CAPACITY 256u

typedef struct {
    size_t length;
    uint8_t bytes[TP_MAX_MESSAGE_SIZE];
} WireMessage;

typedef struct {
    WireMessage items[QUEUE_CAPACITY];
    size_t head;
    size_t count;
    pthread_mutex_t mutex;
    pthread_cond_t available;
} MessageQueue;

typedef struct {
    const char *host;
    const char *port;
    MessageQueue queue;
    atomic_bool running;
    atomic_bool connected;
    atomic_bool reconnect_requested;
    atomic_uint_fast32_t sequence;
    atomic_uint_fast64_t captured;
    atomic_uint_fast64_t sent;
    atomic_uint_fast64_t dropped;
    atomic_uint_fast64_t reconnects;
    atomic_bool button_down;
    atomic_uint_fast64_t button_events;
    TPFrame latest_frame;
    pthread_mutex_t frame_mutex;
    FILE *button_trace;
    pthread_mutex_t trace_mutex;
} Agent;

typedef CFArrayRef (*CreateListFn)(void);
typedef void (*RegisterCallbackFn)(MTDeviceRef, MTContactFrameCallback);
typedef void (*RegisterButtonCallbackFn)(MTDeviceRef, MTButtonStateCallback, void *);
typedef void (*StartFn)(MTDeviceRef, int32_t);
typedef bool (*IsBuiltInFn)(MTDeviceRef);

static Agent g_agent;

static void queue_clear(MessageQueue *queue);
static bool queue_push(MessageQueue *queue, const WireMessage *message);

static uint64_t monotonic_microseconds(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return 0;
    }
    return (uint64_t)value.tv_sec * 1000000ULL + (uint64_t)value.tv_nsec / 1000ULL;
}

static uint8_t flags_for_state(int32_t state) {
    uint8_t flags = TP_CONTACT_CONFIDENCE;
    if (state >= 1 && state <= 6) {
        flags |= TP_CONTACT_IN_RANGE;
    }
    if (state == 3 || state == 4) {
        flags |= TP_CONTACT_TIP;
    }
    return flags;
}

static void trace_callback(Agent *agent, int32_t pressed, int32_t released) {
    if (agent->button_trace == NULL) {
        return;
    }
    pthread_mutex_lock(&agent->trace_mutex);
    fprintf(agent->button_trace,
            "{\"event\":\"button_callback\",\"time_us\":%llu,"
            "\"pressed\":%d,\"released\":%d}\n",
            (unsigned long long)monotonic_microseconds(),
            pressed,
            released);
    fflush(agent->button_trace);
    pthread_mutex_unlock(&agent->trace_mutex);
}

static void trace_frame(Agent *agent, const TPFrame *frame, const char *origin) {
    if (agent->button_trace == NULL) {
        return;
    }
    pthread_mutex_lock(&agent->trace_mutex);
    fprintf(agent->button_trace,
            "{\"event\":\"frame\",\"time_us\":%llu,\"origin\":\"%s\","
            "\"sequence\":%u,\"frame_flags\":%u,\"button\":%s,"
            "\"contact_count\":%u}\n",
            (unsigned long long)frame->capture_time_us,
            origin,
            frame->sequence,
            frame->flags,
            (frame->flags & TP_FRAME_BUTTON) != 0 ? "true" : "false",
            frame->contact_count);
    fflush(agent->button_trace);
    pthread_mutex_unlock(&agent->trace_mutex);
}

static void trace_connection(Agent *agent) {
    if (agent->button_trace == NULL) {
        return;
    }
    pthread_mutex_lock(&agent->trace_mutex);
    fprintf(agent->button_trace,
            "{\"event\":\"connection\",\"time_us\":%llu,\"connected\":true}\n",
            (unsigned long long)monotonic_microseconds());
    fflush(agent->button_trace);
    pthread_mutex_unlock(&agent->trace_mutex);
}

static void enqueue_frame_locked(Agent *agent, TPFrame *frame, const char *origin) {
    frame->sequence =
        atomic_fetch_add_explicit(&agent->sequence, 1, memory_order_relaxed) + 1;
    frame->capture_time_us = monotonic_microseconds();
    WireMessage message;
    message.length =
        tp_encode_message(TP_MESSAGE_FRAME, frame, message.bytes, sizeof(message.bytes));
    if (message.length != 0 && queue_push(&agent->queue, &message)) {
        trace_frame(agent, frame, origin);
    } else {
        /*
         * Assigned but unsent sequence numbers cannot be skipped inside a
         * strict MTP1 session. Reconnect to establish a fresh HELLO/RESET epoch.
         */
        atomic_store_explicit(&agent->connected, false, memory_order_release);
        atomic_store_explicit(&agent->reconnect_requested, true, memory_order_relaxed);
        queue_clear(&agent->queue);
        atomic_fetch_add_explicit(&agent->dropped, 1, memory_order_relaxed);
    }
}

static void button_callback(MTDeviceRef device,
                            int32_t pressed,
                            int32_t released,
                            void *context) {
    (void)device;
    Agent *agent = context;
    trace_callback(agent, pressed, released);
    if (pressed != 0) {
        atomic_store_explicit(&agent->button_down, true, memory_order_release);
    }
    if (released != 0) {
        atomic_store_explicit(&agent->button_down, false, memory_order_release);
    }
    atomic_fetch_add_explicit(&agent->button_events, 1, memory_order_relaxed);
    if (!atomic_load_explicit(&agent->connected, memory_order_acquire)) {
        return;
    }

    pthread_mutex_lock(&agent->frame_mutex);
    TPFrame frame = agent->latest_frame;
    frame.flags = atomic_load_explicit(&agent->button_down, memory_order_acquire)
                      ? TP_FRAME_BUTTON
                      : 0;
    frame.device_time_us = 0;
    agent->latest_frame = frame;
    enqueue_frame_locked(agent, &frame, "button_callback");
    pthread_mutex_unlock(&agent->frame_mutex);
}

static void queue_clear(MessageQueue *queue) {
    pthread_mutex_lock(&queue->mutex);
    queue->head = 0;
    queue->count = 0;
    pthread_mutex_unlock(&queue->mutex);
}

static bool queue_push(MessageQueue *queue, const WireMessage *message) {
    bool accepted = true;
    pthread_mutex_lock(&queue->mutex);
    if (queue->count == QUEUE_CAPACITY) {
        accepted = false;
    } else {
        const size_t tail = (queue->head + queue->count) % QUEUE_CAPACITY;
        queue->items[tail] = *message;
        ++queue->count;
        pthread_cond_signal(&queue->available);
    }
    pthread_mutex_unlock(&queue->mutex);
    return accepted;
}

static bool queue_pop(MessageQueue *queue, WireMessage *message) {
    pthread_mutex_lock(&queue->mutex);
    while (queue->count == 0 &&
           atomic_load_explicit(&g_agent.running, memory_order_relaxed) &&
           !atomic_load_explicit(&g_agent.reconnect_requested, memory_order_relaxed)) {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_nsec += 100000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            ++deadline.tv_sec;
            deadline.tv_nsec -= 1000000000L;
        }
        (void)pthread_cond_timedwait(&queue->available, &queue->mutex, &deadline);
    }
    if (queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return false;
    }
    *message = queue->items[queue->head];
    queue->head = (queue->head + 1) % QUEUE_CAPACITY;
    --queue->count;
    pthread_mutex_unlock(&queue->mutex);
    return true;
}

static bool send_all(int socket_fd, const uint8_t *bytes, size_t length) {
    while (length > 0) {
        const ssize_t sent = send(socket_fd, bytes, length, 0);
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent <= 0) {
            return false;
        }
        bytes += (size_t)sent;
        length -= (size_t)sent;
    }
    return true;
}

static int connect_to_receiver(const char *host, const char *port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *addresses = NULL;
    const int lookup = getaddrinfo(host, port, &hints, &addresses);
    if (lookup != 0) {
        fprintf(stderr, "receiver lookup failed: %s\n", gai_strerror(lookup));
        return -1;
    }

    int connected_socket = -1;
    for (struct addrinfo *address = addresses; address != NULL; address = address->ai_next) {
        const int candidate = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (candidate < 0) {
            continue;
        }
        int enabled = 1;
        (void)setsockopt(candidate, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
        (void)setsockopt(candidate, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
        if (connect(candidate, address->ai_addr, address->ai_addrlen) == 0) {
            connected_socket = candidate;
            break;
        }
        close(candidate);
    }
    freeaddrinfo(addresses);
    return connected_socket;
}

static bool send_control(int socket_fd, enum TPMessageType type) {
    TPFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.sequence =
        atomic_fetch_add_explicit(&g_agent.sequence, 1, memory_order_relaxed) + 1;
    frame.capture_time_us = monotonic_microseconds();
    WireMessage message;
    message.length = tp_encode_message(type, &frame, message.bytes, sizeof(message.bytes));
    return message.length > 0 && send_all(socket_fd, message.bytes, message.length);
}

static void *sender_main(void *context) {
    Agent *agent = context;
    while (atomic_load_explicit(&agent->running, memory_order_relaxed)) {
        const int socket_fd = connect_to_receiver(agent->host, agent->port);
        if (socket_fd < 0) {
            sleep(1);
            continue;
        }
        atomic_fetch_add_explicit(&agent->reconnects, 1, memory_order_relaxed);
        queue_clear(&agent->queue);
        if (!send_control(socket_fd, TP_MESSAGE_HELLO) ||
            !send_control(socket_fd, TP_MESSAGE_RESET)) {
            close(socket_fd);
            continue;
        }
        atomic_store_explicit(&agent->reconnect_requested, false, memory_order_relaxed);
        pthread_mutex_lock(&agent->frame_mutex);
        memset(&agent->latest_frame, 0, sizeof(agent->latest_frame));
        pthread_mutex_unlock(&agent->frame_mutex);
        atomic_store_explicit(&agent->connected, true, memory_order_release);
        trace_connection(agent);
        fprintf(stderr, "connected to %s:%s\n", agent->host, agent->port);

        WireMessage message;
        while (atomic_load_explicit(&agent->running, memory_order_relaxed) &&
               !atomic_load_explicit(&agent->reconnect_requested, memory_order_relaxed) &&
               queue_pop(&agent->queue, &message)) {
            if (!send_all(socket_fd, message.bytes, message.length)) {
                break;
            }
            atomic_fetch_add_explicit(&agent->sent, 1, memory_order_relaxed);
        }
        atomic_store_explicit(&agent->connected, false, memory_order_release);
        queue_clear(&agent->queue);
        close(socket_fd);
        fprintf(stderr, "receiver disconnected; retrying\n");
    }
    return NULL;
}

static void contact_callback(MTDeviceRef device,
                             MTTouch *touches,
                             size_t touch_count,
                             double timestamp,
                             size_t frame_number) {
    (void)device;
    (void)frame_number;
    atomic_fetch_add_explicit(&g_agent.captured, 1, memory_order_relaxed);
    if (!atomic_load_explicit(&g_agent.connected, memory_order_acquire)) {
        return;
    }
    if (touch_count > TP_MAX_CONTACTS || (touch_count > 0 && touches == NULL)) {
        atomic_fetch_add_explicit(&g_agent.dropped, 1, memory_order_relaxed);
        return;
    }

    pthread_mutex_lock(&g_agent.frame_mutex);
    TPFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.device_time_us = timestamp > 0 ? (uint64_t)(timestamp * 1000000.0) : 0;
    frame.flags = atomic_load_explicit(&g_agent.button_down, memory_order_acquire)
                      ? TP_FRAME_BUTTON
                      : 0;
    frame.contact_count = (uint16_t)touch_count;
    for (size_t index = 0; index < touch_count; ++index) {
        const MTTouch *source = &touches[index];
        TPContact *destination = &frame.contacts[index];
        destination->identifier = (uint32_t)source->identifier;
        destination->state = (uint8_t)source->state;
        destination->flags = flags_for_state(source->state);
        destination->x = source->normalized.position.x;
        destination->y = source->normalized.position.y;
        destination->velocity_x = source->normalized.velocity.x;
        destination->velocity_y = source->normalized.velocity.y;
        destination->size = source->size;
        destination->angle = source->angle;
        destination->major_axis = source->major_axis;
        destination->minor_axis = source->minor_axis;
        destination->density = source->density;
    }
    g_agent.latest_frame = frame;
    enqueue_frame_locked(&g_agent, &frame, "contact_callback");
    pthread_mutex_unlock(&g_agent.frame_mutex);
}

static void stop_agent(int signal_number) {
    (void)signal_number;
    atomic_store_explicit(&g_agent.running, false, memory_order_relaxed);
}

static void *load_symbol(void *framework, const char *name, bool required) {
    dlerror();
    void *symbol = dlsym(framework, name);
    const char *error = dlerror();
    if (required && error != NULL) {
        fprintf(stderr, "missing private symbol %s: %s\n", name, error);
        return NULL;
    }
    return symbol;
}

static int usage(const char *program) {
    fprintf(stderr,
            "usage: %s HOST [PORT] [--duration SECONDS] [--button-trace PATH]\n",
            program);
    return 2;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return usage(argv[0]);
    }
    const char *host = argv[1];
    const char *port = "39871";
    const char *button_trace_path = NULL;
    int duration = 0;
    bool port_seen = false;
    for (int index = 2; index < argc; ++index) {
        if (strcmp(argv[index], "--duration") == 0) {
            if (++index >= argc) {
                return usage(argv[0]);
            }
            duration = atoi(argv[index]);
            if (duration <= 0 || duration > 86400) {
                return usage(argv[0]);
            }
        } else if (strcmp(argv[index], "--button-trace") == 0) {
            if (++index >= argc || argv[index][0] == '\0') {
                return usage(argv[0]);
            }
            button_trace_path = argv[index];
        } else if (!port_seen) {
            port = argv[index];
            port_seen = true;
        } else {
            return usage(argv[0]);
        }
    }

    memset(&g_agent, 0, sizeof(g_agent));
    g_agent.host = host;
    g_agent.port = port;
    atomic_init(&g_agent.running, true);
    atomic_init(&g_agent.connected, false);
    atomic_init(&g_agent.reconnect_requested, false);
    pthread_mutex_init(&g_agent.queue.mutex, NULL);
    pthread_cond_init(&g_agent.queue.available, NULL);
    pthread_mutex_init(&g_agent.frame_mutex, NULL);
    pthread_mutex_init(&g_agent.trace_mutex, NULL);
    if (button_trace_path != NULL) {
        g_agent.button_trace = fopen(button_trace_path, "w");
        if (g_agent.button_trace == NULL) {
            fprintf(stderr,
                    "cannot open button trace %s: %s\n",
                    button_trace_path,
                    strerror(errno));
            return 1;
        }
        fprintf(stderr, "button trace: %s\n", button_trace_path);
    }
    signal(SIGINT, stop_agent);
    signal(SIGTERM, stop_agent);
    signal(SIGPIPE, SIG_IGN);

    pthread_t sender;
    if (pthread_create(&sender, NULL, sender_main, &g_agent) != 0) {
        fprintf(stderr, "cannot create sender thread\n");
        return 1;
    }
    void *framework = dlopen(FRAMEWORK_PATH, RTLD_NOW | RTLD_LOCAL);
    if (framework == NULL) {
        fprintf(stderr, "cannot load MultitouchSupport: %s\n", dlerror());
        stop_agent(0);
        pthread_join(sender, NULL);
        return 1;
    }
    CreateListFn create_list = NULL;
    RegisterCallbackFn register_callback = NULL;
    RegisterButtonCallbackFn register_button_callback = NULL;
    StartFn start = NULL;
    IsBuiltInFn is_built_in = NULL;
    *(void **)(&create_list) = load_symbol(framework, "MTDeviceCreateList", true);
    *(void **)(&register_callback) = load_symbol(framework, "MTRegisterContactFrameCallback", true);
    *(void **)(&register_button_callback) =
        load_symbol(framework, "MTRegisterButtonStateCallback", false);
    *(void **)(&start) = load_symbol(framework, "MTDeviceStart", true);
    *(void **)(&is_built_in) = load_symbol(framework, "MTDeviceIsBuiltIn", false);
    if (create_list == NULL || register_callback == NULL || start == NULL) {
        stop_agent(0);
        pthread_join(sender, NULL);
        return 1;
    }

    fflush(stdout);
    const int saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout >= 0) {
        (void)dup2(STDERR_FILENO, STDOUT_FILENO);
    }
    CFArrayRef devices = create_list();
    if (devices != NULL) {
        CFRetain(devices);
    }
    int started = 0;
    if (devices != NULL) {
        for (CFIndex device_index = 0; device_index < CFArrayGetCount(devices); ++device_index) {
            MTDeviceRef device = (MTDeviceRef)CFArrayGetValueAtIndex(devices, device_index);
            if (is_built_in != NULL && !is_built_in(device)) {
                continue;
            }
            register_callback(device, contact_callback);
            if (register_button_callback != NULL) {
                register_button_callback(device, button_callback, &g_agent);
            }
            start(device, 0);
            ++started;
        }
    }
    fflush(stdout);
    if (saved_stdout >= 0) {
        (void)dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdout);
    }
    if (started == 0) {
        fprintf(stderr, "no built-in multitouch device found\n");
        stop_agent(0);
        pthread_join(sender, NULL);
        return 1;
    }

    fprintf(stderr,
            "streaming %d built-in trackpad(s) to %s:%s; physical button=%s\n",
            started,
            host,
            port,
            register_button_callback != NULL ? "enabled" : "unavailable");
    const uint64_t started_at = monotonic_microseconds();
    while (atomic_load_explicit(&g_agent.running, memory_order_relaxed)) {
        usleep(100000);
        if (duration > 0 && monotonic_microseconds() - started_at >= (uint64_t)duration * 1000000ULL) {
            stop_agent(0);
        }
    }
    pthread_join(sender, NULL);
    fprintf(stderr,
            "summary: captured=%llu sent=%llu dropped=%llu connections=%llu button_events=%llu\n",
            (unsigned long long)atomic_load_explicit(&g_agent.captured, memory_order_relaxed),
            (unsigned long long)atomic_load_explicit(&g_agent.sent, memory_order_relaxed),
            (unsigned long long)atomic_load_explicit(&g_agent.dropped, memory_order_relaxed),
            (unsigned long long)atomic_load_explicit(&g_agent.reconnects, memory_order_relaxed),
            (unsigned long long)atomic_load_explicit(&g_agent.button_events, memory_order_relaxed));
    return 0;
}
