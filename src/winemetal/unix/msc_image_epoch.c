// Candidate dyld event counter. It cannot identify image contents.
#include "msc_image_epoch.h"
#include <mach-o/dyld.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

static _Atomic uint64_t g_epoch;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static bool g_available;

static void
image_changed(const struct mach_header *header, intptr_t slide) {
  (void)header;
  (void)slide;
  uint64_t previous = atomic_load_explicit(&g_epoch, memory_order_seq_cst);
  while (previous != UINT64_MAX && !atomic_compare_exchange_weak_explicit(
                                       &g_epoch, &previous, previous + 1, memory_order_seq_cst, memory_order_seq_cst
                                   )) {
  }
}

static void
register_callbacks(void) {
  if (!atomic_is_lock_free(&g_epoch))
    return;
  // Observe removals during add-image enumeration; this is not an atomic snapshot.
  _dyld_register_func_for_remove_image(image_changed);
  _dyld_register_func_for_add_image(image_changed);
  g_available = true;
}

bool
dxmt_msc_image_epoch_start(void) {
  return pthread_once(&g_once, register_callbacks) == 0 && g_available;
}

bool
dxmt_msc_image_epoch_snapshot(uint64_t *epoch) {
  if (!epoch || !dxmt_msc_image_epoch_start())
    return false;
  uint64_t value = atomic_load_explicit(&g_epoch, memory_order_seq_cst);
  if (value == UINT64_MAX)
    return false;
  *epoch = value;
  return true;
}

bool
dxmt_msc_image_epoch_unchanged(uint64_t epoch) {
  uint64_t current;
  return epoch != UINT64_MAX && dxmt_msc_image_epoch_snapshot(&current) && current == epoch;
}

#ifdef DXMT_IMAGE_EPOCH_TESTING
void
dxmt_msc_image_epoch_test_set(uint64_t epoch) {
  atomic_store_explicit(&g_epoch, epoch, memory_order_seq_cst);
}
#endif
