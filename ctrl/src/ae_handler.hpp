#pragma once

#include <future>

#include <dory/extern/ibverbs.hpp>
#include <dory/shared/logger.hpp>

#include "fcntl.h"
#include "poll.h"

namespace dory {
namespace ctrl {
/**
 * Handles the verbs async events by printing its type.
 * This functions should be running in a separate thread as it is constantly
 * polling on the async event queue, until the exit_sig future is ready.
 *
 * @param logger: the dory-logger
 * @param exit_sig: future to poll for an exit signal
 * @param ctx: pointer to the ibverbs context for which to poll for events
 **/
void async_event_handler(dory::logger logger, std::future<void> exit_sig,
                         struct ibv_context* ctx) {
  logger->info("Changing the mode of events read to be non-blocking");

  /* change the blocking mode of the async event queue */
  auto flags = fcntl(ctx->async_fd, F_GETFL);
  auto ret = fcntl(ctx->async_fd, F_SETFL, flags | O_NONBLOCK);
  if (ret < 0) {
    logger->error(
        "Error, failed to change file descriptor of async event queue");

    return;
  }

  struct ibv_async_event event;

  while (exit_sig.wait_for(std::chrono::seconds(0)) ==
         std::future_status::timeout) {
    struct pollfd async_fd;
    int ms_timeout = 100;

    async_fd.fd = ctx->async_fd;
    async_fd.events = POLLIN;
    async_fd.revents = 0;

    do {
      ret = poll(&async_fd, 1, ms_timeout);

      if (exit_sig.wait_for(std::chrono::seconds(0)) ==
          std::future_status::ready)
        return;
    } while (ret == 0);

    if (ret < 0) {
      logger->error("poll failed");
      return;
    }

    ret = ibv_get_async_event(ctx, &event);

    if (ret) {
      logger->error("Error, ibv_get_async_event() failed");
      return;
    }

    logger->warn("Got async event {}", event.event_type);

    // ack the event
    ibv_ack_async_event(&event);
  }
}

}  // namespace ctrl
}  // namespace dory
