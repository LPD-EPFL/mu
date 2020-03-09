#pragma once

static constexpr int DEFAULT_NUM_PROCESSES = 4;
static constexpr size_t BUFFER_SIZE = 8 * 1024 * 1024;
static constexpr size_t BUFFER_ENTRY_SIZE = 128;
static constexpr size_t NEB_MSG_OVERHEAD = 8;
static constexpr auto SPD_FORMAT_STR = "[%n:%^%l%$] %v";
