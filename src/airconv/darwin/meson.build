native_llvm_path = get_option('native_llvm_path')

if not native_llvm_path.startswith('/')
  # Meson only allows relative include paths in case the toolchains are located inside
  # of the project source root, so handle that here.
  llvm_include_path_darwin = include_directories(join_paths('../../..', native_llvm_path, 'include'))
  native_llvm_path = join_paths(meson.project_source_root(), native_llvm_path)
else
  llvm_include_path_darwin = include_directories(join_paths(native_llvm_path, 'include'))
endif

llvm_ld_flags_darwin = [
  '-L'+join_paths(native_llvm_path, 'lib'),
  '-lm', '-lz', '-lcurses', '-lxml2'
]

if native_llvm_path.startswith('/usr/local/opt') or native_llvm_path.startswith('/opt/homebrew/opt')
  homebrew_prefix = native_llvm_path.startswith('/opt/homebrew/opt') ? '/opt/homebrew' : '/usr/local'
  llvm_ld_flags_darwin = [
    llvm_ld_flags_darwin,
    join_paths(homebrew_prefix, 'opt/zstd/lib/libzstd.a'), join_paths(native_llvm_path, 'lib/libunwind.a')
  ]
endif

airconv_lib_darwin = static_library('airconv', airconv_src,
  include_directories : [ dxmt_include_path, llvm_include_path_darwin ],
  cpp_args            : [ airconv_args ],
  dependencies        : [ dxbc_parser_native_dep ],
  link_args           : llvm_ld_flags_darwin,
  native              : dxmt_crossbuild
)

airconv_dep_darwin = declare_dependency(
  link_with           : [ airconv_lib_darwin ],
  include_directories : [ dxmt_include_path, include_directories('..') ],
  link_args           : [ llvm_ld_flags_darwin, llvm_deps ] # meh
)

executable('airconv', airconv_src + airconv_cli_src,
  include_directories : [ dxmt_include_path, llvm_include_path_darwin ],
  cpp_args            : [ airconv_args ],
  dependencies        : [ dxbc_parser_native_dep ],
  link_args           : [ llvm_ld_flags_darwin, llvm_deps ],
  native              : dxmt_crossbuild
)

if get_option('enable_tests')
  msc_typed_buffer_experiment = executable('msc-typed-buffer-experiment',
    files('../../../tests/airconv/msc_typed_buffer_experiment.cpp', '../metallib_writer.cpp'),
    include_directories : [ include_directories('..'), llvm_include_path_darwin ],
    cpp_args            : [ airconv_args ],
    link_args           : [ llvm_ld_flags_darwin, llvm_deps ],
    native              : dxmt_crossbuild,
    build_by_default    : false
  )
  msc_typed_buffer_python = find_program('python3', required : false)
  if msc_typed_buffer_python.found()
    test('airconv msc typed buffer experiment', msc_typed_buffer_python,
      args : [files('../../../tests/airconv/test_msc_typed_buffer_experiment.py'),
              msc_typed_buffer_experiment, files('../../../tests/airconv/msc_typed_buffer_experiment.ir')])
  endif
  lower_unsupported_double_test = executable('lower-unsupported-double-test',
    files('../tests/lower_unsupported_double.cpp', '../transforms/lower_unsupported_double.cpp'),
    include_directories : [ include_directories('..'), llvm_include_path_darwin ],
    cpp_args            : [ airconv_args ],
    link_args           : [ llvm_ld_flags_darwin, llvm_deps ],
    native              : dxmt_crossbuild
  )

  test('airconv lower unsupported double', lower_unsupported_double_test)

  msc_sample_level_lod_test = executable('msc-sample-level-lod-test',
    files('../../../tests/airconv/msc_sample_level_lod_test.cpp', '../transforms/msc_sample_level_lod.cpp',
          '../transforms/lower_unsupported_double.cpp', '../metallib_patch.cpp', '../metallib_writer.cpp'),
    include_directories : [ include_directories('..'), llvm_include_path_darwin ],
    cpp_args            : [ airconv_args ],
    link_args           : [ llvm_ld_flags_darwin, llvm_deps ],
    native              : dxmt_crossbuild
  )

  test('airconv msc sample-level lod', msc_sample_level_lod_test)
endif

if get_option('enable_tests')
  msc_icb_constant_experiment = executable('msc-icb-constant-experiment',
    files('../../../tests/airconv/msc_icb_constant_experiment.cpp', '../metallib_writer.cpp'),
    include_directories: [ include_directories('..'), llvm_include_path_darwin ],
    cpp_args: [ airconv_args ],
    link_args: [ llvm_ld_flags_darwin, llvm_deps ],
    native: dxmt_crossbuild
  )
  test('airconv msc icb constant experiment', msc_icb_constant_experiment, args: ['--self-test'])
  icb_experiment_lli = find_program('lli', required: false, native: true)
  icb_experiment_opt = find_program('opt', required: false, native: true)
  icb_experiment_python = find_program('python3', required: false, native: true)
  if icb_experiment_python.found()
    test('airconv msc icb isolated cache', icb_experiment_python,
      args: [files('../../../tests/airconv/test_prepare_icb_experiment_cache.py'),
             files('../../../tests/airconv/prepare_icb_experiment_cache.py')])
    test('airconv darwin loaded image identity', icb_experiment_python,
      args: [files('../../../tests/airconv/test_darwin_loaded_image_identity.py'),
             files('../../../tests/airconv/darwin_loaded_image_probe.c')])
    test('airconv darwin pinned image identity', icb_experiment_python,
      args: [files('../../../tests/airconv/test_darwin_pinned_image.py'),
             files('../../../tests/airconv/darwin_pinned_image_probe.c')])
    test('airconv msc image identity component', icb_experiment_python,
      args: [files('../../../tests/airconv/test_msc_image_identity_component.py'),
             files('../../../tests/airconv/darwin_image_identity_component_probe.c'),
             files('../../winemetal/unix/msc_image_identity.c')])
    test('airconv msc image identity dependencies', icb_experiment_python,
      args: [files('../../../tests/airconv/test_msc_identity_dependencies.py'),
             files('../../../tests/airconv/darwin_loaded_image_probe.c'),
             files('../../../tests/airconv/darwin_image_identity_component_probe.c'),
             files('../../winemetal/unix/msc_image_identity.c')])
    test('airconv msc system dependency identity', icb_experiment_python,
      args: [files('../../../tests/airconv/test_msc_system_dependencies.py'),
             files('../../../tests/airconv/darwin_image_identity_component_probe.c'),
             files('../../winemetal/unix/msc_image_identity.c'),
             files('../../../tests/airconv/darwin_shared_cache_probe.c')])
    test('airconv msc shared cache provider override', icb_experiment_python,
      args: [files('../../../tests/airconv/test_msc_cache_provider.py'),
             files('../../../tests/airconv/darwin_cache_provider_probe.c')])
    test('airconv msc image epoch', icb_experiment_python,
      args: [files('../../../tests/airconv/test_msc_image_epoch.py'),
             files('../../../tests/airconv/darwin_image_epoch_probe.c'),
             files('../../winemetal/unix/msc_image_epoch.c')])
    test('airconv msc conversion image epoch', icb_experiment_python,
      args: [files('../../../tests/airconv/test_msc_conversion_epoch.py'),
             files('../../../tests/airconv/darwin_msc_conversion_epoch.c'),
             files('../../winemetal/unix/msc_image_epoch.c'),
             meson.project_source_root() / 'include'])
  endif
  if icb_experiment_opt.found() and icb_experiment_python.found()
    test('airconv msc icb index layout', icb_experiment_python,
      args: [files('../../../tests/airconv/test_msc_icb_index_layout.py'),
             msc_icb_constant_experiment, icb_experiment_opt.full_path()])
  endif
  if icb_experiment_lli.found() and icb_experiment_python.found()
    test('airconv msc icb constant differential', icb_experiment_python,
      args: [files('../../../tests/airconv/test_msc_icb_constant_differential.py'),
             msc_icb_constant_experiment, icb_experiment_lli.full_path(),
             files('../../../tests/airconv/msc_icb_constant_differential.ir')])
  endif
endif
