; Captured MSC 4.0.1 compute AIR for msc_typed_buffer_gpu_fixture.hlsl.
; Offline experiment fixture; not a production shader ABI guarantee.
; ModuleID = '/Users/zhangbo/Documents/Vibe-Codeding/dxmt-test/n95-capability-gate/msc-buffer-air/multitable.bc'
source_filename = "main"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx14.0.0"

%struct.top_level_global_ab = type opaque
%struct.res_desc_heap_ab = type opaque
%struct.smp_desc_heap_ab = type opaque
%struct__desc = type { %struct._texture_buffer_1d_t addrspace(1)*, %struct._texture_buffer_1d_t addrspace(1)*, i64 }
%struct._texture_buffer_1d_t = type opaque

define void @main(<3 x i32> %0, i16 %1, i32 %2, <3 x i32> %3, %struct.top_level_global_ab addrspace(2)* %4, %struct.res_desc_heap_ab addrspace(2)* %5, %struct.smp_desc_heap_ab addrspace(2)* %6) local_unnamed_addr {
  %8 = bitcast %struct.top_level_global_ab addrspace(2)* %4 to i8 addrspace(2)*
  %9 = getelementptr inbounds i8, i8 addrspace(2)* %8, i64 16
  %10 = bitcast i8 addrspace(2)* %9 to i32 addrspace(1)* addrspace(2)* addrspace(2)*
  %11 = load i32 addrspace(1)* addrspace(2)*, i32 addrspace(1)* addrspace(2)* addrspace(2)* %10, align 8
  %..unpack6 = load i32 addrspace(1)*, i32 addrspace(1)* addrspace(2)* %11, align 8
  %12 = extractelement <3 x i32> %0, i64 0
  %13 = and i32 %12, 1
  %14 = bitcast %struct.top_level_global_ab addrspace(2)* %4 to %struct__desc addrspace(2)* addrspace(2)*
  %15 = load %struct__desc addrspace(2)*, %struct__desc addrspace(2)* addrspace(2)* %14, align 8
  %16 = zext i32 %13 to i64
  %.1.elt7 = getelementptr inbounds %struct__desc, %struct__desc addrspace(2)* %15, i64 %16, i32 1
  %.1.unpack8 = load %struct._texture_buffer_1d_t addrspace(1)*, %struct._texture_buffer_1d_t addrspace(1)* addrspace(2)* %.1.elt7, align 8
  %17 = call i1 @air.is_null_texture_buffer_1d(%struct._texture_buffer_1d_t addrspace(1)* %.1.unpack8)
  %18 = call i32 @air.get_width_texture_buffer_1d(%struct._texture_buffer_1d_t addrspace(1)* %.1.unpack8)
  %19 = select i1 %17, i32 0, i32 %18
  %20 = bitcast %struct.top_level_global_ab addrspace(2)* %4 to i8 addrspace(2)*
  %21 = getelementptr inbounds i8, i8 addrspace(2)* %20, i64 8
  %22 = bitcast i8 addrspace(2)* %21 to %struct__desc addrspace(2)* addrspace(2)*
  %23 = load %struct__desc addrspace(2)*, %struct__desc addrspace(2)* addrspace(2)* %22, align 8
  %24 = zext i32 %13 to i64
  %.2.elt12 = getelementptr inbounds %struct__desc, %struct__desc addrspace(2)* %23, i64 %24, i32 1
  %.2.unpack13 = load %struct._texture_buffer_1d_t addrspace(1)*, %struct._texture_buffer_1d_t addrspace(1)* addrspace(2)* %.2.elt12, align 8
  %25 = call i1 @air.is_null_texture_buffer_1d(%struct._texture_buffer_1d_t addrspace(1)* %.2.unpack13)
  %26 = call i32 @air.get_width_texture_buffer_1d(%struct._texture_buffer_1d_t addrspace(1)* %.2.unpack13)
  %27 = select i1 %25, i32 0, i32 %26
  call void @air.fence_texture_buffer_1d(%struct._texture_buffer_1d_t addrspace(1)* %.1.unpack8)
  %28 = call { <4 x i32>, i8 } @air.read_texture_buffer_1d.s.v4i32(%struct._texture_buffer_1d_t addrspace(1)* %.1.unpack8, i32 %12, i32 3)
  %29 = extractvalue { <4 x i32>, i8 } %28, 0
  %30 = extractelement <4 x i32> %29, i64 0
  call void @air.fence_texture_buffer_1d(%struct._texture_buffer_1d_t addrspace(1)* %.2.unpack13)
  %31 = call { <4 x i32>, i8 } @air.read_texture_buffer_1d.s.v4i32(%struct._texture_buffer_1d_t addrspace(1)* %.2.unpack13, i32 %12, i32 3)
  %32 = extractvalue { <4 x i32>, i8 } %31, 0
  %33 = extractelement <4 x i32> %32, i64 0
  %34 = add nsw i32 %30, 1
  %35 = insertelement <4 x i32> undef, i32 %34, i64 0
  %36 = shufflevector <4 x i32> %35, <4 x i32> poison, <4 x i32> zeroinitializer
  call void @air.write_texture_buffer_1d.s.v4i32(%struct._texture_buffer_1d_t addrspace(1)* %.1.unpack8, i32 %12, <4 x i32> %36, i32 3)
  %37 = add nsw i32 %33, 2
  %38 = insertelement <4 x i32> undef, i32 %37, i64 0
  %39 = shufflevector <4 x i32> %38, <4 x i32> poison, <4 x i32> zeroinitializer
  call void @air.write_texture_buffer_1d.s.v4i32(%struct._texture_buffer_1d_t addrspace(1)* %.2.unpack13, i32 %12, <4 x i32> %39, i32 3)
  %40 = add i32 %27, %19
  %41 = add i32 %40, %30
  %42 = add i32 %41, %33
  %43 = sext i32 %12 to i64
  %44 = getelementptr i32, i32 addrspace(1)* %..unpack6, i64 %43
  store i32 %42, i32 addrspace(1)* %44, align 4
  ret void
}

; Function Attrs: argmemonly nounwind writeonly
declare void @air.write_texture_buffer_1d.s.v4i32(%struct._texture_buffer_1d_t addrspace(1)*, i32, <4 x i32>, i32) local_unnamed_addr #0

; Function Attrs: argmemonly nounwind readonly
declare { <4 x i32>, i8 } @air.read_texture_buffer_1d.s.v4i32(%struct._texture_buffer_1d_t addrspace(1)*, i32, i32) local_unnamed_addr #1

; Function Attrs: nounwind
declare void @air.fence_texture_buffer_1d(%struct._texture_buffer_1d_t addrspace(1)*) local_unnamed_addr #2

; Function Attrs: argmemonly nounwind readonly
declare i32 @air.get_width_texture_buffer_1d(%struct._texture_buffer_1d_t addrspace(1)*) local_unnamed_addr #1

; Function Attrs: argmemonly nounwind readonly
declare i1 @air.is_null_texture_buffer_1d(%struct._texture_buffer_1d_t addrspace(1)*) local_unnamed_addr #1

attributes #0 = { argmemonly nounwind writeonly }
attributes #1 = { argmemonly nounwind readonly }
attributes #2 = { nounwind }

!llvm.module.flags = !{!0, !1, !2, !3, !4, !5, !6, !7, !8, !9, !10}
!llvm.ident = !{!11}
!air.version = !{!12}
!air.language_version = !{!13}
!air.kernel = !{!14}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 7, !"air.max_device_buffers", i32 31}
!2 = !{i32 7, !"air.max_constant_buffers", i32 31}
!3 = !{i32 7, !"air.max_threadgroup_buffers", i32 31}
!4 = !{i32 7, !"air.max_textures", i32 128}
!5 = !{i32 7, !"air.max_read_write_textures", i32 8}
!6 = !{i32 7, !"air.max_samplers", i32 16}
!7 = !{i32 7, !"agx.allow_mismatched_component_counts", i32 1}
!8 = !{i32 1, !"irc.version_major", i32 4}
!9 = !{i32 1, !"irc.version_minor", i32 0}
!10 = !{i32 1, !"irc.version_patch", i32 3}
!11 = !{!"dxcoob 1.9.2602.17 (21d28f727)"}
!12 = !{i32 2, i32 6, i32 0}
!13 = !{!"Metal", i32 3, i32 1, i32 0}
!14 = !{void (<3 x i32>, i16, i32, <3 x i32>, %struct.top_level_global_ab addrspace(2)*, %struct.res_desc_heap_ab addrspace(2)*, %struct.smp_desc_heap_ab addrspace(2)*)* @main, !15, !16, !24}
!15 = !{}
!16 = !{!17, !18, !19, !20, !21, !22, !23}
!17 = !{i32 0, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint3", !"air.arg_name", !"gid"}
!18 = !{i32 1, !"air.thread_index_in_quadgroup", !"air.arg_type_name", !"ushort", !"air.arg_name", !"tid_in_quad", !"air.arg_unused"}
!19 = !{i32 2, !"air.thread_index_in_threadgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"thread_index_in_threadgroup", !"air.arg_unused"}
!20 = !{i32 3, !"air.threadgroup_position_in_grid", !"air.arg_type_name", !"uint3", !"air.arg_name", !"tg_pos_in_grid", !"air.arg_unused"}
!21 = !{i32 4, !"air.indirect_buffer", !"air.location_index", i32 2, i32 1, !"air.read", !"air.address_space", i32 2, !"air.struct_type_info", !15, !"air.arg_type_name", !"struct.top_level_global_ab", !"air.arg_name", !"struct.top_level_global_ab"}
!22 = !{i32 5, !"air.indirect_buffer", !"air.location_index", i32 0, i32 1, !"air.read", !"air.address_space", i32 2, !"air.struct_type_info", !15, !"air.arg_type_name", !"struct.res_desc_heap_ab", !"air.arg_name", !"struct.res_desc_heap_ab", !"air.arg_unused"}
!23 = !{i32 6, !"air.indirect_buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.address_space", i32 2, !"air.struct_type_info", !15, !"air.arg_type_name", !"struct.smp_desc_heap_ab", !"air.arg_name", !"struct.smp_desc_heap_ab", !"air.arg_unused"}
!24 = !{!"air.max_work_group_size", i32 2}
