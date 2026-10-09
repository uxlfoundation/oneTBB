# oneTBB Tasking API Evolution

This document tracks the planned and proposed directions for evolving the oneTBB tasking API,
built around the task dependency and completion-transfer primitives introduced by the
"Task Group Dynamic Dependencies" preview feature. Each direction is intended to be developed
into a separate RFC.

## Decouple Task Dependency APIs from `task_group`
Move `set_task_order` and `transfer_completion_to` out of `task_group` and into the `tbb::task`
namespace as free functions, before the preview feature is promoted to production.
See: [Decoupling Task Dependency APIs from `task_group`](move_to_namespace_task_1.md)

## `task_group` Enhancements
Improve the ergonomics of the `task_group` API.
Key ideas: `task_group::run` returning a `task_completion_handle`, and a `task_group::run_after`
method combining the defer/order/run sequence into a single call.

## Non-blocking Parallel Algorithms
Add variants of TBB parallel algorithms that return a `task_completion_handle` instead of blocking.

## Embeddable Task Graphs
Allow manually constructed task graphs to be embedded into the current execution context
on the fly, without an explicit `task_group`.

## Integration with Asynchronous C++ Execution Models
Explore using the task dependency API as a foundation for integration with C++ coroutines
and the sender/receiver model of `std::execution`, such as a TBB-backed coroutine awaiter
or exposing TBB algorithms as senders.
