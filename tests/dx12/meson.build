dx12_sm6_probe = executable('dx12_compute_sm6', 'dx12_compute_sm6.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_texture_sampler = executable('dx12_texture_sampler', 'dx12_texture_sampler.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_graphics_sm6 = executable('dx12_graphics_sm6', 'dx12_graphics_sm6.cpp',
  dependencies: [ d3d12_dep ],
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

dx12_pipeline_persistence = executable('dx12_pipeline_persistence', 'dx12_pipeline_persistence.cpp',
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
