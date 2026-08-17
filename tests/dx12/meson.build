dx12_sm6_probe = executable('dx12_compute_sm6', 'dx12_compute_sm6.cpp',
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
endif
