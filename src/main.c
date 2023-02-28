#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <GL/gl.h>
#include <libdecor.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-egl.h>

static const size_t default_width = 600;
static const size_t default_height = 400;

struct client {
  struct wl_display* display;
  struct wl_compositor* compositor;
  struct wl_seat* seat;
  uint32_t capabilities;
  struct wl_pointer* pointer;
  struct wl_buffer* cursor_buffer;
  struct wl_shm* shm;
  struct wl_surface* cursor_surface;
  EGLDisplay egl_display;
  EGLContext egl_context;
};

struct window {
  struct client* client;
  struct wl_surface* surface;
  struct libdecor_frame* frame;
  struct wl_egl_window* egl_window;
  EGLSurface egl_surface;
  int content_width;
  int content_height;
  int floating_width;
  int floating_height;
  bool open;
  bool configured;
};

static void frame_configure(
  struct libdecor_frame* frame,
  struct libdecor_configuration* configuration,
  void* user_data
) {
  struct window* window = user_data;
  struct libdecor_state* state;
  int width, height;

  if (!libdecor_configuration_get_content_size(
        configuration, frame, &width, &height
      )) {
    width = window->floating_width;
    height = window->floating_height;
  }

  window->content_width = width;
  window->content_height = height;

  wl_egl_window_resize(
    window->egl_window, window->content_width, window->content_height, 0, 0
  );

  state = libdecor_state_new(width, height);
  libdecor_frame_commit(frame, state, configuration);
  libdecor_state_free(state);

  if (libdecor_frame_is_floating(window->frame)) {
    window->floating_width = width;
    window->floating_height = height;
  }

  window->configured = true;
}

static void frame_close(struct libdecor_frame* frame, void* user_data) {
  struct window* window = user_data;

  window->open = false;
}

static void frame_commit(struct libdecor_frame* frame, void* user_data) {
  struct window* window = user_data;

  eglSwapBuffers(window->client->display, window->egl_surface);
}

static struct libdecor_frame_interface frame_interface = {
  .configure = frame_configure,
  .close = frame_close,
  .commit = frame_commit,
};

static void libdecor_error(
  struct libdecor* context,
  enum libdecor_error error,
  const char* message
) {
  fprintf(stderr, "libdecor error (%d): %s\n", error, message);
  exit(EXIT_FAILURE);
}

static struct libdecor_interface libdecor_interface = {
  .error = libdecor_error,
};

static void registry_global(
  void* data,
  struct wl_registry* wl_registry,
  uint32_t name,
  const char* interface,
  uint32_t version
) {
  struct client* client = data;

  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    client->compositor =
      wl_registry_bind(wl_registry, name, &wl_compositor_interface, 4);
  } else if (!strcmp(interface, wl_seat_interface.name) && !client->seat) {
    client->seat = wl_registry_bind(wl_registry, name, &wl_seat_interface, 1);
  } else if (!strcmp(interface, wl_shm_interface.name)) {
    client->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
  }
}

static void registry_global_remove(
  void* data,
  struct wl_registry* wl_registry,
  uint32_t name
) {}

static const struct wl_registry_listener registry_listener = {
  .global = registry_global,
  .global_remove = registry_global_remove,
};

static void pointer_enter_handler(
  void* data,
  struct wl_pointer* pointer,
  uint32_t serial,
  struct wl_surface* surface,
  wl_fixed_t sxW,
  wl_fixed_t syW
) {
  struct window* window = data;

  if (surface != window->surface) {
    return;
  }

  printf("pointer_enter\n");
  wl_pointer_set_cursor(pointer, serial, window->client->cursor_surface, 4, 4);
}

static void pointer_leave_handler(
  void* data,
  struct wl_pointer* pointer,
  uint32_t serial,
  struct wl_surface* surface
) {
  struct window* window = data;

  if (surface != window->surface) {
    return;
  }

  printf("pointer_leave\n");
}

static void pointer_motion_handler(
  void* data,
  struct wl_pointer* pointer,
  uint32_t serial,
  wl_fixed_t sxW,
  wl_fixed_t syW
) {
  // double x = wl_fixed_to_double(sxW);
  // double y = wl_fixed_to_double(syW);
  // printf("(%0.2f, %0.2f)\n", x, y);
}

static void pointer_button_handler(
  void* data,
  struct wl_pointer* pointer,
  uint32_t serial,
  uint32_t time,
  uint32_t button,
  uint32_t stateW
) {}

static void pointer_axis_handler(
  void* data,
  struct wl_pointer* pointer,
  uint32_t serial,
  uint32_t axis,
  wl_fixed_t value
) {}

static const struct wl_pointer_listener pointer_listener = {
  .enter = pointer_enter_handler,
  .leave = pointer_leave_handler,
  .motion = pointer_motion_handler,
  .button = pointer_button_handler,
  .axis = pointer_axis_handler,
};

static void seat_capabilities_handler(
  void* data,
  struct wl_seat* seat,
  uint32_t capabilities
) {
  struct window* window = data;
  struct client* client = window->client;

  client->capabilities = capabilities;
  bool hasPointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
  if (!hasPointer && client->pointer) {
    printf("lost pointer capability\n");
    wl_pointer_destroy(client->pointer);
    client->pointer = NULL;
  } else if (hasPointer && !client->pointer) {
    printf("acquired pointer capability\n");
    client->pointer = wl_seat_get_pointer(client->seat);
    wl_pointer_add_listener(client->pointer, &pointer_listener, window);
  }
}

static const struct wl_seat_listener seat_listener = {
  .capabilities = seat_capabilities_handler,
};

static const uint32_t cursor_bitmap[] = {
  // clang-format off
  0x000000, 0x000000, 0x000000, 0x000000,
  0x000000, 0xFFFFFF, 0xFFFFFF, 0x000000,
  0x000000, 0xFFFFFF, 0xFFFFFF, 0x000000,
  0x000000, 0x000000, 0x000000, 0x000000,
  // clang-format on
};

static bool create_cursor_buffer(struct client* client) {
  int fd = memfd_create("cursor", 0);
  if (fd < 0) {
    perror("failed to create cursor shared memory");
    return false;
  }

  if (ftruncate(fd, sizeof cursor_bitmap) < 0) {
    perror("failed to ftruncate cursor shared memory");
    goto fail;
  }

  void* shm_data =
    mmap(NULL, sizeof cursor_bitmap, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (shm_data == MAP_FAILED) {
    perror("failed to map memory for cursor");
    goto fail;
  }

  struct wl_shm_pool* pool =
    wl_shm_create_pool(client->shm, fd, sizeof cursor_bitmap);
  client->cursor_buffer =
    wl_shm_pool_create_buffer(pool, 0, 4, 4, 16, WL_SHM_FORMAT_XRGB8888);
  wl_shm_pool_destroy(pool);

  memcpy(shm_data, cursor_bitmap, sizeof cursor_bitmap);
  munmap(shm_data, sizeof cursor_bitmap);

  close(fd);
  return true;

fail:
  close(fd);
  return false;
}

static bool setup(struct window* window) {
  static const EGLint config_attribs[] = {
    EGL_SURFACE_TYPE,
    EGL_WINDOW_BIT,
    EGL_RED_SIZE,
    8,
    EGL_GREEN_SIZE,
    8,
    EGL_BLUE_SIZE,
    8,
    EGL_RENDERABLE_TYPE,
    EGL_OPENGL_BIT,
    EGL_NONE};

  EGLint major, minor;
  EGLint n;
  EGLConfig config;

  window->client->egl_display =
    eglGetDisplay((EGLNativeDisplayType)window->client->display);

  if (eglInitialize(window->client->egl_display, &major, &minor) == EGL_FALSE) {
    fprintf(stderr, "cannot initialise EGL!\n");
    return false;
  }

  if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE) {
    fprintf(stderr, "cannot bind EGL API!\n");
    return false;
  }

  if (eglChooseConfig(window->client->egl_display, config_attribs, &config, 1, &n) == EGL_FALSE) {
    fprintf(stderr, "no matching EGL configurations!\n");
    return false;
  }

  window->client->egl_context =
    eglCreateContext(window->client->egl_display, config, EGL_NO_CONTEXT, NULL);

  if (window->client->egl_context == EGL_NO_CONTEXT) {
    fprintf(stderr, "no EGL context!\n");
    return false;
  }

  window->surface = wl_compositor_create_surface(window->client->compositor);

  window->egl_window =
    wl_egl_window_create(window->surface, default_width, default_height);

  window->egl_surface = eglCreateWindowSurface(
    window->client->egl_display,
    config,
    (EGLNativeWindowType)window->egl_window,
    NULL
  );

  eglMakeCurrent(
    window->client->egl_display,
    window->egl_surface,
    window->egl_surface,
    window->client->egl_context
  );

  if (!create_cursor_buffer(window->client)) {
    return false;
  }

  window->client->cursor_surface =
    wl_compositor_create_surface(window->client->compositor);
  wl_surface_attach(
    window->client->cursor_surface, window->client->cursor_buffer, 0, 0
  );
  wl_surface_commit(window->client->cursor_surface);

  return true;
}

static void cleanup(struct window* window) {
  if (window->client->egl_display) {
    eglMakeCurrent(
      window->client->egl_display,
      EGL_NO_SURFACE,
      EGL_NO_SURFACE,
      EGL_NO_CONTEXT
    );
  }

  if (window->client->cursor_surface) {
    wl_surface_destroy(window->client->cursor_surface);
  }

  if (window->egl_surface) {
    eglDestroySurface(window->client->egl_display, window->egl_surface);
  }

  if (window->egl_window) {
    wl_egl_window_destroy(window->egl_window);
  }

  if (window->surface) {
    wl_surface_destroy(window->surface);
  }

  if (window->client->egl_context) {
    eglDestroyContext(window->client->egl_display, window->client->egl_context);
  }

  if (window->client->egl_display) {
    eglTerminate(window->client->egl_display);
  }
}

static void draw(struct window* window) {
  float rgb[3] = {0x20 / (float)0xFF, 0x20 / (float)0xFF, 0x20 / (float)0xFF};

  glClearColor(rgb[0], rgb[1], rgb[2], 1);
  glClear(GL_COLOR_BUFFER_BIT);

  eglSwapBuffers(window->client->egl_display, window->egl_surface);
}

int main(int argc, char* argv[]) {
  struct wl_registry* wl_registry;
  struct libdecor* context = NULL;
  struct window* window;
  struct client* client;
  int ret = EXIT_SUCCESS;

  client = calloc(1, sizeof(struct client));

  client->display = wl_display_connect(NULL);
  if (!client->display) {
    fprintf(stderr, "no wayland connection\n");
    free(client);
    return EXIT_FAILURE;
  }

  wl_registry = wl_display_get_registry(client->display);
  wl_registry_add_listener(wl_registry, &registry_listener, client);
  wl_display_roundtrip(client->display);

  window = calloc(1, sizeof(struct window));
  window->client = client;
  window->open = true;
  window->configured = false;
  window->floating_width = default_width;
  window->floating_height = default_height;

  if (!setup(window)) {
    goto out;
  }

  wl_seat_add_listener(client->seat, &seat_listener, window);

  context = libdecor_new(client->display, &libdecor_interface);
  window->frame =
    libdecor_decorate(context, window->surface, &frame_interface, window);
  libdecor_frame_set_app_id(window->frame, "bugrepro");
  libdecor_frame_set_title(window->frame, "bugrepro");
  libdecor_frame_map(window->frame);

  wl_display_roundtrip(client->display);
  wl_display_roundtrip(client->display);

  while (!window->configured) {
    if (libdecor_dispatch(context, 0) < 0) {
      ret = EXIT_FAILURE;
      goto out;
    }
  }

  while (window->open) {
    if (libdecor_dispatch(context, 0) < 0) {
      ret = EXIT_FAILURE;
      goto out;
    }
    draw(window);
  }

out:
  if (context) {
    libdecor_unref(context);
  }
  cleanup(window);
  free(window);
  free(client);

  return ret;
}
