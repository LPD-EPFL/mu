#include <iostream>
#include <vector>

#include "buffer_overlay.cpp"

void boradcast_buffer_test() {
  printf("---boradcast_buffer_test---\n");
  const auto buf_size = 2048;

  auto buf = (uint8_t*)calloc(buf_size, sizeof(uint8_t));
  auto bcast_buf = BroadcastBuffer(buf, buf_size);

  auto e = bcast_buf.get_entry(1);

  printf("ID: %lu, Content: %s\n", e->id(), e->content());

  char str[100] = "Hello World!";

  bcast_buf.write(1, 1, (uint8_t*)&str, strlen(str));
  bcast_buf.write(10, 10, (uint8_t*)&str, strlen(str));

  e = bcast_buf.get_entry(1);
  printf("ID: %lu, Content: %s\n", e->id(), e->content());

  e = bcast_buf.get_entry(10);
  printf("ID: %lu, Content: %s\n", e->id(), e->content());

  try {
    bcast_buf.get_entry(1000);
  } catch (const std::exception& e) {
    printf("correctly thrown: %s for index 1000\n", e.what());
  }

  try {
    bcast_buf.get_entry(0);
  } catch (const std::out_of_range& e) {
    printf("correctly thrown: %s for index 0\n", e.what());
  }
}

void replay_buffer_write_test() {
  printf("---replay_buffer_write_test---\n");
  const auto buf_size = 2048;
  const auto num_proc = 4;
  const auto process_space = 512;

  auto buf = (uint8_t*)calloc(buf_size, sizeof(uint8_t));
  auto replay_buf_w = ReplayBufferWriter(buf, buf_size, num_proc);

  // origin_id, entry_index, expected_offset
  typedef std::tuple<uint64_t, uint64_t, uint64_t> test_case;

  auto cases = std::vector<test_case>();

  cases.push_back({0, 1, 0});
  cases.push_back({0, 2, BUFFER_ENTRY_SIZE});
  cases.push_back({1, 1, process_space});

  for (auto c : cases) {
    auto offset = replay_buf_w.get_byte_offset(std::get<0>(c), std::get<1>(c));

    if (offset != std::get<2>(c)) {
      std::cerr << "[ERR] wrong offset, expected: " << std::get<2>(c)
                << ", got: " << offset << std::endl;
      throw;
    }
  }

  auto offset = replay_buf_w.get_byte_offset(0, 4);

  if (offset != 3 * BUFFER_ENTRY_SIZE) {
    throw std::logic_error("wrong offset returned");
  }
}

void replay_buffer_read_test() {
  printf("---replay_buffer_read_test---\n");
  const auto buf_size = BUFFER_SIZE;
  const auto num_proc = 4;
  const auto process_space = 4096 * 4 * BUFFER_ENTRY_SIZE;
  const auto entry_space = BUFFER_ENTRY_SIZE * num_proc;

  auto buf = (uint8_t*)calloc(buf_size, sizeof(uint8_t));
  auto replay_buf_r = ReplayBufferReader(buf, buf_size, num_proc);

  // origin_id, replayer_id, entry_index, expected_offset
  typedef std::tuple<uint64_t, uint64_t, uint64_t, uint64_t> test_case;

  auto cases = std::vector<test_case>();

  cases.push_back({0, 0, 1, 0});
  cases.push_back({0, 0, 2, num_proc * BUFFER_ENTRY_SIZE});
  cases.push_back({1, 0, 1, process_space});
  cases.push_back({1, 1, 1, process_space + BUFFER_ENTRY_SIZE});
  cases.push_back({1, 2, 1, process_space + BUFFER_ENTRY_SIZE * 2});
  cases.push_back({1, 0, 2, process_space + entry_space });

  for (auto c : cases) {
    auto offset = replay_buf_r.get_byte_offset(std::get<0>(c), std::get<1>(c),
                                               std::get<2>(c));

    if (offset != std::get<3>(c)) {
      std::cerr << "[ERR] wrong offset, expected: " << std::get<3>(c)
                << ", got: " << offset << std::endl;
      throw;
    }
  }
}

int main() {
  boradcast_buffer_test();
  replay_buffer_write_test();
  replay_buffer_read_test();

  std::cout << "Tests passed!" << std::endl;
}