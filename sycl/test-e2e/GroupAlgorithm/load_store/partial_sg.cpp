// RUN: %{build} -o %t.out
// RUN: %{run} %t.out

#include <sycl/detail/core.hpp>
#include <sycl/ext/oneapi/experimental/group_load_store.hpp>

#include <numeric>

using namespace sycl;
namespace sycl_exp = sycl::ext::oneapi::experimental;

template <int SG_SIZE> void test(queue &q) {
  // Verify scenario when the last sub_group isn't full.
  constexpr std::size_t wg_size = SG_SIZE * 3 / 2;
  constexpr std::size_t elems_per_wi = 4;
  constexpr std::size_t n = wg_size * elems_per_wi;

  buffer<int, 1> input_buf{n};

  {
    host_accessor acc{input_buf};
    std::iota(acc.begin(), acc.end(), 0);
  }

  buffer<int, 1> load_blocked_buf{n};
  buffer<int, 1> load_striped_buf{n};
  buffer<int, 1> store_blocked_buf{n};
  buffer<int, 1> store_striped_buf{n};

  q.submit([&](handler &cgh) {
    accessor input{input_buf, cgh};

    accessor load_blocked{load_blocked_buf, cgh};
    accessor load_striped{load_striped_buf, cgh};
    accessor store_blocked{store_blocked_buf, cgh};
    accessor store_striped{store_striped_buf, cgh};

    cgh.parallel_for(
        nd_range<1>{wg_size, wg_size},
        [=](nd_item<1> ndi) [[intel::reqd_sub_group_size(SG_SIZE)]] {
          auto gid = ndi.get_global_id(0);
          auto sg = ndi.get_sub_group();
          auto offset =
              sg.get_group_id() * sg.get_max_local_range() * elems_per_wi;

          int data[elems_per_wi];

          auto blocked = sycl_exp::properties{sycl_exp::data_placement_blocked};
          auto striped = sycl_exp::properties{sycl_exp::data_placement_striped};

          // blocked
          sycl_exp::group_load(sg, input.begin() + offset, span{data}, blocked);
          for (int i = 0; i < elems_per_wi; ++i)
            load_blocked[gid * elems_per_wi + i] = data[i];

          // striped
          sycl_exp::group_load(sg, input.begin() + offset, span{data}, striped);
          for (int i = 0; i < elems_per_wi; ++i)
            load_striped[gid * elems_per_wi + i] = data[i];

          // Stores:

          std::iota(std::begin(data), std::end(data), gid * elems_per_wi);

          sycl_exp::group_store(sg, span{data}, store_blocked.begin() + offset,
                                blocked);
          sycl_exp::group_store(sg, span{data}, store_striped.begin() + offset,
                                striped);
        });
  });

  host_accessor load_blocked{load_blocked_buf};
  host_accessor load_striped{load_striped_buf};
  host_accessor store_blocked{store_blocked_buf};
  host_accessor store_striped{store_striped_buf};

  // Check blocked.
  for (int i = 0; i < wg_size * elems_per_wi; ++i) {
    assert(load_blocked[i] == i);
    assert(store_blocked[i] == i);
  }

  // Check striped.
  for (int wi = 0; wi < wg_size; ++wi) {
    auto sg = wi / SG_SIZE;
    auto lid = wi % SG_SIZE;

    // sub_group size is different for full/partial.
    auto this_sg_size = sg == 0 ? SG_SIZE : SG_SIZE / 2;

    for (auto elem = 0; elem < elems_per_wi; ++elem) {
      auto striped_idx =
          sg * SG_SIZE * elems_per_wi + elem * this_sg_size + lid;
      assert(load_striped[wi * elems_per_wi + elem] == striped_idx);

      auto value_stored = wi * elems_per_wi + elem;
      assert(store_striped[striped_idx] == value_stored);
    }
  }
}

int main() {
  queue q;
  auto device_sg_sizes =
      q.get_device().get_info<info::device::sub_group_sizes>();

  constexpr std::size_t sg_sizes[] = {4, 8, 16, 32};

  detail::loop<std::size(sg_sizes)>([&](auto sg_size_idx) {
    constexpr auto sg_size = sg_sizes[sg_size_idx];
    if (std::any_of(device_sg_sizes.begin(), device_sg_sizes.end(),
                    [](auto x) { return x == sg_size; }))
      test<sg_size>(q);
  });

  return 0;
}
