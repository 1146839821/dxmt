dx12_sm6_probe = executable('dx12_compute_sm6', 'dx12_compute_sm6.cpp',
  include_directories: [
    include_directories('../../src/airconv'),
    include_directories('../../src/dxgi'),
    include_directories('../../src/dxmt'),
    include_directories('../../src/winemetal'),
    include_directories('../../src/util'),
  ],
  dependencies: [ d3d12_dep, winemetal_dep ],
)

dx12_shader_container = executable('dx12_shader_container', 'dx12_shader_container.cpp', '../../src/util/util_md5.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_shader_validation = executable('dx12_shader_validation', 'dx12_shader_validation.cpp',
  include_directories: [
    include_directories('../../src/airconv'),
    include_directories('../../src/winemetal'),
  ],
  dependencies: [ d3d12_dep ],
)

dx12_airconv_compute_residency = executable('dx12_airconv_compute_residency', 'dx12_airconv_compute_residency.cpp',
  include_directories: [
    include_directories('../../src/airconv'),
    include_directories('../../src/dxgi'),
    include_directories('../../src/dxmt'),
    include_directories('../../src/winemetal'),
    include_directories('../../src/util'),
  ],
  dependencies: [ d3d12_dep, winemetal_dep ],
)

dx12_airconv_firstbit = executable('dx12_airconv_firstbit', 'dx12_airconv_firstbit.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_shader_runtime = executable('dx12_shader_runtime', 'dx12_shader_runtime.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_texture_sampler = executable('dx12_texture_sampler', 'dx12_texture_sampler.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_texture_dimensions = executable('dx12_texture_dimensions', 'dx12_texture_dimensions.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_graphics_sm6 = executable('dx12_graphics_sm6', 'dx12_graphics_sm6.cpp',
  include_directories: [
    include_directories('../../src/airconv'),
    include_directories('../../src/dxgi'),
    include_directories('../../src/dxmt'),
    include_directories('../../src/winemetal'),
    include_directories('../../src/util'),
  ],
  dependencies: [ d3d12_dep ],
)

dx12_graphics_sm5 = executable('dx12_graphics_sm5', 'dx12_graphics_sm5.cpp',
  include_directories: [
    include_directories('../../src/airconv'),
    include_directories('../../src/dxgi'),
    include_directories('../../src/dxmt'),
    include_directories('../../src/winemetal'),
    include_directories('../../src/util'),
  ],
  dependencies: [ d3d12_dep ],
)

dx12_stream_output = executable('dx12_stream_output', 'dx12_stream_output.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_feature_support = executable('dx12_feature_support', 'dx12_feature_support.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_msc_probe = executable('dx12_msc_probe', 'dx12_msc_probe.cpp',
  include_directories: [ include_directories('../../src/winemetal') ],
  dependencies: [ d3d12_dep ],
)

dx12_msc_capability_gate = executable('dx12_msc_capability_gate', 'dx12_msc_capability_gate.cpp',
  include_directories: [ include_directories('../../src/winemetal') ],
  dependencies: [ d3d12_dep ],
)

dx12_msc_raytracing = executable('dx12_msc_raytracing', 'dx12_msc_raytracing.cpp',
  include_directories: [ include_directories('../../src/winemetal') ],
  dependencies: [ d3d12_dep, winemetal_dep ],
)

dx12_metal_acceleration = executable('dx12_metal_acceleration', 'dx12_metal_acceleration.cpp',
  include_directories: [ include_directories('../../src/winemetal') ],
  dependencies: [ winemetal_dep ],
)

dx12_raytracing_prebuild = executable('dx12_raytracing_prebuild', 'dx12_raytracing_prebuild.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_raytracing_build = executable('dx12_raytracing_build', 'dx12_raytracing_build.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_raytracing_state_object = executable('dx12_raytracing_state_object', 'dx12_raytracing_state_object.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_raytracing_dispatch = executable('dx12_raytracing_dispatch', 'dx12_raytracing_dispatch.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_raytracing_trace = executable('dx12_raytracing_trace', 'dx12_raytracing_trace.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_msc_function_constants = executable('dx12_msc_function_constants', 'dx12_msc_function_constants.cpp',
  include_directories: [ include_directories('../../src/winemetal') ],
  dependencies: [ d3d12_dep ],
)

dx12_msc_debug_validation = executable('dx12_msc_debug_validation', 'dx12_msc_debug_validation.cpp',
  include_directories: [ include_directories('../../src/winemetal') ],
  dependencies: [ d3d12_dep ],
)

dx12_msc_framebuffer_fetch = executable('dx12_msc_framebuffer_fetch', 'dx12_msc_framebuffer_fetch.cpp',
  include_directories: [ include_directories('../../src/winemetal') ],
  dependencies: [ d3d12_dep ],
)

dx12_interface_support = executable('dx12_interface_support', 'dx12_interface_support.cpp',
  dependencies: [ d3d12_dep, dxgi_dep ],
)

dx12_command_list_type = executable('dx12_command_list_type', 'dx12_command_list_type.cpp',
  include_directories: [
    include_directories('../../src/airconv'),
    include_directories('../../src/dxgi'),
    include_directories('../../src/dxmt'),
    include_directories('../../src/winemetal'),
    include_directories('../../src/util'),
  ],
  dependencies: [ d3d12_dep, winemetal_dep ],
)

dx12_resource_tests = executable('dx12_resource_tests', 'dx12_resource_tests.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_ngx_smoke = executable('dx12_ngx_smoke', 'dx12_ngx_smoke.cpp',
  dependencies: [ d3d12_dep, dxgi_dep ],
)

dx12_hdr = executable('dx12_hdr', 'dx12_hdr.cpp',
  dependencies: [ d3d12_dep, dxgi_dep ],
)

dx12_present = executable('dx12_present', 'dx12_present.cpp',
  include_directories: [ include_directories('../../src/dxgi') ],
  dependencies: [ d3d12_dep, dxgi_dep ],
)

dx12_pipeline_persistence = executable('dx12_pipeline_persistence', 'dx12_pipeline_persistence.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_debug_logging = executable('dx12_debug_logging', 'dx12_debug_logging.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_mesh_sm6 = executable('dx12_mesh_sm6', 'dx12_mesh_sm6.cpp',
  dependencies: [ d3d12_dep ],
)

dxc_command = []
dxc = find_program('dxc', required: false)
if dxc.found()
  dxc_command = [dxc]
else
  dxc_arch = cpu_family == 'x86' ? 'x86' : 'x64'
  dxc_executable = join_paths(meson.project_source_root(), 'tools', 'dxc', 'bin', dxc_arch, 'dxc.exe')
  wine = find_program(join_paths(meson.project_source_root(), 'toolchains', 'wine', 'bin', 'wine'), required: false)
  if not wine.found()
    wine = find_program('wine', required: false)
  endif
  if wine.found() and fs.is_file(dxc_executable)
    dxc_command = [wine, dxc_executable]
  endif
endif

if dxc_command.length() > 0
  custom_target('dx12_compute_sm6_fixture',
    input: 'compute_sm6.hlsl',
    output: 'compute_sm6.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_function_constants_sm6_fixture',
    input: 'function_constants_sm6.hlsl',
    output: 'function_constants_sm6.ps.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_debug_validation_sm6_fixture',
    input: 'debug_validation_sm6.hlsl',
    output: 'debug_validation_sm6.ps.cso',
    command: dxc_command + [ '-Zi', '-Qembed_debug', '-E', 'main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_framebuffer_fetch_sm6_fixture',
    input: 'framebuffer_fetch_sm6.hlsl',
    output: 'framebuffer_fetch_sm6.ps.cso',
    command: dxc_command + [ '-enable-16bit-types', '-E', 'main', '-T', 'ps_6_2', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_shader_embedded_compute_fixture',
    input: 'shader_embedded_compute.hlsl',
    output: 'shader_embedded.compute.cs.cso',
    command: dxc_command + [ '-E', 'cs_main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_shader_embedded_graphics_vs_fixture',
    input: 'shader_embedded_graphics.hlsl',
    output: 'shader_embedded.graphics.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_shader_embedded_graphics_ps_fixture',
    input: 'shader_embedded_graphics.hlsl',
    output: 'shader_embedded.graphics.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_shader_embedded_graphics_mismatch_fixture',
    input: 'shader_embedded_graphics_mismatch.hlsl',
    output: 'shader_embedded.graphics.mismatch.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_shader_backend_geometry_fixture',
    input: 'shader_backend_stages.hlsl',
    output: 'shader_backend.geometry.gs.cso',
    command: dxc_command + [ '-E', 'gs_main', '-T', 'gs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  foreach stage : ['vs', 'ps', 'gs', 'hs', 'ds', 'ms']
    profile = stage == 'ms' ? 'ms_6_5' : stage + '_6_0'
    custom_target('dx12_stage_binding_' + stage,
      input: 'shader_backend_stages.hlsl',
      output: 'stage_binding.' + stage + '.cso',
      command: dxc_command + [ '-E', stage + '_main', '-T', profile,
        '-D', 'STAGE_BINDING_DIFFERENTIAL=1', '@INPUT@', '-Fo', '@OUTPUT@' ],
      build_by_default: false,
    )
  endforeach
  foreach topology : ['point', 'line', 'ccw']
    foreach stage : ['hs', 'ds']
      custom_target('dx12_stage_binding_tess_' + topology + '_' + stage,
        input: 'shader_backend_stages.hlsl',
        output: 'stage_binding_' + topology + '.' + stage + '.cso',
        command: dxc_command + [ '-E', stage + '_main', '-T', stage + '_6_0',
          '-D', 'STAGE_BINDING_DIFFERENTIAL=1', '-D', 'STAGE_TESS_' + topology.to_upper() + '=1',
          '@INPUT@', '-Fo', '@OUTPUT@' ],
        build_by_default: false,
      )
    endforeach
  endforeach
  foreach topology : ['point', 'line']
    foreach stage : ['hs', 'ds']
      custom_target('dx12_stage_binding_tess_' + topology + '_image_' + stage,
        input: 'shader_backend_stages.hlsl',
        output: 'stage_binding_' + topology + '_image.' + stage + '.cso',
        command: dxc_command + [ '-E', stage + '_main', '-T', stage + '_6_0',
          '-D', 'STAGE_BINDING_DIFFERENTIAL=1', '-D', 'STAGE_TESS_' + topology.to_upper() + '=1',
          '-D', 'STAGE_TESS_IMAGE=1', '@INPUT@', '-Fo', '@OUTPUT@' ],
        build_by_default: false,
      )
    endforeach
  endforeach
  custom_target('dx12_stage_binding_tess_count_ps',
    input: 'shader_backend_stages.hlsl',
    output: 'stage_binding_count.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0',
      '-D', 'STAGE_BINDING_DIFFERENTIAL=1', '-D', 'STAGE_TESS_COUNT=1',
      '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  foreach stage : ['hs', 'ds']
    custom_target('dx12_stage_binding_tess_point_coord_' + stage,
      input: 'shader_backend_stages.hlsl',
      output: 'stage_binding_point_coord.' + stage + '.cso',
      command: dxc_command + [ '-E', stage + '_main', '-T', stage + '_6_0',
        '-D', 'STAGE_BINDING_DIFFERENTIAL=1', '-D', 'STAGE_TESS_POINT=1',
        '-D', 'STAGE_TESS_COORD=1', '@INPUT@', '-Fo', '@OUTPUT@' ],
      build_by_default: false,
    )
  endforeach
  foreach stage : ['hs', 'ds']
    custom_target('dx12_stage_binding_tess_point_coord_factor2_' + stage,
      input: 'shader_backend_stages.hlsl',
      output: 'stage_binding_point_coord_factor2.' + stage + '.cso',
      command: dxc_command + [ '-E', stage + '_main', '-T', stage + '_6_0',
        '-D', 'STAGE_BINDING_DIFFERENTIAL=1', '-D', 'STAGE_TESS_POINT=1',
        '-D', 'STAGE_TESS_COORD=1', '-D', 'STAGE_TESS_FACTOR2=1',
        '@INPUT@', '-Fo', '@OUTPUT@' ],
      build_by_default: false,
    )
  endforeach
  foreach stage : ['hs', 'ds']
    custom_target('dx12_stage_binding_tess_point_grid_factor2_' + stage,
      input: 'shader_backend_stages.hlsl',
      output: 'stage_binding_point_grid_factor2.' + stage + '.cso',
      command: dxc_command + [ '-E', stage + '_main', '-T', stage + '_6_0',
        '-D', 'STAGE_BINDING_DIFFERENTIAL=1', '-D', 'STAGE_TESS_POINT=1',
        '-D', 'STAGE_TESS_COORD=1', '-D', 'STAGE_TESS_FACTOR2=1',
        '-D', 'STAGE_TESS_GRID=1', '@INPUT@', '-Fo', '@OUTPUT@' ],
      build_by_default: false,
    )
  endforeach
  custom_target('dx12_stage_binding_point_grid_control_vs',
    input: 'shader_backend_stages.hlsl',
    output: 'stage_binding_point_grid_control.vs.cso',
    command: dxc_command + [ '-E', 'vs_grid_main', '-T', 'vs_6_0',
      '-D', 'STAGE_BINDING_DIFFERENTIAL=1', '-D', 'STAGE_TESS_GRID_CONTROL=1',
      '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  foreach stage : ['ms', 'as']
    custom_target('dx12_stage_binding_as_' + stage,
      input: 'shader_backend_stages.hlsl',
      output: 'stage_binding_as.' + stage + '.cso',
      command: dxc_command + [ '-E', stage + '_main', '-T', stage + '_6_5',
        '-D', 'STAGE_BINDING_DIFFERENTIAL=1', '-D', 'STAGE_BINDING_AS_MESH=1',
        '@INPUT@', '-Fo', '@OUTPUT@' ],
      build_by_default: false,
    )
  endforeach
  custom_target('dx12_shader_backend_hull_fixture',
    input: 'shader_backend_stages.hlsl',
    output: 'shader_backend.tessellation.hs.cso',
    command: dxc_command + [ '-E', 'hs_main', '-T', 'hs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_shader_backend_domain_fixture',
    input: 'shader_backend_stages.hlsl',
    output: 'shader_backend.tessellation.ds.cso',
    command: dxc_command + [ '-E', 'ds_main', '-T', 'ds_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_shader_backend_library_fixture',
    input: 'shader_backend_library.hlsl',
    output: 'shader_backend.library.lib.cso',
    command: dxc_command + [ '-T', 'lib_6_3', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_root_uav_fixture',
    input: 'compute_sm6_root_uav.hlsl',
    output: 'compute_sm6_root_uav.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_global_coherent_fixture',
    input: 'compute_sm6_global_coherent.hlsl',
    output: 'compute_sm6_global_coherent.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_root_value_fixture',
    input: 'compute_sm6_root_value.hlsl',
    output: 'compute_sm6_root_value.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_root_srv_fixture',
    input: 'compute_sm6_root_srv.hlsl',
    output: 'compute_sm6_root_srv.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_descriptor_resources_fixture',
    input: 'compute_sm6_descriptor_resources.hlsl',
    output: 'compute_sm6_descriptor_resources.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_descriptor_typed_resources_fixture',
    input: 'compute_sm6_descriptor_resources.hlsl',
    output: 'compute_sm6_descriptor_typed_resources.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '-D', 'TYPED_BUFFER=1', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_descriptor_resources_space_fixture',
    input: 'compute_sm6_descriptor_resources_space.hlsl',
    output: 'compute_sm6_descriptor_resources_space.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_descriptor_bgra_fixture',
    input: 'compute_sm6_descriptor_resources.hlsl',
    output: 'compute_sm6_descriptor_bgra.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '-D', 'BGRA_BUFFER=1', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_unbounded_resources_fixture',
    input: 'compute_sm6_unbounded_resources.hlsl',
    output: 'compute_sm6_unbounded_resources.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_append_buffer_fixture',
    input: 'compute_sm6_append_buffer.hlsl',
    output: 'compute_sm6_append_buffer.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  foreach operation : ['append', 'consume']
    custom_target('dx12_' + operation + '_counter_fixture',
      input: 'append_consume.hlsl',
      output: operation + '_counter.cs.cso',
      command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '-D',
        operation == 'consume' ? 'COUNTER_CONSUME=1' : 'COUNTER_APPEND=1',
        '@INPUT@', '-Fo', '@OUTPUT@' ],
      build_by_default: false,
    )
  endforeach
  custom_target('dx12_compute_sm6_sampler_feedback_fixture',
    input: 'compute_sm6_sampler_feedback.hlsl',
    output: 'compute_sm6_sampler_feedback.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_5', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_nan_inf_fixture',
    input: 'compute_sm6_nan_inf.hlsl',
    output: 'compute_sm6_nan_inf.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_direct_indexed_fixture',
    input: 'compute_sm6_direct_indexed.hlsl',
    output: 'compute_sm6_direct_indexed.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_direct_indexed_resources_fixture',
    input: 'compute_sm6_direct_indexed_resources.hlsl',
    output: 'compute_sm6_direct_indexed_resources.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_direct_indexed_nonuniform_fixture',
    input: 'compute_sm6_direct_indexed_nonuniform.hlsl',
    output: 'compute_sm6_direct_indexed_nonuniform.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_wave_ops_fixture',
    input: 'compute_sm6_wave_ops.hlsl',
    output: 'compute_sm6_wave_ops.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_5', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_wave_size_unsupported_fixture',
    input: 'compute_sm6_wave_size_unsupported.hlsl',
    output: 'compute_sm6_wave_size_unsupported.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_wave_size_32_fixture',
    input: 'compute_sm6_wave_size_32.hlsl',
    output: 'compute_sm6_wave_size_32.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_texture_dimensions_sm6_fixture',
    input: 'texture_dimensions_sm6.hlsl',
    output: 'texture_dimensions_sm6.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  foreach plane : ['depth', 'stencil']
    custom_target('dx12_texture_dimensions_' + plane + '_distinct_fixture',
      input: 'texture_dimensions_sm6.hlsl',
      output: 'texture_dimensions_' + plane + '_distinct.cs.cso',
      command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '-D', plane.to_upper() + '_MS=1', '-D', 'MS_DISTINCT=1', '@INPUT@', '-Fo', '@OUTPUT@' ],
      build_by_default: false,
    )
    foreach samples : ['2', '4', '8']
      custom_target('dx12_texture_dimensions_' + plane + '_pixel_' + samples + 'x_fixture',
        input: 'texture_dimensions_sm6.hlsl',
        output: 'texture_dimensions_' + plane + '_pixel_' + samples + 'x.ps.cso',
        command: dxc_command + [ '-E', 'pixel_main', '-T', 'ps_6_0', '-D', plane.to_upper() + '_MS=1', '-D', 'MS_DISTINCT=1', '-D', 'MS_PIXEL_READ=1', '-D', 'MS_SAMPLES=' + samples, '@INPUT@', '-Fo', '@OUTPUT@' ],
        build_by_default: false,
      )
    endforeach
    foreach samples : ['2', '8']
      custom_target('dx12_texture_dimensions_' + plane + '_distinct_' + samples + 'x_fixture',
        input: 'texture_dimensions_sm6.hlsl',
        output: 'texture_dimensions_' + plane + '_distinct_' + samples + 'x.cs.cso',
        command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '-D', plane.to_upper() + '_MS=1', '-D', 'MS_DISTINCT=1', '-D', 'MS_SAMPLES=' + samples, '@INPUT@', '-Fo', '@OUTPUT@' ],
        build_by_default: false,
      )
    endforeach
  endforeach
  foreach stage : ['vs', 'ps']
    custom_target('dx12_texture_dimensions_ms_write_' + stage + '_fixture',
      input: 'texture_dimensions_sm6.hlsl',
      output: 'texture_dimensions_ms_write.' + stage + '.cso',
      command: dxc_command + [ '-E', stage == 'vs' ? 'vertex_main' : 'pixel_main', '-T', stage + '_6_0', '-D', 'MS_WRITE=1', '@INPUT@', '-Fo', '@OUTPUT@' ],
      build_by_default: false,
    )
  endforeach
  custom_target('dx12_texture_dimensions_ms_write_coverage_fixture',
    input: 'texture_dimensions_sm6.hlsl',
    output: 'texture_dimensions_ms_write_coverage.ps.cso',
    command: dxc_command + [ '-E', 'pixel_main', '-T', 'ps_6_0', '-D', 'MS_WRITE=1', '-D', 'MS_COVERAGE=1', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  foreach plane : ['depth', 'stencil']
    foreach mask : ['ZERO', 'SUBSET']
      custom_target('dx12_texture_dimensions_' + plane + '_' + mask.to_lower() + '_fixture',
        input: 'texture_dimensions_sm6.hlsl',
        output: 'texture_dimensions_' + plane + '_' + mask.to_lower() + '.cs.cso',
        command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '-D', plane.to_upper() + '_MS=1', '-D', 'MS_DISTINCT=1', '-D', 'MS_' + mask + '=1', '@INPUT@', '-Fo', '@OUTPUT@' ],
        build_by_default: false,
      )
    endforeach
  endforeach
  custom_target('dx12_texture_dimensions_ms_write_subset_fixture',
    input: 'texture_dimensions_sm6.hlsl',
    output: 'texture_dimensions_ms_write_subset.ps.cso',
    command: dxc_command + [ '-E', 'pixel_main', '-T', 'ps_6_0', '-D', 'MS_WRITE=1', '-D', 'MS_COVERAGE=1', '-D', 'MS_COVERAGE_SUBSET=1', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_int64_fixture',
    input: 'compute_sm6_int64.hlsl',
    output: 'compute_sm6_int64.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_native16_fixture',
    input: 'compute_sm6_native16.hlsl',
    output: 'compute_sm6_native16.cs.cso',
    command: dxc_command + [ '-enable-16bit-types', '-E', 'main', '-T', 'cs_6_2', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  foreach kind : ['int64', 'native16']
    custom_target('dx12_compute_sm6_' + kind + '_differential_fixture',
      input: 'compute_sm6_' + kind + '.hlsl',
      output: 'compute_sm6_' + kind + '_differential.cs.cso',
      command: dxc_command + (kind == 'native16' ? ['-enable-16bit-types'] : []) +
        [ '-E', 'main', '-T', kind == 'native16' ? 'cs_6_2' : 'cs_6_0',
          '-D', 'NUMERIC_DIFFERENTIAL=1', '@INPUT@', '-Fo', '@OUTPUT@' ],
      build_by_default: false,
    )
  endforeach
  custom_target('dx12_compute_sm6_native16_denorm_preserve_fixture',
    input: 'compute_sm6_native16.hlsl',
    output: 'compute_sm6_native16_denorm_preserve.cs.cso',
    command: dxc_command + [ '-enable-16bit-types', '-denorm', 'preserve', '-E', 'main', '-T', 'cs_6_2', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_native16_denorm_ftz_fixture',
    input: 'compute_sm6_native16.hlsl',
    output: 'compute_sm6_native16_denorm_ftz.cs.cso',
    command: dxc_command + [ '-enable-16bit-types', '-denorm', 'ftz', '-E', 'main', '-T', 'cs_6_2', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_library_subobjects_sm6_fixture',
    input: 'library_subobjects_sm6.hlsl',
    output: 'library_subobjects_sm6.lib.cso',
    command: dxc_command + [ '-T', 'lib_6_3', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_ray_stages_sm6_fixture',
    input: 'ray_stages_sm6.hlsl',
    output: 'ray_stages_sm6.lib.cso',
    command: dxc_command + [ '-T', 'lib_6_3', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_ray_dispatch_sm6_fixture',
    input: 'ray_dispatch_sm6.hlsl',
    output: 'ray_dispatch_sm6.lib.cso',
    command: dxc_command + [ '-T', 'lib_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_ray_trace_sm6_fixture',
    input: 'ray_trace_sm6.hlsl',
    output: 'ray_trace_sm6.lib.cso',
    command: dxc_command + [ '-T', 'lib_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_ray_payload_qualifiers_sm6_fixture',
    input: 'ray_payload_qualifiers_sm6.hlsl',
    output: 'ray_payload_qualifiers_sm6.lib.cso',
    command: dxc_command + [ '-enable-payload-qualifiers', '-T', 'lib_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_mesh_sm6_ms_fixture',
    input: 'mesh_sm6.hlsl',
    output: 'mesh_sm6.ms.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'ms_6_5', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_mesh_sm6_cull_primitive_fixture',
    input: 'mesh_sm6_cull_primitive.hlsl',
    output: 'mesh_sm6_cull_primitive.ms.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'ms_6_5', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_mesh_sm6_as_fixture',
    input: 'mesh_sm6_as.hlsl',
    output: 'mesh_sm6.as.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'as_6_5', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_mesh_sm6_ps_fixture',
    input: 'mesh_sm6.hlsl',
    output: 'mesh_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_packed_dot_fixture',
    input: 'compute_sm6_packed_dot.hlsl',
    output: 'compute_sm6_packed_dot.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_4', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_pack_unpack_fixture',
    input: 'compute_sm6_pack_unpack.hlsl',
    output: 'compute_sm6_pack_unpack.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_derivatives_fixture',
    input: 'compute_sm6_derivatives.hlsl',
    output: 'compute_sm6_derivatives.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_derivatives_unsupported_fixture',
    input: 'compute_sm6_derivatives_unsupported.hlsl',
    output: 'compute_sm6_derivatives_unsupported.cs.cso',
    command: dxc_command + [ '-Vd', '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_atomic64_fixture',
    input: 'compute_sm6_atomic64.hlsl',
    output: 'compute_sm6_atomic64.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_texture_sampler_fixture',
    input: 'texture_sampler.hlsl',
    output: 'texture_sampler.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  foreach kind : [ 'float', 'uint', 'sint', 'snorm', 'packed_uint', 'alpha' ]
    custom_target('dx12_texture_sampler_typed_uav_' + kind + '_fixture',
      input: 'texture_sampler.hlsl',
      output: 'texture_sampler_typed_uav_' + kind + '.cs.cso',
      command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '-D', 'TYPED_UAV_' + kind.to_upper() + '=1', '@INPUT@', '-Fo', '@OUTPUT@' ],
      build_by_default: false,
    )
  endforeach
  custom_target('dx12_texture_sampler_direct_indexed_fixture',
    input: 'texture_sampler_direct_indexed.hlsl',
    output: 'texture_sampler_direct_indexed.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_texture_sampler_direct_indexed_uav_texture_fixture',
    input: 'texture_sampler_direct_indexed_uav_texture.hlsl',
    output: 'texture_sampler_direct_indexed_uav_texture.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_sm6_vs_fixture',
    input: 'graphics_sm6.hlsl',
    output: 'graphics_sm6.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_sm6_ps_fixture',
    input: 'graphics_sm6.hlsl',
    output: 'graphics_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_wave_ops_sm6_vs_fixture',
    input: 'graphics_wave_ops_sm6.hlsl',
    output: 'graphics_wave_ops_sm6.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_wave_ops_sm6_ps_fixture',
    input: 'graphics_wave_ops_sm6.hlsl',
    output: 'graphics_wave_ops_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_wave_quad', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_shader_ops_sm6_int64_vs_fixture',
    input: 'graphics_shader_ops_sm6.hlsl',
    output: 'graphics_shader_ops_sm6_int64.vs.cso',
    command: dxc_command + [ '-E', 'vs_int64', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_shader_ops_sm6_int64_ps_fixture',
    input: 'graphics_shader_ops_sm6.hlsl',
    output: 'graphics_shader_ops_sm6_int64.ps.cso',
    command: dxc_command + [ '-E', 'ps_int64', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_shader_ops_sm6_native16_vs_fixture',
    input: 'graphics_shader_ops_sm6.hlsl',
    output: 'graphics_shader_ops_sm6_native16.vs.cso',
    command: dxc_command + [ '-enable-16bit-types', '-DDXMT_NATIVE16', '-E', 'vs_native16', '-T', 'vs_6_2', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_shader_ops_sm6_native16_ps_fixture',
    input: 'graphics_shader_ops_sm6.hlsl',
    output: 'graphics_shader_ops_sm6_native16.ps.cso',
    command: dxc_command + [ '-enable-16bit-types', '-DDXMT_NATIVE16', '-E', 'ps_native16', '-T', 'ps_6_2', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_stencil_ref_sm6_fixture',
    input: 'graphics_stencil_ref_sm6.hlsl',
    output: 'graphics_stencil_ref_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_barycentrics_sm6_vs_fixture',
    input: 'graphics_barycentrics_sm6.hlsl',
    output: 'graphics_barycentrics_sm6.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_barycentrics_sm6_ps_fixture',
    input: 'graphics_barycentrics_sm6.hlsl',
    output: 'graphics_barycentrics_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_1', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_helper_lane_sm6_vs_fixture',
    input: 'graphics_helper_lane_sm6.hlsl',
    output: 'graphics_helper_lane_sm6.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_helper_lane_sm6_ps_fixture',
    input: 'graphics_helper_lane_sm6.hlsl',
    output: 'graphics_helper_lane_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_helper_lane', '-T', 'ps_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_helper_lane_derivative_sm6_ps_fixture',
    input: 'graphics_helper_lane_sm6.hlsl',
    output: 'graphics_helper_lane_derivative_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_helper_lane_derivative', '-T', 'ps_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_helper_lane_discard_sm6_ps_fixture',
    input: 'graphics_helper_lane_sm6.hlsl',
    output: 'graphics_helper_lane_discard_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_helper_lane_discard', '-T', 'ps_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_unsupported_semantics_sm6_vs_fixture',
    input: 'graphics_unsupported_semantics_sm6.hlsl',
    output: 'graphics_unsupported_semantics_sm6.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_view_id_sm6_ps_fixture',
    input: 'graphics_unsupported_semantics_sm6.hlsl',
    output: 'graphics_view_id_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_view_id', '-T', 'ps_6_1', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_get_attribute_sm6_ps_fixture',
    input: 'graphics_unsupported_semantics_sm6.hlsl',
    output: 'graphics_get_attribute_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_get_attribute', '-T', 'ps_6_1', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_vrs_sm6_ps_fixture',
    input: 'graphics_unsupported_semantics_sm6.hlsl',
    output: 'graphics_vrs_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_shading_rate', '-T', 'ps_6_4', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_logic_op_sm6_vs_fixture',
    input: 'graphics_logic_op_sm6.hlsl',
    output: 'graphics_logic_op_sm6.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_logic_op_sm6_ps_fixture',
    input: 'graphics_logic_op_sm6.hlsl',
    output: 'graphics_logic_op_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  foreach stage : ['vs', 'ps']
    custom_target('dx12_vertex_stride_' + stage,
      input: 'graphics_sm6.hlsl',
      output: 'vertex_stride.' + stage + '.cso',
      command: dxc_command + [ '-E', stage + '_main', '-T', stage + '_6_0',
        '-D', 'VERTEX_STRIDE_DIFFERENTIAL=1', '@INPUT@', '-Fo', '@OUTPUT@' ],
      build_by_default: false,
    )
  endforeach
  foreach input_count : ['1', '2', '3', '4', '6']
    foreach stage : ['vs', 'gs', 'ps']
      custom_target('dx12_geometry_differential_' + input_count + '_' + stage,
        input: 'geometry_sm6.hlsl',
        output: 'geometry_differential_' + input_count + '.' + stage + '.cso',
        command: dxc_command + [ '-E', stage + '_main', '-T', stage + '_6_0',
          '-D', 'GS_DIFFERENTIAL=1', '-D', 'GS_INPUT=' + input_count,
          '@INPUT@', '-Fo', '@OUTPUT@' ],
        build_by_default: false,
      )
    endforeach
  endforeach
  foreach stage : ['vs', 'gs', 'ps']
    custom_target('dx12_geometry_instanced_differential_' + stage,
      input: 'geometry_sm6.hlsl',
      output: 'geometry_instanced_differential.' + stage + '.cso',
      command: dxc_command + [ '-E', stage + '_main', '-T', stage + '_6_0',
        '-D', 'GS_DIFFERENTIAL=1', '-D', 'GS_INPUT=3', '-D', 'GS_INSTANCES=3',
        '@INPUT@', '-Fo', '@OUTPUT@' ],
      build_by_default: false,
    )
  endforeach
  foreach instances : ['2', '32']
    foreach stage : ['vs', 'gs', 'ps']
      custom_target('dx12_geometry_instanced_differential_' + instances + '_' + stage,
        input: 'geometry_sm6.hlsl',
        output: 'geometry_instanced_differential_' + instances + '.' + stage + '.cso',
        command: dxc_command + [ '-E', stage + '_main', '-T', stage + '_6_0',
          '-D', 'GS_DIFFERENTIAL=1', '-D', 'GS_INPUT=3', '-D', 'GS_INSTANCES=' + instances,
          '@INPUT@', '-Fo', '@OUTPUT@' ],
        build_by_default: false,
      )
    endforeach
  endforeach
  foreach input_count : ['1', '2', '4', '6']
    foreach stage : ['vs', 'gs', 'ps']
      custom_target('dx12_geometry_instanced_input_' + input_count + '_' + stage,
        input: 'geometry_sm6.hlsl',
        output: 'geometry_instanced_input_' + input_count + '.' + stage + '.cso',
        command: dxc_command + [ '-E', stage + '_main', '-T', stage + '_6_0',
          '-D', 'GS_DIFFERENTIAL=1', '-D', 'GS_INPUT=' + input_count, '-D', 'GS_INSTANCES=3',
          '@INPUT@', '-Fo', '@OUTPUT@' ],
        build_by_default: false,
      )
    endforeach
  endforeach
  custom_target('dx12_geometry_instanced_max_output_gs',
    input: 'geometry_sm6.hlsl',
    output: 'geometry_instanced_differential_32_max.gs.cso',
    command: dxc_command + [ '-E', 'gs_main', '-T', 'gs_6_0',
      '-D', 'GS_DIFFERENTIAL=1', '-D', 'GS_INPUT=3', '-D', 'GS_INSTANCES=32',
      '-D', 'GS_MAX_OUTPUT=128', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  foreach instances : ['1', '3']
    foreach stage : ['vs', 'gs', 'ps']
      custom_target('dx12_geometry_msc_point_' + instances + '_' + stage,
        input: 'geometry_sm6.hlsl',
        output: 'geometry_msc_point_' + instances + '.' + stage + '.cso',
        command: dxc_command + [ '-E', stage + '_main', '-T', stage + '_6_0',
          '-D', 'POINT_STREAM_DIFFERENTIAL=1', '-D', 'GS_INSTANCES=' + instances,
          '@INPUT@', '-Fo', '@OUTPUT@' ],
        build_by_default: false,
      )
    endforeach
  endforeach
  foreach instances : ['1', '3']
    custom_target('dx12_geometry_msc_point_nocut_' + instances + '_gs',
      input: 'geometry_sm6.hlsl',
      output: 'geometry_msc_point_nocut_' + instances + '.gs.cso',
      command: dxc_command + [ '-E', 'gs_main', '-T', 'gs_6_0',
        '-D', 'POINT_STREAM_DIFFERENTIAL=1', '-D', 'GS_INSTANCES=' + instances,
        '-D', 'POINT_STREAM_NO_CUT=1', '@INPUT@', '-Fo', '@OUTPUT@' ],
      build_by_default: false,
    )
  endforeach
  custom_target('dx12_geometry_invocation_counter_gs',
    input: 'geometry_sm6.hlsl',
    output: 'geometry_invocation_counter.gs.cso',
    command: dxc_command + [ '-E', 'gs_main', '-T', 'gs_6_0',
      '-D', 'GS_DIFFERENTIAL=1', '-D', 'GS_INPUT=3', '-D', 'GS_INVOCATION_COUNTER=1',
      '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_geometry_msc_point_invocation_map_gs',
    input: 'geometry_sm6.hlsl',
    output: 'geometry_msc_point_invocation_map.gs.cso',
    command: dxc_command + [ '-E', 'gs_main', '-T', 'gs_6_0',
      '-D', 'POINT_STREAM_DIFFERENTIAL=1', '-D', 'GS_INSTANCES=1',
      '-D', 'POINT_STREAM_INVOCATION_MAP=1', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  foreach count : ['1', '2']
    custom_target('dx12_geometry_msc_point_fixed_' + count + '_gs',
      input: 'geometry_sm6.hlsl',
      output: 'geometry_msc_point_fixed_' + count + '.gs.cso',
      command: dxc_command + [ '-E', 'gs_main', '-T', 'gs_6_0',
        '-D', 'POINT_STREAM_DIFFERENTIAL=1', '-D', 'GS_INSTANCES=1',
        '-D', 'POINT_STREAM_FORCE_EMIT=' + count, '@INPUT@', '-Fo', '@OUTPUT@' ],
      build_by_default: false,
    )
  endforeach
  custom_target('dx12_geometry_msc_point_fixed_1_max_1_gs',
    input: 'geometry_sm6.hlsl',
    output: 'geometry_msc_point_fixed_1_max_1.gs.cso',
    command: dxc_command + [ '-E', 'gs_main', '-T', 'gs_6_0',
      '-D', 'POINT_STREAM_DIFFERENTIAL=1', '-D', 'GS_INSTANCES=1',
      '-D', 'POINT_STREAM_FORCE_EMIT=1', '-D', 'POINT_STREAM_MAX_OUTPUT=1',
      '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_geometry_msc_point_invocation_map_fixed_2_gs',
    input: 'geometry_sm6.hlsl',
    output: 'geometry_msc_point_invocation_map_fixed_2.gs.cso',
    command: dxc_command + [ '-E', 'gs_main', '-T', 'gs_6_0',
      '-D', 'POINT_STREAM_DIFFERENTIAL=1', '-D', 'GS_INSTANCES=1',
      '-D', 'POINT_STREAM_INVOCATION_MAP=1', '-D', 'POINT_STREAM_FORCE_EMIT=2',
      '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_geometry_sm6_vs_fixture',
    input: 'geometry_sm6.hlsl',
    output: 'geometry_sm6.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_geometry_sm6_gs_fixture',
    input: 'geometry_sm6.hlsl',
    output: 'geometry_sm6.gs.cso',
    command: dxc_command + [ '-E', 'gs_main', '-T', 'gs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_geometry_sm6_ps_fixture',
    input: 'geometry_sm6.hlsl',
    output: 'geometry_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_geometry_adj_sm6_vs_fixture',
    input: 'geometry_adj_sm6.hlsl',
    output: 'geometry_adj_sm6.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_geometry_adj_sm6_gs_fixture',
    input: 'geometry_adj_sm6.hlsl',
    output: 'geometry_adj_sm6.gs.cso',
    command: dxc_command + [ '-E', 'gs_main', '-T', 'gs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_geometry_adj_sm6_ps_fixture',
    input: 'geometry_adj_sm6.hlsl',
    output: 'geometry_adj_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_geometry_root_cbv_sm6_vs_fixture',
    input: 'geometry_root_cbv_sm6.hlsl',
    output: 'geometry_root_cbv_sm6.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_geometry_root_cbv_sm6_gs_fixture',
    input: 'geometry_root_cbv_sm6.hlsl',
    output: 'geometry_root_cbv_sm6.gs.cso',
    command: dxc_command + [ '-E', 'gs_main', '-T', 'gs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_geometry_root_cbv_sm6_ps_fixture',
    input: 'geometry_root_cbv_sm6.hlsl',
    output: 'geometry_root_cbv_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_geometry_instanced_sm6_vs_fixture',
    input: 'geometry_instanced_sm6.hlsl',
    output: 'geometry_instanced_sm6.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_geometry_instanced_sm6_gs_fixture',
    input: 'geometry_instanced_sm6.hlsl',
    output: 'geometry_instanced_sm6.gs.cso',
    command: dxc_command + [ '-E', 'gs_main', '-T', 'gs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_geometry_instanced_sm6_ps_fixture',
    input: 'geometry_instanced_sm6.hlsl',
    output: 'geometry_instanced_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_root_cbv_sm6_vs_fixture',
    input: 'graphics_root_cbv_sm6.hlsl',
    output: 'graphics_root_cbv_sm6.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_root_cbv_sm6_ps_fixture',
    input: 'graphics_root_cbv_sm6.hlsl',
    output: 'graphics_root_cbv_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_root_constants_sm6_vs_fixture',
    input: 'graphics_root_constants_sm6.hlsl',
    output: 'graphics_root_constants_sm6.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_root_constants_sm6_ps_fixture',
    input: 'graphics_root_constants_sm6.hlsl',
    output: 'graphics_root_constants_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_root_srv_sm6_vs_fixture',
    input: 'graphics_root_srv_sm6.hlsl',
    output: 'graphics_root_srv_sm6.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_root_srv_sm6_ps_fixture',
    input: 'graphics_root_srv_sm6.hlsl',
    output: 'graphics_root_srv_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_root_uav_sm6_vs_fixture',
    input: 'graphics_root_uav_sm6.hlsl',
    output: 'graphics_root_uav_sm6.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_root_uav_sm6_ps_fixture',
    input: 'graphics_root_uav_sm6.hlsl',
    output: 'graphics_root_uav_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_root_cbv_texture_sm6_vs_fixture',
    input: 'graphics_root_cbv_texture_sm6.hlsl',
    output: 'graphics_root_cbv_texture_sm6.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_root_cbv_texture_sm6_ps_fixture',
    input: 'graphics_root_cbv_texture_sm6.hlsl',
    output: 'graphics_root_cbv_texture_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_texture_sm6_vs_fixture',
    input: 'graphics_texture_sm6.hlsl',
    output: 'graphics_texture_sm6.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_texture_sm6_ps_fixture',
    input: 'graphics_texture_sm6.hlsl',
    output: 'graphics_texture_sm6.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_hdr_vs_fixture',
    input: 'dx12_hdr.hlsl',
    output: 'dx12_hdr.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: true,
  )
  custom_target('dx12_hdr_linear_ps_fixture',
    input: 'dx12_hdr.hlsl',
    output: 'dx12_hdr.linear.ps.cso',
    command: dxc_command + [ '-E', 'ps_main_linear', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: true,
  )
  custom_target('dx12_hdr_pq_ps_fixture',
    input: 'dx12_hdr.hlsl',
    output: 'dx12_hdr.pq.ps.cso',
    command: dxc_command + [ '-E', 'ps_main_pq', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: true,
  )
endif

if dxc_command.length() > 0
  foreach entry : ['main', 'probe_read', 'probe_write']
    custom_target('dx12_reserved_buffer_' + entry + '_fixture',
      input: 'reserved_buffer.hlsl',
      output: 'reserved_buffer.' + entry + '.cs.cso',
      command: dxc_command + [ '-E', entry, '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
      build_by_default: false,
    )
  endforeach
  custom_target('dx12_reserved_texture_read_fixture',
    input: 'reserved_texture.hlsl',
    output: 'reserved_texture.read.cs.cso',
    command: dxc_command + [ '-E', 'read_phase', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_reserved_texture_write_fixture',
    input: 'reserved_texture.hlsl',
    output: 'reserved_texture.write.cs.cso',
    command: dxc_command + [ '-E', 'write_phase', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_reserved_texture_mip_array_fixture',
    input: 'reserved_texture_mip_array.hlsl',
    output: 'reserved_texture.mip_array.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  foreach entry : ['read_phase', 'write_phase_a', 'write_phase_b']
    custom_target('dx12_reserved_texture_packed_' + entry + '_fixture',
      input: 'reserved_texture_packed.hlsl',
      output: 'reserved_texture.packed.' + entry + '.cs.cso',
      command: dxc_command + [ '-E', entry, '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
      build_by_default: false,
    )
  endforeach
  custom_target('dx12_reserved_buffer_copy_mapping_fixture',
    input: 'reserved_buffer_copy_mapping.hlsl',
    output: 'reserved_buffer.copy_mapping.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_texture_lod_clamp_fixture',
    input: 'texture_lod_clamp.hlsl',
    output: 'texture_lod_clamp.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_texture_lod_clamp_read_fixture',
    input: 'texture_lod_clamp_read.hlsl',
    output: 'texture_lod_clamp_read.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_reserved_texture_view_range_fixture',
    input: 'reserved_texture_view_range.hlsl',
    output: 'reserved_texture.view_range.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
endif

dx12_reserved_buffer = executable('dx12_reserved_buffer', 'dx12_reserved_buffer.cpp', dependencies: [ d3d12_dep ])
dx12_reserved_texture = executable('dx12_reserved_texture', 'dx12_reserved_texture.cpp', dependencies: [ d3d12_dep ])
dx12_reserved_texture_mip_array = executable('dx12_reserved_texture_mip_array', 'dx12_reserved_texture_mip_array.cpp', dependencies: [ d3d12_dep ])
dx12_reserved_texture_packed = executable('dx12_reserved_texture_packed', 'dx12_reserved_texture_packed.cpp', dependencies: [ d3d12_dep ])
dx12_reserved_buffer_copy_mapping = executable('dx12_reserved_buffer_copy_mapping', 'dx12_reserved_buffer_copy_mapping.cpp', dependencies: [ d3d12_dep ])
dx12_tiled_status_sm5 = executable('dx12_tiled_status_sm5', 'dx12_tiled_status_sm5.cpp', dependencies: [ d3d12_dep ])
dx12_tiled_tier2_oracle = executable('dx12_tiled_tier2_oracle', 'dx12_tiled_tier2_oracle.cpp', dependencies: [ d3d12_dep, dxgi_dep ])
dx12_texture_lod_clamp = executable('dx12_texture_lod_clamp', 'dx12_texture_lod_clamp.cpp', dependencies: [ d3d12_dep ])
dx12_reserved_texture_view_range = executable('dx12_reserved_texture_view_range', 'dx12_reserved_texture_view_range.cpp', dependencies: [ d3d12_dep ])

if dxc_command.length() > 0
  custom_target('dx12_predication_vs_fixture',
    input: 'predication.hlsl',
    output: 'predication.vs.cso',
    command: dxc_command + [ '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_predication_ps_fixture',
    input: 'predication.hlsl',
    output: 'predication.ps.cso',
    command: dxc_command + [ '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_predication_cs_fixture',
    input: 'predication.hlsl',
    output: 'predication.cs.cso',
    command: dxc_command + [ '-E', 'cs_main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
endif

dx12_predication = executable('dx12_predication', 'dx12_predication.cpp', dependencies: [ d3d12_dep ])
dx12_enhanced_barriers = executable('dx12_enhanced_barriers', 'dx12_enhanced_barriers.cpp', dependencies: [ d3d12_dep ])
dx12_descriptor_stress = executable('dx12_descriptor_stress', 'dx12_descriptor_stress.cpp', dependencies: [ d3d12_dep ])
dx12_texture_map = executable('dx12_texture_map', 'dx12_texture_map.cpp', dependencies: [ d3d12_dep ])
dx12_texture_cpu_oracle = executable('dx12_texture_cpu_oracle', 'dx12_texture_cpu_oracle.cpp', dependencies: [ d3d12_dep ])
dx12_present_oracle = executable('dx12_present_oracle', 'dx12_present_oracle.cpp', dependencies: [ d3d12_dep, dxgi_dep ])
dx12_descriptor_ownership = executable(
  'dx12_descriptor_ownership', 'dx12_descriptor_ownership.cpp',
  include_directories: [
    include_directories('../../src/airconv'),
    include_directories('../../src/dxgi'),
    include_directories('../../src/dxmt'),
    include_directories('../../src/winemetal'),
    include_directories('../../src/util'),
  ],
  dependencies: [ d3d12_dep ],
)
dx12_pipeline_statistics = executable('dx12_pipeline_statistics', 'dx12_pipeline_statistics.cpp', dependencies: [ d3d12_dep ])

python = import('python').find_installation('python3', required: false)
if python.found()
  run_target('dx12_tiled_tier2_gate',
    command: [python, files('dx12_tiled_tier2_gate'), '--build-dir', meson.current_build_dir()],
  )
endif
