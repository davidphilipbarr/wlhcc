#define _POSIX_C_SOURCE 200809L
#define UNUSED(x) (void)(x)
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>

#include "single-pixel-buffer-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "viewporter-client-protocol.h"

struct state {
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct zwlr_layer_shell_v1 *layer_shell;
  struct wp_single_pixel_buffer_manager_v1 *buffer_manager;
  struct wp_viewporter *viewporter;
  struct wl_shm *shm;
  struct wl_seat *seat;
  struct wl_pointer *pointer;

  struct wl_list outputs;
  struct wl_buffer *buffer;

  uint32_t anchor;
  char *cmd;
  char **args;
  bool exec;
  int delay_ms;
  int timer_fd;
  bool run;
  bool debug;
};

struct output_data {
  struct wl_list link;
  uint32_t name;
  struct wl_output *output;
  struct wl_surface *surface;
  struct zwlr_layer_surface_v1 *layer_surface;
  struct wp_viewport *viewport;
  struct state *state;
};

static void destroy_output(struct output_data *output) {
  if (output->layer_surface) {
    zwlr_layer_surface_v1_destroy(output->layer_surface);
  }
  if (output->viewport) {
    wp_viewport_destroy(output->viewport);
  }
  if (output->surface) {
    wl_surface_destroy(output->surface);
  }
  wl_output_destroy(output->output);
  wl_list_remove(&output->link);
  free(output);
}

// --- Layer Surface Listeners ---

static void layer_surface_configure(void *data,
                                    struct zwlr_layer_surface_v1 *surface,
                                    uint32_t serial, uint32_t width,
                                    uint32_t height) {
  struct output_data *output = data;
  zwlr_layer_surface_v1_ack_configure(surface, serial);

  if (output->state->viewporter && !output->viewport) {
    output->viewport = wp_viewporter_get_viewport(output->state->viewporter,
                                                  output->surface);
  }
  if (output->viewport) {
    wp_viewport_set_destination(output->viewport, width, height);
  }

  wl_surface_attach(output->surface, output->state->buffer, 0, 0);
  wl_surface_commit(output->surface);
}

static void layer_surface_closed(void *data,
                                 struct zwlr_layer_surface_v1 *surface) {
  UNUSED(surface);
  struct output_data *output = data;
  destroy_output(output);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

// --- Pointer Listeners ---

static void execute(struct state *state) {
  if (state->exec) {
    execvp(state->cmd, state->args);
    fprintf(stderr, "execvp failed: %s\n", strerror(errno));
    exit(1);
  } else {
    pid_t pid = fork();
    if (pid == 0) {
      if (fork() == 0) {
        execvp(state->cmd, state->args);
        fprintf(stderr, "execvp failed: %s\n", strerror(errno));
        exit(1);
      }
      exit(0);
    }
    if (pid > 0) {
      waitpid(pid, NULL, 0);
    }
  }
}

static void pointer_enter(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t sx, wl_fixed_t sy) {
  UNUSED(pointer); UNUSED(serial); UNUSED(surface);
  struct state *state = data;
  UNUSED(sx); UNUSED(sy);
  if (state->delay_ms > 0) {
    struct itimerspec its = {0};
    its.it_value.tv_sec = state->delay_ms / 1000;
    its.it_value.tv_nsec = (state->delay_ms % 1000) * 1000000;
    timerfd_settime(state->timer_fd, 0, &its, NULL);
  } else {
    execute(state);
  }
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface) {
  UNUSED(pointer); UNUSED(serial); UNUSED(surface);
  struct state *state = data;
  struct itimerspec its = {0}; // Disarm
  timerfd_settime(state->timer_fd, 0, &its, NULL);
}

static void pointer_handle_motion(void *data, struct wl_pointer *pointer,
                                 uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
  UNUSED(data); UNUSED(pointer); UNUSED(time);
  UNUSED(sx); UNUSED(sy);
}
static void pointer_handle_button(void *data, struct wl_pointer *pointer,
                                 uint32_t serial, uint32_t time,
                                 uint32_t button, uint32_t state) {
  UNUSED(data); UNUSED(pointer); UNUSED(serial); UNUSED(time); UNUSED(button); UNUSED(state);
}
static void pointer_handle_axis(void *data, struct wl_pointer *pointer,
                               uint32_t time, uint32_t axis,
                               wl_fixed_t value) {
  UNUSED(data); UNUSED(pointer); UNUSED(time); UNUSED(axis); UNUSED(value);
}
static void pointer_handle_frame(void *data, struct wl_pointer *pointer) {
  UNUSED(data); UNUSED(pointer);
}
static void pointer_handle_axis_source(void *data, struct wl_pointer *pointer,
                                      uint32_t axis_source) {
  UNUSED(data); UNUSED(pointer); UNUSED(axis_source);
}
static void pointer_handle_axis_stop(void *data, struct wl_pointer *pointer,
                                    uint32_t time, uint32_t axis) {
  UNUSED(data); UNUSED(pointer); UNUSED(time); UNUSED(axis);
}
static void pointer_handle_axis_discrete(void *data, struct wl_pointer *pointer,
                                        uint32_t axis, int32_t discrete) {
  UNUSED(data); UNUSED(pointer); UNUSED(axis); UNUSED(discrete);
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_handle_motion,
    .button = pointer_handle_button,
    .axis = pointer_handle_axis,
    .frame = pointer_handle_frame,
    .axis_source = pointer_handle_axis_source,
    .axis_stop = pointer_handle_axis_stop,
    .axis_discrete = pointer_handle_axis_discrete,
};

static void seat_handle_name(void *data, struct wl_seat *seat,
                            const char *name) {
  UNUSED(data); UNUSED(seat); UNUSED(name);
}

static void seat_handle_capabilities(void *data, struct wl_seat *seat,
                                     uint32_t caps) {
  struct state *state = data;
  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !state->pointer) {
    state->pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(state->pointer, &pointer_listener, state);
  }
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

// --- Output Listeners ---

static void output_handle_geometry(
    void *data, struct wl_output *wl_output, int32_t x, int32_t y,
    int32_t physical_width, int32_t physical_height, int32_t subpixel,
    const char *make, const char *model, int32_t transform) {
  UNUSED(data); UNUSED(wl_output); UNUSED(x); UNUSED(y); UNUSED(physical_width);
  UNUSED(physical_height); UNUSED(subpixel); UNUSED(make); UNUSED(model); UNUSED(transform);
}

static void output_handle_mode(void *data, struct wl_output *wl_output,
                               uint32_t flags, int32_t width, int32_t height,
                               int32_t refresh) {
  UNUSED(data); UNUSED(wl_output); UNUSED(flags); UNUSED(width); UNUSED(height); UNUSED(refresh);
}

static void output_handle_done(void *data, struct wl_output *wl_output) {
  UNUSED(wl_output);
  struct output_data *output = data;
  struct state *state = output->state;

  if (!state->compositor || !state->layer_shell)
    return;
  if (output->surface)
    return; // Already setup

  output->surface = wl_compositor_create_surface(state->compositor);
  output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
      state->layer_shell, output->surface, output->output,
      ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wlhc");

  uint32_t width = 1, height = 1;
  uint32_t anchor = state->anchor;

  // Edge stretching logic:
  // If only TOP or BOTTOM is set, stretch horizontally.
  // If only LEFT or RIGHT is set, stretch vertically.
  bool horizontal = (anchor & (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                               ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM));
  bool vertical = (anchor & (ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                             ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT));

  if (horizontal && !vertical) {
    width = 0; // Stretch width
    anchor |=
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  } else if (vertical && !horizontal) {
    height = 0; // Stretch height
    anchor |=
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
  }

  zwlr_layer_surface_v1_set_size(output->layer_surface, width, height);
  zwlr_layer_surface_v1_set_anchor(output->layer_surface, anchor);
  zwlr_layer_surface_v1_add_listener(output->layer_surface,
                                     &layer_surface_listener, output);
  wl_surface_commit(output->surface);
}

static void output_handle_scale(void *data, struct wl_output *wl_output,
                                int32_t factor) {
  UNUSED(data); UNUSED(wl_output); UNUSED(factor);
}

static void output_handle_name(void *data, struct wl_output *wl_output,
                               const char *name) {
  UNUSED(data); UNUSED(wl_output); UNUSED(name);
}

static void output_handle_description(void *data, struct wl_output *wl_output,
                                      const char *description) {
  UNUSED(data); UNUSED(wl_output); UNUSED(description);
}

static const struct wl_output_listener output_listener = {
    .geometry = output_handle_geometry,
    .mode = output_handle_mode,
    .done = output_handle_done,
    .scale = output_handle_scale,
    .name = output_handle_name,
    .description = output_handle_description,
};

// --- Registry Listeners ---

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t name, const char *interface,
                                   uint32_t version) {
  struct state *state = data;
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    state->compositor =
        wl_registry_bind(registry, name, &wl_compositor_interface, version);
  } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
    state->layer_shell = wl_registry_bind(
        registry, name, &zwlr_layer_shell_v1_interface, version);
  } else if (strcmp(interface,
                    wp_single_pixel_buffer_manager_v1_interface.name) == 0) {
    state->buffer_manager = wl_registry_bind(
        registry, name, &wp_single_pixel_buffer_manager_v1_interface, version);
  } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
    state->viewporter =
        wl_registry_bind(registry, name, &wp_viewporter_interface, version);
  } else if (strcmp(interface, wl_seat_interface.name) == 0) {
    state->seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
    wl_seat_add_listener(state->seat, &seat_listener, state);
  } else if (strcmp(interface, wl_output_interface.name) == 0) {
    struct output_data *output = calloc(1, sizeof(struct output_data));
    output->name = name;
    output->output =
        wl_registry_bind(registry, name, &wl_output_interface, version);
    output->state = state;
    wl_list_insert(&state->outputs, &output->link);
    wl_output_add_listener(output->output, &output_listener, output);
  }
}

static void registry_handle_global_remove(void *data,
                                          struct wl_registry *registry,
                                          uint32_t name) {
  UNUSED(registry);
  struct state *state = data;
  struct output_data *output, *tmp;
  wl_list_for_each_safe(output, tmp, &state->outputs, link) {
    if (output->name == name) {
      destroy_output(output);
      return;
    }
  }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

// --- Main ---

int main(int argc, char **argv) {
  struct state state = {
      .run = true,
      .delay_ms = 0,
      .anchor = 0,
  };
  wl_list_init(&state.outputs);

  int c;
  while ((c = getopt(argc, argv, "hDetblr d:")) != -1) {
    switch (c) {
    case 'd':
      state.delay_ms = atoi(optarg);
      break;
    case 'D':
      state.debug = true;
      break;
    case 'e':
      state.exec = true;
      break;
    case 't':
      state.anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
      break;
    case 'b':
      state.anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
      break;
    case 'l':
      state.anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
      break;
    case 'r':
      state.anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
      break;
    case 'h':
    default:
      fprintf(stderr, "Usage: %s [-hDetblr] [-d <delay>] cmd args...\n",
              argv[0]);
      fprintf(stderr, "  -t, -b, -l, -r: anchor to top, bottom, left, right. "
                      "Combine for corners.\n");
      return c == 'h' ? 0 : 1;
    }
  }

  if (optind >= argc) {
    fprintf(stderr, "Must specify a command.\n");
    return 1;
  }

  if (!state.anchor) {
    fprintf(stderr, "Must provide at least one anchor (-t, -b, -l, or -r).\n");
    return 1;
  }

  state.cmd = argv[optind];
  state.args = &argv[optind];

  state.display = wl_display_connect(NULL);
  if (!state.display) {
    fprintf(stderr, "Failed to connect to wayland display\n");
    return 1;
  }

  state.registry = wl_display_get_registry(state.display);
  wl_registry_add_listener(state.registry, &registry_listener, &state);
  wl_display_roundtrip(state.display);

  if (!state.buffer_manager) {
    fprintf(stderr, "Compositor must support single_pixel_buffer_v1\n");
    return 1;
  }

  uint32_t r = state.debug ? 0xFFFFFFFF : 0;
  uint32_t a = state.debug ? 0xFFFFFFFF : 0x1; // Slightly non-zero alpha
  state.buffer = wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(
      state.buffer_manager, r, 0, 0, a);

  state.timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);

  struct pollfd fds[2] = {
      {.fd = wl_display_get_fd(state.display), .events = POLLIN},
      {.fd = state.timer_fd, .events = POLLIN},
  };

  while (state.run) {
    while (wl_display_prepare_read(state.display) != 0) {
      if (wl_display_dispatch_pending(state.display) < 0)
        goto out;
    }
    wl_display_flush(state.display);

    if (poll(fds, 2, -1) == -1) {
      wl_display_cancel_read(state.display);
      break;
    }

    if (fds[0].revents & POLLIN) {
      if (wl_display_read_events(state.display) < 0)
        goto out;
    } else {
      wl_display_cancel_read(state.display);
    }

    if (wl_display_dispatch_pending(state.display) < 0)
      goto out;

    if (fds[1].revents & POLLIN) {
      uint64_t expirations;
      read(state.timer_fd, &expirations, sizeof(expirations));
      execute(&state);
    }
  }

out:

  // Cleanup (simplified)
  close(state.timer_fd);
  wl_display_disconnect(state.display);

  return 0;
}
