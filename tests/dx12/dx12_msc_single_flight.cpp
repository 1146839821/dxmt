#include "../../src/d3d12/d3d12_msc_single_flight.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

int main() {
  using namespace std::chrono_literals;
  dxmt::MSCFlightTable<int> table;
  std::atomic<unsigned> ready{0}, conversions{0}, active{0}, overlap{0};
  std::atomic<bool> start{false};
  bool cached = false;
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
    threads.emplace_back([&] {
      ++ready;
      while (!start.load())
        std::this_thread::yield();
      dxmt::MSCFlightTable<int>::Guard guard(table, 1);
      if (!cached) {
        if (active.fetch_add(1) != 0)
          ++overlap;
        ++conversions;
        std::this_thread::yield();
        cached = true;
        --active;
      }
    });
  while (ready.load() != 8)
    std::this_thread::yield();
  start = true;
  for (auto &thread : threads)
    thread.join();
  if (conversions != 1 || overlap != 0)
    return 1;

  std::promise<void> first_entered, second_entered, release_first;
  auto first_ready = first_entered.get_future();
  auto second_ready = second_entered.get_future();
  auto release = release_first.get_future();
  std::thread first([&] {
    dxmt::MSCFlightTable<int>::Guard guard(table, 10);
    first_entered.set_value();
    release.wait();
  });
  first_ready.wait();
  std::thread second([&] {
    dxmt::MSCFlightTable<int>::Guard guard(table, 11);
    second_entered.set_value();
  });
  bool parallel = second_ready.wait_for(3s) == std::future_status::ready;
  release_first.set_value();
  first.join();
  second.join();
  if (!parallel)
    return 2;

  try {
    dxmt::MSCFlightTable<int>::Guard guard(table, 20);
    throw 1;
  } catch (int) {
  }
  {
    dxmt::MSCFlightTable<int>::Guard guard(table, 20);
  }
  std::promise<void> failure_started, allow_failure, recovered;
  auto failure_ready = failure_started.get_future();
  auto allow = allow_failure.get_future();
  auto recovery = recovered.get_future();
  std::thread failing([&] {
    try {
      dxmt::MSCFlightTable<int>::Guard guard(table, 30);
      failure_started.set_value();
      allow.wait();
      throw 1;
    } catch (int) {
    }
  });
  failure_ready.wait();
  std::thread retry([&] {
    dxmt::MSCFlightTable<int>::Guard guard(table, 30);
    recovered.set_value();
  });
  allow_failure.set_value();
  bool recovery_ok = recovery.wait_for(3s) == std::future_status::ready;
  failing.join();
  retry.join();
  if (!recovery_ok)
    return 3;
  std::cout << "MSC same-key serialization, different-key parallelism and "
               "failure release passed\n";
  return 0;
}
