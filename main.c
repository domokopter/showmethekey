// ============================================================================
// Compiler Directives and Macros
// ============================================================================

// INFO: To run the file:
// clang -o main main.c -ludev -levdev -linput &&  sudo ./main

#define DEBUG // Comment out to disable debug mode

#ifdef DEBUG
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif

// ============================================================================
// Header Files
// ============================================================================

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <string.h>
// #include <libevdev/libevdev.h> // Arch has a different file path
#include <libevdev-1.0/libevdev/libevdev.h>
#include <libinput.h>
#include <libudev.h>
#include <stdio.h>
#include <unistd.h>

// ============================================================================
// Constants
// ============================================================================

// TODO: Automatically generate the version number from the git tag.
static const char *PROJECT_VERSION = "0.1.0 (alpha)";

// ============================================================================
// Enumerations
// ============================================================================

enum error_code {
    NO_ERROR,        // 0
    UDEV_FAILED,     // 1
    LIBINPUT_FAILED, // 2
    SEAT_FAILED,     // 3
};

// ============================================================================
// Libinput Interface
// ============================================================================

static int open_restricted(const char *path, int flags, void *user_data) {
    int fd = open(path, flags);
    if (fd < 0)
        fprintf(stderr, "Failed to open %s because of %s.\n", path,
                strerror(errno));
    return fd < 0 ? -errno : fd;
}

static void close_restricted(int fd, void *user_data) { close(fd); }

static const struct libinput_interface interface = {
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

// ============================================================================
// Function Declarations
// ============================================================================

static void print_help(const char *program_name);
static int print_key_event(struct libinput_event *event);
static int handle_events(struct libinput *libinput);

// ============================================================================
// Function Definitions
// ============================================================================

static void print_help(const char *program_name) {
    printf("The backend of TypeTrace.\n");
    printf("Version: %s\n", PROJECT_VERSION);
    printf("Usage: %s [OPTION…]\n", program_name);
    printf("Options:\n");
    printf("\t-h, --help\tDisplay help then exit.\n");
    printf("\t-v, --version\tDisplay version then exit.\n");
    printf("Warning: This is the backend and is not designed to run by users. "
           "You should run the frontend of TypeTrace which will run this.\n");
}

static int print_key_event(struct libinput_event *event) {
    struct libinput_event_keyboard *keyboard =
        libinput_event_get_keyboard_event(event);
    if (!keyboard)
        return -1;

    // Only process key presses (state == 1), ignore releases (state == 0)
    if (libinput_event_keyboard_get_key_state(keyboard) !=
        LIBINPUT_KEY_STATE_PRESSED) {
        return 0;
    }

    uint32_t key_code = libinput_event_keyboard_get_key(keyboard);
    const char *key_name = libevdev_event_code_get_name(EV_KEY, key_code);
    key_name = key_name ? key_name : "unknown";

    // JSON format
    return printf("{\"key_name\": \"%s\", \"key_code\": %d}\n", key_name,
                  key_code);
}

static int handle_events(struct libinput *libinput) {
    int result = NO_ERROR;

    if (libinput_dispatch(libinput) < 0) {
        fprintf(stderr, "Failed to dispatch libinput events.\n");
        return LIBINPUT_FAILED;
    }

    struct libinput_event *event;
    while ((event = libinput_get_event(libinput))) {
        if (libinput_event_get_type(event) == LIBINPUT_EVENT_KEYBOARD_KEY) {
            if (print_key_event(event) < 0) {
                result = LIBINPUT_FAILED;
            }
        }
        libinput_event_destroy(event);
    }

    return result;
}

// ============================================================================
// Main Function
// ============================================================================

int main(int argc, char *argv[]) {
    const struct option options[] = {
        {"version", no_argument, NULL, 'v'},
        {"help",    no_argument, NULL, 'h'},
        {NULL,      0,           NULL, 0  }
    };

    // Parse command line arguments
    for (int opt = getopt_long(argc, argv, "vh", options, NULL); opt != -1;) {
        switch (opt) {
        case 'v':
            printf("%s\n", PROJECT_VERSION);
            return NO_ERROR;
        case 'h':
            print_help(argv[0]);
            return NO_ERROR;
        default:
            fprintf(stderr, "%s: Invalid option `-%c`.\n", argv[0], opt);
            break;
        }
    }

    // Initialize udev
    struct udev *udev = udev_new();
    if (udev == NULL) {
        fprintf(stderr, "Failed to initialize udev.\n");
        return UDEV_FAILED;
    }
    DEBUG_PRINT("udev initialized successfully.\n");

    // Initialize libinput
    struct libinput *libinput =
        libinput_udev_create_context(&interface, NULL, udev);
    if (!libinput) {
        fprintf(stderr, "Failed to initialize libinput from udev.\n");
        udev_unref(udev);
        return LIBINPUT_FAILED;
    }

    // Assign default seat
    if (libinput_udev_assign_seat(libinput, "seat0") < 0) {
        fprintf(stderr, "Failed to assign seat0 to libinput.\n");
        libinput_unref(libinput);
        udev_unref(udev);
        return SEAT_FAILED;
    }
    DEBUG_PRINT("libinput initialized successfully with seat0.\n");

    // TODO: Rewrite this loop
    while (1) {
        int result = handle_events(libinput);
        if (result != NO_ERROR) {
            fprintf(stderr, "Event handling failed with code %d.\n", result);
            libinput_unref(libinput);
            udev_unref(udev);
            return result;
        }
        usleep(1000); // Sleep 1ms to avoid busy-waiting -> really needed?
    }

    // Cleanup
    libinput_unref(libinput);
    udev_unref(udev);
}
