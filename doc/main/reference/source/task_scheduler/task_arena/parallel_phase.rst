.. SPDX-FileCopyrightText: 2026 UXL Foundation Contributors
..
.. SPDX-License-Identifier: CC-BY-4.0

.. _parallel_phase_for_task_arena:

==============
parallel_phase
==============
**[scheduler.task_arena.parallel_phase]**

The API to mark the start and the end of a *parallel phase* in a ``task_arena``.
A parallel phase is a hint to the scheduler that a sequence of parallel work is about to be submitted
into the arena, so that worker threads can be retained in the arena until the phase ends.
See :ref:`Worker Thread Retention <worker_retention>` for the description of the concept.

A parallel phase is bound to a specific arena. It can be started and ended in two ways:

* with the RAII class ``task_arena::parallel_phase``, which starts the phase on construction
  and ends it on destruction.
* with the explicit ``start_parallel_phase`` and ``end_parallel_phase`` functions, available
  as members of ``task_arena`` for an explicit arena and in the ``this_task_arena`` namespace
  for the arena currently used by the calling thread.

**Start of a parallel phase** indicates a point from which the scheduler may retain worker threads
in the arena. If the arena is not initialized yet, starting a phase initializes it. For the calling thread's arena,
that means an implicit task arena is created and bound to the thread if there is none.

**End of a parallel phase** indicates a point from which the scheduler may stop retaining worker threads
in the arena.

Both the start and the end accept a ``parallel_phase::flags`` object. Only the flags applicable
to the given boundary are taken into account, the others are ignored.

.. caution::
    Every start of a parallel phase must have a matching end on the same arena, before the arena is destroyed
    (for an implicit arena, before the owning thread completes). Otherwise, the behavior is undefined.
    Ending a parallel phase that was not started also results in undefined behavior.

Synopsis
--------

.. code:: cpp

    // Defined in header <oneapi/tbb/task_arena.h>

    namespace oneapi {
        namespace tbb {

            class task_arena {
            public:
                class parallel_phase {
                public:
                    class flags {
                    public:
                        // Only participates in overload resolution if each type in Flags is a parallel phase flag
                        template <typename... Flags>
                        flags(Flags... f);

                        flags() = default;
                        flags(const flags&) = default;
                        flags(flags&&) = default;
                        flags& operator=(const flags&) = default;
                        flags& operator=(flags&&) = default;
                        ~flags() = default;
                    };
                    class end_flag_fast_leave;

                    parallel_phase(oneapi::tbb::attach, flags f = {});
                    parallel_phase(task_arena& ta, flags f = {});
                    parallel_phase(parallel_phase&& other);
                    parallel_phase& operator=(parallel_phase&& other);
                    ~parallel_phase();

                    void end();
                };

                void start_parallel_phase(parallel_phase::flags f = {});
                void end_parallel_phase(parallel_phase::flags f = {});
            }; // class task_arena

            namespace this_task_arena {
                void start_parallel_phase(task_arena::parallel_phase::flags f = {});
                void end_parallel_phase(task_arena::parallel_phase::flags f = {});
            } // namespace this_task_arena

        } // namespace tbb
    } // namespace oneapi

Member types
------------

.. cpp:class:: task_arena::parallel_phase

    The RAII class that maps a parallel phase to a code scope. The phase starts on construction
    and ends on destruction, unless ended explicitly with ``end()`` or transferred by a move operation.
    The class is not copyable.

.. cpp:class:: task_arena::parallel_phase::flags

    A set of flags that adjust the behavior of a parallel phase at its start or its end.
    Each flag is a distinct tag type. A flag passed to an operation it does not apply to is ignored.

.. cpp:class:: task_arena::parallel_phase::end_flag_fast_leave

    A parallel phase flag that applies to the end of a phase. When passed to the end of the last active
    phase in the arena, worker threads leave the arena as soon as possible, even if the arena was initialized
    with ``leave_policy::automatic``. If other phases are still active in the arena, the flag has no effect.

    .. note::
        The effect is temporary. Once new work is submitted into the arena while no parallel phase is active,
        the leave policy set at arena initialization applies again.

    .. note::
        For a ``task_arena`` initialized with ``leave_policy::fast``, this flag has no additional effect.

Member functions
----------------

.. cpp:function:: flags()

    Constructs an empty set of flags.

.. cpp:function:: template <typename... Flags> flags(Flags... f)

    Constructs a set from the given parallel phase flags. Participates in overload resolution
    only if each type in ``Flags`` is a parallel phase flag type.

.. cpp:function:: parallel_phase::parallel_phase(task_arena& ta, flags f = {})

    Starts a parallel phase in ``ta``. The corresponding flags from ``f`` are applied at the start and at the end of the phase.

.. cpp:function:: parallel_phase::parallel_phase(oneapi::tbb::attach, flags f = {})

    Starts a parallel phase in the arena currently used by the calling thread.
    The corresponding flags from ``f`` are applied at the start and at the end of the phase.

    .. caution::
        The phase must end (by ``end()``, destruction, or move assignment) on a thread that uses
        the same arena. Otherwise, the behavior is undefined.

.. cpp:function:: parallel_phase::parallel_phase(parallel_phase&& other)

    Transfers the ownership of the parallel phase from ``other``.

.. cpp:function:: parallel_phase& parallel_phase::operator=(parallel_phase&& other)

    Ends the parallel phase owned by ``this``, if any, and transfers the ownership of the phase from ``other``.

    **Returns**: a reference to ``this``.

.. cpp:function:: parallel_phase::~parallel_phase()

    Ends the owned parallel phase, if it has not been ended yet.

.. cpp:function:: void parallel_phase::end()

    Ends the owned parallel phase. Subsequent calls to ``end()`` and the destructor have no effect.

.. cpp:function:: void task_arena::start_parallel_phase(parallel_phase::flags f = {})

    Starts a parallel phase in the arena.

.. cpp:function:: void task_arena::end_parallel_phase(parallel_phase::flags f = {})

    Ends a parallel phase in the arena.

    .. caution::
        Ending a parallel phase that was not started results in undefined behavior.

Non-member functions
--------------------

.. cpp:function:: void this_task_arena::start_parallel_phase(task_arena::parallel_phase::flags f = {})

    Starts a parallel phase in the arena currently used by the calling thread.

.. cpp:function:: void this_task_arena::end_parallel_phase(task_arena::parallel_phase::flags f = {})

    Ends a parallel phase in the arena currently used by the calling thread.

    .. caution::
        The phase must have been started on a thread that uses the same arena. Otherwise, the behavior is undefined.

Example
-------

In this example, a ``global_control`` object sets fast leave as the application-wide default,
so worker threads are not expected to remain in ``ta`` once parallel work is completed.

However, one stage of the workflow is a sequence of parallel computations
interleaved with serial code. Bracketing it with ``parallel_phase`` hints the scheduler to keep
worker threads in ``ta`` between the computations. Outside the phase, the fast leave behavior applies again.

.. literalinclude:: ./examples/parallel_phase_example.cpp
   :language: c++
   :start-after: /*begin_parallel_phase_example*/
   :end-before: /*end_parallel_phase_example*/

See also:

* :doc:`Worker Thread Retention <../scheduling_controls/worker_retention>`
* :doc:`task_arena <task_arena_cls>`
* :doc:`this_task_arena namespace <this_task_arena_ns>`
* :doc:`global_control <../scheduling_controls/global_control_cls>`
