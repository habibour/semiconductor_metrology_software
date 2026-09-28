#Architecture notes

This file holds the things CLAUDE.md §6.2(concurrency rules) and PRD C4 / NFR - CON -
                                                                     1 ask to have written down
                                                                     separately from the code
    : the lock hierarchy actually used,
    not an aspirational one.It reflects the code as of the Day 6 refactor and perf commits; re-check it whenever a new mutex is
added.

## Lock hierarchy

There isn't one, in the sense of "lock A, then lock B" anywhere in this
codebase. Every mutex below guards one small piece of state local to its own
class, is held only inside that class's own methods, and is released before
calling anything outside the class (an event-bus publish, a subscriber
handler, another object's method). No two of these mutexes are ever held by
the same thread at the same time. This was verified by reading every
`std::mutex` declaration in `src/` and `include/ssim/` (nine of them,
listed below) and every place it is taken.

| Mutex | Class | Guards | Touched from |
|---|---|---|---|
| `EventBus::mutex_` | `EventBus` | the subscriber lists | any thread that calls `subscribe`/`unsubscribe`/`publish` |
| `AlarmManager::mutex_` | `AlarmManager` | the active-alarm set | controller, processing thread |
| `Controller::wafer_id_mutex_` | `Controller` | `current_wafer_id_` | controller thread (write), any thread via `current_wafer_id()` (read) |
| `MachineApi::result_mutex_` | `MachineApi` | `last_result_` | the `WaferResultReady` handler (event-bus dispatch thread) writes, callers of `snapshot()` read |
| `MachineRuntime::dir_mutex_` | `MachineRuntime` | `last_wafer_dir_` | processing thread (write), any thread via `last_wafer_dir()` (read) |
| `BoundedQueue::mutex_` (v1) | `core::BoundedQueue<T>` | the deque and `closed_`/`dropped_` | any producer/consumer thread |
| `SpscRingQueue::mutex_` (v2) | `core::SpscRingQueue<T>` | only the parked-wait handshake (the ring itself is lock-free — see below) | the one producer thread and the one consumer thread |
| `FitThreadPool::for_each_index`'s `done_mutex` | local to the call, not a class member | the `remaining` countdown | the calling thread and the pool's worker threads, for the duration of one call |
| `EventBridge::log_mutex_` (Qt panel) | `EventBridge` | the pending log-line buffer | the logger's log thread (write), the Qt GUI thread's poll timer (read) |

Rule actually enforced: **at most one lock at a time, and never across a
call to code the class doesn't own.** Every one of the nine mutexes above
follows the same shape:

```cpp
{
    std::lock_guard lock(mutex_);
    // touch only this class's own fields
}
// lock released here, only then: bus_.publish(...), a subscriber callback,
// another object's method, etc.
```

`AlarmManager::set`/`clear` and `Controller::submit`'s Start path are the
clearest examples: both copy or compute what they need inside the lock,
release it, and only then call `bus_.publish(...)`. `EventBus::publish_erased`
copies the subscriber list under its lock, releases the lock, and only then
invokes each handler (see the comment at `src/core/event_bus.cpp:66`, "the
bus lock is released before any subscriber code runs (rule C3)"). Because no
class ever calls into another class while holding its own lock, there is
nothing that could form a cycle, so a classic lock-ordering deadlock is not
possible in this codebase today. If a second lock is ever added to a class
that already takes one, this table must grow a real ordering rule (documented
here, per CLAUDE.md §6.2) before that code ships.

## Thread confinement instead of locking

Two parts of the system avoid locks entirely by confining mutable state to
one thread and handing work to it instead of sharing the state:

- **HSMS/GEM.** `HsmsServer` owns its Asio `io_context` on the `hsms_io`
  thread; `Session` and `GemService` state is only ever touched on that
  thread. Any other thread that needs to affect it (send a reply, post an
  event) calls `IMessageSender::post_task`, which is `asio::post(io, ...)`
  (`src/secsgem/hsms/server.cpp`) — the work runs later, on the I/O thread,
  never immediately on the caller's thread. No mutex is declared anywhere in
  `src/secsgem/` or `include/ssim/secsgem/`.
- **`SpscRingQueue`'s fast path.** The ring buffer itself (head/tail indices,
  slot storage) is lock-free: the producer only ever writes `head_`, the
  consumer only ever writes `tail_`, and each caches the other's index to
  avoid a cross-core read on every call. The mutex it does declare is only
  the parking mechanism for the (rare) case where the ring is full or empty;
  see the memory-order comments in
  `include/ssim/core/spsc_ring_queue.hpp` for why each atomic operation uses
  the ordering it uses.

## Where each queue sits

Matches CLAUDE.md §3.3/3.4. `SampleQueue` (`ssim::core::SpscRingQueue<SampleBlock>`,
the v2 ring, Day 6) carries scan blocks from the scan thread (sole producer)
to the processing thread (sole consumer). `MachineRuntime::jobs_`
(`BoundedQueue<WorkItem>`, v1) carries "a wafer's scan finished, run the
pipeline" and "discard what's left in the sample queue" from the scan
thread's two callbacks to the processing thread. `FitThreadPool` uses its own
`BoundedQueue<std::function<void()>>` as its job queue. All three are bounded
with a documented back-pressure policy at their construction site, per
CLAUDE.md §6.2.

## Shutdown order

`MachineRuntime::shutdown()` (`src/machine/machine_runtime.cpp`): stop the
comm link first (no more commands can arrive), request the scan thread to
abort, close `jobs_` and `sample_queue_` (wakes any blocked push/pop), join
the processing thread, stop the controller, drop the GEM service, stop the
logger. This is the reverse of construction order and matches the shutdown
protocol in CLAUDE.md §6.2 (request stop, wake all waiters, join in reverse
dependency order, flush logs), and is exercised by
`MachineRuntimeTest.DestroyingWhileScanningFinishesWithinTwoSeconds`.
