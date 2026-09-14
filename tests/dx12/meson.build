dx12_sm6_probe = executable('dx12_compute_sm6', 'dx12_compute_sm6.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_shader_container = executable('dx12_shader_container', 'dx12_shader_container.cpp', '../../src/util/util_md5.cpp',
  dependencies: [ d3d12_dep ],
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

dx12_graphics_sm6 = executable('dx12_graphics_sm6', 'dx12_graphics_sm6.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_graphics_sm5 = executable('dx12_graphics_sm5', 'dx12_graphics_sm5.cpp',
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

dx12_interface_support = executable('dx12_interface_support', 'dx12_interface_support.cpp',
  dependencies: [ d3d12_dep, dxgi_dep ],
)

dx12_command_list_type = executable('dx12_command_list_type', 'dx12_command_list_type.cpp',
  dependencies: [ d3d12_dep ],
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
  custom_target('dx12_compute_sm6_root_uav_fixture',
    input: 'compute_sm6_root_uav.hlsl',
    output: 'compute_sm6_root_uav.cs.cso',
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
  custom_target('dx12_compute_sm6_descriptor_resources_space_fixture',
    input: 'compute_sm6_descriptor_resources_space.hlsl',
    output: 'compute_sm6_descriptor_resources_space.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_direct_indexed_fixture',
    input: 'compute_sm6_direct_indexed.hlsl',
    output: 'compute_sm6_direct_indexed.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_wave_ops_fixture',
    input: 'compute_sm6_wave_ops.hlsl',
    output: 'compute_sm6_wave_ops.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_wave_size_unsupported_fixture',
    input: 'compute_sm6_wave_size_unsupported.hlsl',
    output: 'compute_sm6_wave_size_unsupported.cs.cso',
    command: dxc_command + [ '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
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
  custom_target('dx12_mesh_sm6_ms_fixture',
    input: 'mesh_sm6.hlsl',
    output: 'mesh_sm6.ms.cso',
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
  custom_target('dx12_texture_sampler_direct_indexed_fixture',
    input: 'texture_sampler_direct_indexed.hlsl',
    output: 'texture_sampler_direct_indexed.cs.cso',
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
