util_src = files([
  'util_env.cpp',
  'util_string.cpp',
  # 'util_fps_limiter.cpp',
  # 'util_flush.cpp',
  # 'util_gdi.cpp',
  # 'util_luid.cpp',
  # 'util_matrix.cpp',
  # 'util_shared_res.cpp',
  # 'util_sleep.cpp',
  'util_bloom.cpp',
  'util_md5.cpp',

  'thread.cpp',

  'com/com_guid.cpp',
  'com/com_private_data.cpp',

  'config/config.cpp',

  'log/log.cpp',

  'sha1/sha1.c',
  'sha1/sha1_util.cpp',
])

if not dxmt_crossbuild
  util_src += files([
    'wsi_monitor_headless.cpp',
    'wsi_window_headless.cpp',
    'wsi_platform_darwin.cpp',
  ])
else
  util_src += files([
    'wsi_monitor_win32.cpp',
    'wsi_window_win32.cpp',
    'wsi_platform_win32.cpp',
  ])
endif

util_lib = static_library('util', util_src,
  include_directories : [ dxmt_include_path ],
)

util_dep = declare_dependency(
  link_with           : [ util_lib ],
  include_directories : [ include_directories('.') ],
)

if get_option('enable_tests')
  debug_logging_test = executable('debug-logging-test', 'tests/debug_logging.cpp',
    include_directories: [include_directories('.'), dxmt_native_include_path],
    native: dxmt_crossbuild,
  )
  foreach level : [0, 1, 2, 3, 4, 5]
    test('debug logging level ' + level.to_string(), debug_logging_test,
      env: ['DXMT_TEST_LOG_LEVEL=' + level.to_string()],
    )
  endforeach
endif
