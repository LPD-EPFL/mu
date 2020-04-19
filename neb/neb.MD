# NEB Pseudocode

## Buffers at process p

- `bcast_buf[q]`, for q in P - one broadcast buffer per process
  - process q is the only remote who can access (RW) it
- `replay_buf` - one replay buffer to replay values written to the broadcast buffers
  - all remotes can access (R) it

## Code for process `p` to broadcast message `m`

```python
broadcast(k, m):
    # local write - only used to reference the buffer in WR when posting to write to remote broadcast buffers
    write(p, bcast_buf[p], m, k, own_next)
    for i in P \ {self}:
        # remote write over ibv_post_send
        post_write(i, bcast_buf[p], m, k, own_next)

    deliver(k, m, p)
    own_next += 1
```

## Code for process `p` to deliver message `m` from `q`

```python
# executed infinitely in own thread
poll_bcast_bufs:
    for i in P \ {self}:
        n = next[i]
        (k, val) = bcast_buf[i][n]

        if (k == \bot || key != n)
            continue

        # write to the replay buffer where others can read from
        copy(replay_buf[i, n], (k, val))

        # there is nobody left to read replayed values from
        if (size(P \ {self, i}) == 0)
            deliver(k, m, i)
        else
            for q in P \ {self, i}:
                # remote read via a ibv_post_send
                post_read(q, replay_buf[i, n])

        next[i]++

# executed infinitely in own thread
poll_read_cq:
    for wc in WCs:
        msg_id, origin, replayer = unpack(wc.id)
        # We store the number of not conflicting reads so we know when we can deliver
        tracker = read_tracker[origin, msg_id]

        if (wc.status == success)
            # loads the value written by the RNIC during remote read
            (k, val) = read(origin, replayer, msg_id)

            # nothing is written, so no conflict
            if ((k, val) == \bot)
                tracker += 1

            # values do match
            if ((k, val) == replay_buf[origin, msg_id])
                tracker += 1

        # the remote is gone
        if(wc.status == IBV_WC_RETRY_EXC_ERR)
            # We remove the replayer process as he's not participating anymore
            P = P \ {replayer}
            # Also we deliver all messages that are in the `read_tracker`
            # map for which we only waited for the read WC from `remote` (that is now gone).
            # Otherwise those messages will never get delivered.


        # We ignore the origin and ourselves from replay reading, therefore #(P) - 2
        if(tracker >= size(P) - 2)
            deliver(k, replay_buf[origin, msg_id], origin)
```
