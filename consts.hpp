#pragma once

static const size_t ib_port_index = 0;
static constexpr size_t kAppBufSize = (8 * 1024);

static constexpr size_t kRoCE = false;  ///< Use RoCE
static constexpr size_t kHrdMaxInline = 16;
static constexpr size_t kHrdSQDepth = 128;   ///< Depth of all SEND queues
static constexpr size_t kHrdRQDepth = 2048;  ///< Depth of all RECV queues

static constexpr uint32_t kHrdInvalidNUMANode = 9;
static constexpr uint32_t kHrdDefaultPSN = 3185;
static constexpr uint32_t kHrdDefaultQKey = 0x11111111;
static constexpr size_t kHrdMaxLID = 256;
static constexpr size_t kHrdMaxUDQPs = 256;  ///< Maximum number of UD QPs

static constexpr size_t kHrdQPNameSize = 200;
