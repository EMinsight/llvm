// REQUIRES: aspect-usm_shared_allocations
// RUN: %{build} %{embed-ir} -o %t.out
// RUN: %{run} %t.out

// Test validity of events after complete_fusion.

#include "fusion_event_test_common.h"

#include <sycl/detail/core.hpp>
#include <sycl/ext/codeplay/experimental/fusion_wrapper.hpp>
#include <sycl/properties/all_properties.hpp>
#include <sycl/usm.hpp>

using namespace sycl;

int main() {
  constexpr size_t dataSize = 512;

  queue q{ext::codeplay::experimental::property::queue::enable_fusion{}};

  int *in1 = sycl::malloc_shared<int>(dataSize, q);
  int *in2 = sycl::malloc_shared<int>(dataSize, q);
  int *in3 = sycl::malloc_shared<int>(dataSize, q);
  int *tmp = sycl::malloc_shared<int>(dataSize, q);
  int *out = sycl::malloc_shared<int>(dataSize, q);

  for (size_t i = 0; i < dataSize; ++i) {
    in1[i] = i * 2;
    in2[i] = i * 3;
    in3[i] = i * 4;
    tmp[i] = -1;
    out[i] = -1;
  }

  ext::codeplay::experimental::fusion_wrapper fw{q};
  fw.start_fusion();

  assert(fw.is_in_fusion_mode() && "Queue should be in fusion mode");

  auto kernel1 = q.submit([&](handler &cgh) {
    cgh.parallel_for<class KernelOne>(
        dataSize, [=](id<1> i) { tmp[i] = in1[i] + in2[i]; });
  });

  auto kernel2 = q.submit([&](handler &cgh) {
    cgh.parallel_for<class KernelTwo>(
        dataSize, [=](id<1> i) { out[i] = tmp[i] * in3[i]; });
  });

  auto complete = fw.complete_fusion(
      {ext::codeplay::experimental::property::no_barriers{}});

  assert(!fw.is_in_fusion_mode() &&
         "Queue should not be in fusion mode anymore");

  complete.wait();
  assert(isEventComplete(complete) && "Event should be complete");
  // The execution of the fused kennel does not depend on any events.
  assert(complete.get_wait_list().size() == 0);

  // Need to wait for the event 'kernel1' here, as it is the event associated
  // with the placeholder, which depends on 'complete', but is a separate event.
  kernel1.wait();
  assert(isEventComplete(kernel1) && "Event should be complete");
  // The event returned for submissions while in fusion mode depends on three
  // events, for the two original kernels (which do not execute) and the fused
  // kernel to be executed.
  assert(kernel1.get_wait_list().size() == 3);

  // 'kernel2' is the same event (associated with the placeholder) as 'kernel1',
  // so no need to wait again.
  assert(isEventComplete(kernel2) && "Event should be complete");
  // The event returned for submissions while in fusion mode depends on three
  // events, for the two original kernels (which do not execute) and the fused
  // kernel to be executed.
  assert(kernel2.get_wait_list().size() == 3);

  // Check the results.
  for (size_t i = 0; i < dataSize; ++i) {
    assert(out[i] == (20 * i * i) && "Computation error");
  }

  sycl::free(in1, q);
  sycl::free(in2, q);
  sycl::free(in3, q);
  sycl::free(tmp, q);
  sycl::free(out, q);

  return 0;
}
