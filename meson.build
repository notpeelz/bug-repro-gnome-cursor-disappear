project('bugrepro', 'c',
  version: '0.1',
  default_options: ['warning_level=3', 'c_std=gnu2x']
)

add_global_arguments('-Wno-unused-parameter', language: 'c')

wl_dep = dependency('wayland-client')
wl_cursor_dep = dependency('wayland-cursor')
egl_dep = dependency('egl')
gl_dep = dependency('opengl')
wayland_egl_dep = dependency('wayland-egl')
libdecor_dep = dependency('libdecor-0')

executable('bugrepro',
  'src/main.c',
  dependencies: [
    wl_dep,
    wl_cursor_dep,
    wayland_egl_dep,
    egl_dep,
    gl_dep,
    libdecor_dep,
  ]
)
