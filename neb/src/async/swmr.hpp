#include <unordered_map>
#include <vector>

/**
 * Until now this SWMR class only stores the ids of remote processes from which
 * it got a ACK for the read. As soon as the minority is reached, together with
 * the local process a read majority is gathered and one can loop over the
 * temporal slots stored in the mem-pool.
 *
 * The completed boolean indicates that all subsequent ACKs can be handled by
 * immediately releasing the slot from the mem-pool, making it available again.
 **/
class SWMRRegister {
 public:
  void set_complete(int count) {
    completed[count] = true;
    read_lists[count].clear();
  }

  bool is_complete(int count) {
    auto it = completed.find(count);

    return it == completed.end() ? false : it->second;
  }

  void reset(int count) {
    auto it = completed.find(count);

    if (it != completed.end()) {
      completed.erase(it);
      complete_lists[count].clear();
    }
  }

  void add_to(int count, int pid) { read_lists[count].insert(pid); }

  void add_to_complete(int count, int pid) {
    complete_lists[count].push_back(pid);
  }

  std::set<int>& readlist(int count) { return read_lists[count]; }

  size_t read_list_size(int count) { return read_lists[count].size(); }

  size_t complete_list_size(int count) { return complete_lists[count].size(); }

 private:
  std::unordered_map<int, std::set<int>> read_lists;
  std::unordered_map<int, std::vector<int>> complete_lists;
  std::unordered_map<int, bool> completed;
};