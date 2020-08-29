#pragma once

#include <thread>

namespace dory {
void pinThreadToCore(std::thread &thd, int cpu_id);

void setThreadName(std::thread::native_handle_type pthread, char const *name);
void setThreadName(std::thread &thd, char const *name);
}  // namespace dory