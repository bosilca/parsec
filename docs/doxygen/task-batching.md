<!--
Copyright (c) 2026 NVIDIA Corporation.  All rights reserved.
-->

Task Batching {#task_batching}
==============

Task batching lets a device submit hook combine several compatible ready tasks
into one device operation. The runtime still owns dependency management, data
movement, completion, and release; the submit hook only decides which pending
tasks are compatible with the task it was asked to submit.

Batching is opt-in at the incarnation level and in the device submit hook. A
batch-capable incarnation may call `parsec_gpu_task_collect_batch()` from its
submit hook; a hook that does not call the collector always submits the
singleton task it was given.

Enabling batching
-----------------

For PTG-generated tasks, mark the body with `batch = true` and call the
collector from the device body that can use a batch. The `batch` property marks
that generated incarnation as batch-capable; it does not force the runtime to
batch tasks.

```c
BODY [type=CUDA
      batch = true
      dyld=cublasDgemm dyldtype=cublas_dgemm_t]
{
    int nb_batched = parsec_gpu_task_collect_batch(gpu_stream, gpu_task,
                                                   gemm_batch_match, NULL);
    if( nb_batched < 0 ) {
        return nb_batched;
    }

    /* Submit gpu_task, whose ring may now contain 1 + nb_batched tasks. */
}
```

For DTD tasks, add `PARSEC_DEV_CHORE_ALLOW_BATCH` to the device type when
registering the chore that can batch, then use the same collection approach
inside the registered device hook:

```c
parsec_dtd_task_class_add_chore(tp, tc,
                                PARSEC_DEV_CUDA | PARSEC_DEV_CHORE_ALLOW_BATCH,
                                kernel_cuda);
```

The head task's selected device type must support batching at runtime, and the
selected incarnation of the task passed to the submit hook must be marked
batch-capable. The collector checks these conditions before iterating over
pending work. If either condition fails, it leaves `gpu_task` as a singleton and
returns 0. While scanning the stream, it skips pending tasks whose selected
incarnation is not batch-capable on that same selected device before calling the
user callback.

The MCA parameter `device_enable_batching` defaults to the compile-time batching
capability and can be used to disable batching globally at runtime. It is
read-only when batching support is not compiled in.

Recommended collection helper
-----------------------------

The preferred interface for GPU submit hooks is
`parsec_gpu_task_collect_batch()`. The runtime passes the submit hook a
singleton `parsec_gpu_task_t *gpu_task` on its initial invocation. The hook
calls the collector with a callback that decides, for each task currently
pending on the same stream, whether that candidate can be added to the batch
headed by `gpu_task`. After a batched hook returns `PARSEC_HOOK_RETURN_AGAIN`,
the continuation invocation receives the same intact ring. Calling the
collector again is safe: it returns the existing follower count without
scanning or modifying the pending FIFO.

The callback has the type `parsec_gpu_task_batch_cb_t` and receives:

- `candidate`: a pending task from `gpu_stream->fifo_pending`;
- `batch_head`: the task originally passed to the submit hook;
- `callback_data`: user data passed through by the caller.

The callback return value controls the iterator:

- `PARSEC_GPU_TASK_BATCH_ACCEPT`: remove `candidate` from the pending FIFO and
  append it to `batch_head`'s task ring;
- `PARSEC_GPU_TASK_BATCH_REJECT`: leave `candidate` pending and continue to the
  next pending task;
- `PARSEC_GPU_TASK_BATCH_STOP`: end collection successfully, leaving the
  current candidate and all unvisited candidates pending.

The collector does not impose a scan or batch-size bound. The submit hook owns
that policy and its callback should return `PARSEC_GPU_TASK_BATCH_STOP` once it
has accepted enough work. The callback executes while
`gpu_stream->fifo_pending` is locked, so it must remain short and nonblocking.
It must not modify or acquire that FIFO, call the collector recursively, or
otherwise reenter pending-task operations on the same stream.

Example:

```c
static int
gemm_batch_match(parsec_gpu_task_t *candidate,
                 parsec_gpu_task_t *batch_head,
                 void *callback_data)
{
    (void)callback_data;

    if( (batch_head->ec->task_class == candidate->ec->task_class) &&
        (batch_head->ec->selected_chore == candidate->ec->selected_chore) &&
        (batch_head->ec->selected_device == candidate->ec->selected_device) ) {
        return PARSEC_GPU_TASK_BATCH_ACCEPT;
    }
    return PARSEC_GPU_TASK_BATCH_REJECT;
}

int
gemm_kernel_cuda(parsec_device_gpu_module_t *gpu_device,
                 parsec_gpu_task_t *gpu_task,
                 parsec_gpu_exec_stream_t *gpu_stream)
{
    int nb_batched;
    parsec_gpu_task_t *current;

    (void)gpu_device;

    nb_batched = parsec_gpu_task_collect_batch(gpu_stream, gpu_task,
                                               gemm_batch_match, NULL);
    if( nb_batched < 0 ) {
        return nb_batched;
    }

    current = gpu_task;
    do {
        parsec_task_t *task = current->ec;

        /* Submit one device operation for task, or use the whole ring to
         * issue a real batched operation.
         */

        current = (parsec_gpu_task_t *)current->list_item.list_next;
    } while( current != gpu_task );

    return PARSEC_HOOK_RETURN_DONE;
}
```

`parsec_gpu_task_collect_batch()` returns the number of additional tasks
appended to the ring on success, including when the callback stops collection.
A return value of 0 means no task was batched, either because the callback
stopped before accepting one, no compatible pending task was found, batching
is disabled or unsupported by the head task's selected device, or the head
task's selected incarnation is not batch-capable. Any callback value outside
the three actions above returns `PARSEC_HOOK_RETURN_ERROR`. Tasks accepted
before that error remain attached to `gpu_task`; tasks not accepted remain in
`gpu_stream->fifo_pending`.

The submit hook does not need a completion callback merely to return the ring to
the runtime. If a batched submit hook returns a non-singleton task ring, the GPU
progress engine automatically chains that ring into the next stream's pending
FIFO after the recorded device event completes. The normal data retrieval,
epilog, ownership, pushout, and task completion paths then process the tasks one
at a time. Every stream insertion merges all ring members according to the
stream's priority policy; equal-priority members retain their ring order.

The submit result settles ownership of the complete ring:

- `PARSEC_HOOK_RETURN_DONE` commits every member to the submitted device work;
- `PARSEC_HOOK_RETURN_AGAIN` commits the ring as continuation state, waits for
  work queued by the hook, and re-enters the hook with the same ring after the
  stream event completes. The hook must have submitted progress for every ring
  member before returning `AGAIN`;
- `PARSEC_HOOK_RETURN_NEXT` immediately restores the followers and applies the
  existing singleton `NEXT` path to the head;
- `PARSEC_HOOK_RETURN_ASYNC` transfers every execution context in the ring to
  the hook, which must eventually complete or reschedule each context. The GPU
  engine releases all corresponding device-task wrappers;
- `PARSEC_HOOK_RETURN_ERROR` and `PARSEC_HOOK_RETURN_DISABLE` quiesce the
  affected execution stream and clean every member of the failed batch before
  propagating the terminal status.

Device-wide recovery after `PARSEC_HOOK_RETURN_DISABLE` is not implemented.
The terminal path settles the batch that observed the error, but it does not
drain other stream FIFOs, event slots, or the device-wide pending queue.

Because `AGAIN` preserves a collected ring, hooks must check predictable
resource constraints before calling `parsec_gpu_task_collect_batch()`. Returning
`AGAIN` before collection is a normal singleton retry; returning it afterward
means that the collected batch has started and must continue as one unit.

The runtime never disbands a ring on the `AGAIN` path. A submit hook that no
longer wants to continue the batch must split the ring itself before returning:
detach the followers, restore the head as a valid singleton, and explicitly
give every detached wrapper a new owner. For example, followers may be returned
to `gpu_stream->fifo_pending` using that stream's priority-preserving insertion
policy, or the hook may explicitly retain responsibility for completing the
execution contexts and releasing their wrappers. Merely breaking the links is
insufficient because it leaves the followers unreachable. If the hook returns
`AGAIN` after disbanding, only the ring or singleton it leaves attached to
`gpu_task` is retained by the event.

Iterating over the returned ring
--------------------------------

A batched submit hook should treat `gpu_task` as the head of a circular task
ring. This works for both singleton and batched cases:

```c
parsec_gpu_task_t *current = gpu_task;

do {
    parsec_task_t *task = current->ec;

    /* Use task. */

    current = (parsec_gpu_task_t *)current->list_item.list_next;
} while( current != gpu_task );
```

Original direct collection style
--------------------------------

The helper above is intentionally conservative: it keeps FIFO ownership inside
the device layer and exposes only a compatibility callback to the submit hook.
In very high load scenarios, the repeated callback call can become visible. A
specialized submit hook can still use the original direct style and manipulate
the pending FIFO and task ring itself.

This style is more fragile and should be reserved for code that is already
device-runtime aware. The hook must preserve FIFO correctness, keep rejected
tasks pending, and unlock the FIFO on every exit path.

```c
parsec_list_t *pending = gpu_stream->fifo_pending;
parsec_list_item_t *item;
parsec_list_item_t *next;
int batch_count = 1;

PARSEC_LIST_ITEM_SINGLETON(&gpu_task->list_item);

parsec_list_lock(pending);
for(item = (parsec_list_item_t *)pending->ghost_element.list_next;
    item != &pending->ghost_element;
    item = next) {
    parsec_gpu_task_t *candidate;

    next = (parsec_list_item_t *)item->list_next;
    candidate = (parsec_gpu_task_t *)item;

    if( compatible_with_batch(candidate, gpu_task) ) {
        (void)parsec_list_nolock_remove(pending, item);
        (void)parsec_list_item_ring_push(&gpu_task->list_item, item);
        batch_count++;
    }
}
parsec_list_unlock(pending);
```

The direct style avoids the generic iterator and callback dispatch, and it can
fold the compatibility test into a tight kernel-specific loop. The cost is that
the submit hook now depends on internal list and stream details and must be
updated if the GPU stream internals change.
