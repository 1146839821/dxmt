#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>

namespace dxmt {

// Locks one key at a time without holding the table lock during compilation.
// Waiting callers hold shared ownership, so the gate remains alive until the
// last caller leaves. Failed compilations are not cached by this class.
template <typename Key> class MSCFlightTable {
public:
  class Guard {
  public:
    Guard(MSCFlightTable &table, const Key &key) :
        table_(table),
        key_(key),
        mutex_(table.acquire(key)),
        lock_(*mutex_) {}
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;
    ~Guard() {
      lock_.unlock();
      std::lock_guard<std::mutex> table_lock(table_.mutex_);
      if (mutex_.use_count() == 1)
        table_.gates_.erase(key_);
    }

  private:
    MSCFlightTable &table_;
    Key key_;
    std::shared_ptr<std::mutex> mutex_;
    std::unique_lock<std::mutex> lock_;
  };

private:
  std::shared_ptr<std::mutex>
  acquire(const Key &key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto &weak = gates_[key];
    auto gate = weak.lock();
    if (!gate) {
      gate = std::make_shared<std::mutex>();
      weak = gate;
    }
    return gate;
  }

  std::mutex mutex_;
  std::unordered_map<Key, std::weak_ptr<std::mutex>> gates_;
};

} // namespace dxmt
