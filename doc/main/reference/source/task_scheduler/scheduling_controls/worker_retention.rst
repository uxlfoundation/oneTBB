.. SPDX-FileCopyrightText: 2026 UXL Foundation Contributors
..
.. SPDX-License-Identifier: CC-BY-4.0

.. _worker_retention:

=======================
Worker Thread Retention
=======================
**[scheduler.worker_retention]**

Worker thread retention determines what happens to worker threads participating in work execution
inside a task arena once no more work is available in it: whether they stay for a while, anticipating
new work, or leave promptly to become available for other arenas or other threads in the system.


By default, oneTBB uses a system-specific thread leave heuristic: after completing work in an arena,
worker threads might remain for an unspecified duration, anticipating that new parallel
work will arrive soon. This benefits most workloads by reducing the latency of starting
subsequent parallel computations. However, this behavior can be undesirable, especially if

* parallel tasks are submitted at irregular intervals or with long gaps, and idle threads waste CPU resources.
  For instance, threads that spin while waiting for work increase the CPU load and may cause
  the CPU frequency to drop for the serial parts of the application.
* oneTBB use is interleaved with another threading runtime, and idle threads cause CPU oversubscription.

oneTBB provides two complementary mechanisms to control worker thread retention:

* A *leave policy* sets how fast worker threads leave an arena when no work is available.
  It is set when an arena is initialized, either per arena or application-wide.
* A *parallel phase* brackets a region of recurrent parallel work so that the scheduler
  can retain threads more aggressively during the region and, if needed, releases them promptly afterward.
  It changes retention behavior of an already initialized arena at runtime.

Leave Policy
------------

A leave policy defines the behavior of a worker thread at the moment it finds no more work in the arena.
There are two policies, represented by the ``task_arena::leave_policy`` enumeration:

``automatic``
    The default policy. The worker thread might stay in the arena for an unspecified duration,
    anticipating that new work arrives soon. While staying, it may spin or yield, so it consumes CPU
    resources. If work arrives in time, the thread starts executing it with a minimal delay. Otherwise,
    it leaves the arena. The exact retention heuristic is system-specific and may differ between
    platforms.

``fast``
    The worker thread leaves the arena as soon as it finds no more work. This releases CPU resources
    promptly, at the cost of a higher latency for the next parallel computation in the arena,
    since worker threads have to be brought back into the arena.

The leave policy is fixed for an arena at initialization and cannot be changed afterward.
It can be specified in two ways:

* **Per arena**, with the ``leave_policy`` argument of ``task_arena`` constructors and
  ``task_arena::initialize`` methods. See :ref:`task_arena <task_arena_cls>`.

* **Application-wide**, with the ``global_control::leave_policy`` parameter.
  See :ref:`global_control <global_control_cls>` for the rules of determining the active value
  when multiple ``global_control`` objects exist.

The two settings interact as follows. The active value of ``global_control::leave_policy`` is consulted
when an arena is initialized:

* An arena initialized with an explicit ``leave_policy::fast`` always uses fast leave.
* An arena initialized with ``leave_policy::automatic``, including an implicitly initialized arena,
  uses fast leave if the active value is ``fast`` at that moment, and the default policy otherwise.

Since the leave policy is fixed at initialization, arenas initialized before a ``global_control`` object
is created, or after it is destroyed, are not affected by it.

Parallel Phase
--------------

A parallel phase is a hint to the scheduler that a sequence of parallel computations,
possibly interleaved with serial code, is about to be submitted into an arena.
Once the phase begins, the scheduler may apply a more aggressive policy
to retain worker threads in the arena than the leave policy the arena was initialized with.

When the phase ends, the scheduler may drop the hint and no longer retain threads. Optionally, a phase can end
with a *fast leave* request, so that worker threads leave the arena promptly even if it
was initialized with ``leave_policy::automatic``. The request is one-time: it does not change
the leave policy of the arena.

Parallel phases can be nested or overlap and the arena stays in a phase until all started phases have ended.

The parallel phase API is provided by the ``task_arena::parallel_phase`` class and by the
``start_parallel_phase`` and ``end_parallel_phase`` functions of ``task_arena`` and
``this_task_arena``. See :ref:`parallel_phase <parallel_phase_for_task_arena>` for details.

Examples
--------

In the following example, a stage of parallel computation in a ``task_arena`` is followed by a stage
that uses a different threading runtime. The arena is initialized with ``leave_policy::fast``, so its
worker threads do not spin when idle to not compete for CPU with the threads of the other runtime.

.. literalinclude:: ./examples/leave_policy_example.cpp
   :language: c++
   :start-after: /*begin_arena_leave_policy_example*/
   :end-before: /*end_arena_leave_policy_example*/

The next example has the same structure, but the parallel algorithm is called directly,
without an explicit arena, so there is no place to set the leave policy per arena. Instead,
a ``global_control`` object switches the application-wide default to fast leave, which also applies
to the implicit arena used by the algorithm.

.. literalinclude:: ./examples/leave_policy_example.cpp
   :language: c++
   :start-after: /*begin_global_leave_policy_example*/
   :end-before: /*end_global_leave_policy_example*/

See also:

* :doc:`task_arena <../task_arena/task_arena_cls>`
* :doc:`global_control <global_control_cls>`
* :doc:`parallel_phase <../task_arena/parallel_phase>`
