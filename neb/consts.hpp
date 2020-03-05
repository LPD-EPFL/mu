#pragma once

static constexpr size_t ib_port_index = 0;
static constexpr size_t kRoCE = false; ///< Use RoCE
static constexpr size_t kHrdMaxInline = 16;
static constexpr size_t kHrdSQDepth = 128;  ///< Depth of all SEND queues
static constexpr size_t kHrdRQDepth = 2048; ///< Depth of all RECV queues
static constexpr uint32_t kHrdInvalidNUMANode = 9;
static constexpr uint32_t kHrdDefaultPSN = 3185;
static constexpr uint32_t kHrdDefaultQKey = 0x11111111;
static constexpr size_t kHrdMaxLID = 256;
static constexpr size_t kHrdMaxUDQPs = 256; ///< Maximum number of UD QPs
// The lenght of the QP name published to memcached
static constexpr size_t QP_NAME_LENGTH = 200;
static constexpr auto RESERVED_NAME_PREFIX = "NEB__READY__";
static constexpr size_t DEFAULT_NUM_PROCESSES = 4;
static constexpr int DEFAULT_MEMCACHED_PORT = 11212;
static constexpr auto ENV_REGISTRY_IP = "DORY_REGISTRY_IP";
static constexpr size_t BUFFER_SIZE = 8 * 1024 * 1024;
// fixed size for every entry in the buffer for quick access
static constexpr int BUFFER_ENTRY_SIZE = 128;
// 8 bytes for storing the message id
static constexpr int NEB_MSG_OVERHEAD = 8;