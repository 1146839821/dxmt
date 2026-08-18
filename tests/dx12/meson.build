dx12_sm6_probe = executable('dx12_compute_sm6', 'dx12_compute_sm6.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_texture_sampler = executable('dx12_texture_sampler', 'dx12_texture_sampler.cpp',
  dependencies: [ d3d12_dep ],
)

dx12_graphics_sm6 = executable('dx12_graphics_sm6', 'dx12_graphics_sm6.cpp',
  dependencies: [ d3d12_dep ],
)

dxc = find_program('dxc', required: false)
if dxc.found()
  custom_target('dx12_compute_sm6_fixture',
    input: 'compute_sm6.hlsl',
    output: 'compute_sm6.cs.cso',
    command: [ dxc, '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_root_uav_fixture',
    input: 'compute_sm6_root_uav.hlsl',
    output: 'compute_sm6_root_uav.cs.cso',
    command: [ dxc, '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_root_value_fixture',
    input: 'compute_sm6_root_value.hlsl',
    output: 'compute_sm6_root_value.cs.cso',
    command: [ dxc, '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_root_srv_fixture',
    input: 'compute_sm6_root_srv.hlsl',
    output: 'compute_sm6_root_srv.cs.cso',
    command: [ dxc, '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_descriptor_resources_fixture',
    input: 'compute_sm6_descriptor_resources.hlsl',
    output: 'compute_sm6_descriptor_resources.cs.cso',
    command: [ dxc, '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_descriptor_resources_space_fixture',
    input: 'compute_sm6_descriptor_resources_space.hlsl',
    output: 'compute_sm6_descriptor_resources_space.cs.cso',
    command: [ dxc, '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_compute_sm6_direct_indexed_fixture',
    input: 'compute_sm6_direct_indexed.hlsl',
    output: 'compute_sm6_direct_indexed.cs.cso',
    command: [ dxc, '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_texture_sampler_fixture',
    input: 'texture_sampler.hlsl',
    output: 'texture_sampler.cs.cso',
    command: [ dxc, '-E', 'main', '-T', 'cs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_texture_sampler_direct_indexed_fixture',
    input: 'texture_sampler_direct_indexed.hlsl',
    output: 'texture_sampler_direct_indexed.cs.cso',
    command: [ dxc, '-E', 'main', '-T', 'cs_6_6', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_sm6_vs_fixture',
    input: 'graphics_sm6.hlsl',
    output: 'graphics_sm6.vs.cso',
    command: [ dxc, '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_sm6_ps_fixture',
    input: 'graphics_sm6.hlsl',
    output: 'graphics_sm6.ps.cso',
    command: [ dxc, '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_texture_sm6_vs_fixture',
    input: 'graphics_texture_sm6.hlsl',
    output: 'graphics_texture_sm6.vs.cso',
    command: [ dxc, '-E', 'vs_main', '-T', 'vs_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
  custom_target('dx12_graphics_texture_sm6_ps_fixture',
    input: 'graphics_texture_sm6.hlsl',
    output: 'graphics_texture_sm6.ps.cso',
    command: [ dxc, '-E', 'ps_main', '-T', 'ps_6_0', '@INPUT@', '-Fo', '@OUTPUT@' ],
    build_by_default: false,
  )
endif
