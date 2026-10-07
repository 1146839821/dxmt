#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "winemetal.h"
#include <cstdio>
#include <initializer_list>

// Exercise the optional PE -> Unix boundary, not shader access feedback.
int main() {
  HMODULE module = LoadLibraryA("winemetal.dll");
  if (!module) return 1;
#define LOAD(name) \
  auto fn_##name = reinterpret_cast<decltype(&name)>(GetProcAddress(module, #name)); \
  if (!fn_##name) { std::printf("Missing export: %s\n", #name); return 1; }
  LOAD(NSAutoreleasePool_alloc_init);
  LOAD(NSObject_release);
  LOAD(WMTCopyAllDevices);
  LOAD(NSArray_count);
  LOAD(NSArray_object);
  LOAD(MTLDevice_supportsPlacementSparse);
  LOAD(MTLDevice_newSparseMappingQueue);
  LOAD(MTLDevice_newPlacementSparseBuffer);
  LOAD(MTLDevice_newBuffer);
  LOAD(MTLDevice_newHeap);
  LOAD(MTLDevice_newSharedEvent);
  LOAD(SparseMappingQueue_updateBufferMappingsWithSideband);
  LOAD(SparseMappingQueue_signalEvent);
  LOAD(MTLSharedEvent_waitUntilSignaledValue);
#undef LOAD
  const auto pool = fn_NSAutoreleasePool_alloc_init();
  const auto devices = fn_WMTCopyAllDevices();
  if (!devices || !fn_NSArray_count(devices)) return 1;
  const auto device = fn_NSArray_object(devices, 0);
  if (!fn_MTLDevice_supportsPlacementSparse(device)) return 77;
  const auto queue = fn_MTLDevice_newSparseMappingQueue(device);
  WMTBufferInfo sparse_info = {};
  sparse_info.length = 131072;
  sparse_info.options = WMTResourceStorageModePrivate;
  const auto sparse = fn_MTLDevice_newPlacementSparseBuffer(device, &sparse_info, WMTSparsePageSize64);
  WMTBufferInfo bitmap_info = {};
  bitmap_info.length = 2;
  bitmap_info.options = WMTResourceStorageModeShared;
  const auto bitmap = fn_MTLDevice_newBuffer(device, &bitmap_info);
  const WMTHeapInfo heap_info = {131072, WMTResourceStorageModePrivate, WMTHeapTypePlacement, WMTSparsePageSize64};
  const auto heap = fn_MTLDevice_newHeap(device, &heap_info);
  const auto done = fn_MTLDevice_newSharedEvent(device);
  if (!pool || !queue || !sparse || !bitmap || !heap || !done || !bitmap_info.memory.ptr) {
    std::printf("Allocation failure pool=%llx queue=%llx sparse=%llx bitmap=%llx heap=%llx done=%llx memory=%p\n",
        pool, queue, sparse, bitmap, heap, done, bitmap_info.memory.ptr);
    return 1;
  }
  bool ok = !fn_SparseMappingQueue_updateBufferMappingsWithSideband(queue, sparse, heap, bitmap, nullptr, 1);
  for (unsigned phase = 0; phase < 8 && ok; ++phase) {
    WMTUpdateSparseBufferMappingOperation operations[2] = {};
    for (unsigned tile = 0; tile < 2; ++tile) {
      operations[tile].mode = tile == (phase & 1) ? WMTSparseTextureMappingModeMap : WMTSparseTextureMappingModeUnmap;
      operations[tile].buffer_range = {tile, 1};
      operations[tile].heap_offset = (phase / 2) & 1;
    }
    ok = fn_SparseMappingQueue_updateBufferMappingsWithSideband(queue, sparse, heap, bitmap, operations, 2);
    if (!ok) break;
    fn_SparseMappingQueue_signalEvent(queue, done, phase + 1);
    ok = fn_MTLSharedEvent_waitUntilSignaledValue(done, phase + 1, 10000);
    if (!ok) return 1; // Do not release resources after an unproven completion.
    const auto *bytes = static_cast<const unsigned char *>(bitmap_info.memory.ptr);
    ok = bytes[0] == ((phase & 1) == 0) && bytes[1] == ((phase & 1) == 1);
  }
  // Waits precede object release; the helper owns its transient GPU objects.
  for (auto object : {done, heap, bitmap, sparse, queue, devices, pool}) fn_NSObject_release(object);
  FreeLibrary(module);
  std::printf("SPARSE_SIDEBAND_WINE %s (no shader feedback claim)\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
