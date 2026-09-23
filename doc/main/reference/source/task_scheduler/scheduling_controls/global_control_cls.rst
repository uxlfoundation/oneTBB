.. SPDX-FileCopyrightText: 2019-2021 Intel Corporation
..
.. SPDX-License-Identifier: CC-BY-4.0

.. _global_control_cls:

==============
global_control
==============
**[scheduler.global_control]**

Use this class to control certain settings or behavior of the oneTBB dynamic library.

An object of class ``global_control``, or a "control variable", affects one of several behavioral aspects, or parameters, of TBB.
The ``global_control`` class is primarily intended for use at the application level, to control the whole application behavior.

The current set of parameters that you can modify is defined by the ``global_control::parameter`` enumeration.
The parameter and the value it should take are specified as arguments to the constructor of a control variable.
The impact of the control variable ends when its lifetime is complete.

Control variables can be created in different threads, and may have nested or overlapping scopes.
However, at any point in time each controlled parameter has a single active value that applies to the whole process.
This value is selected from all currently existing control variables by applying a parameter-specific selection rule.

.. code:: cpp

    // Defined in header <oneapi/tbb/global_control.h>

    namespace oneapi {
    namespace tbb {
        class global_control {
        public:
            enum parameter {
                max_allowed_parallelism,
                thread_stack_size,
                terminate_on_exception,
                leave_policy
            };

            global_control(parameter p, size_t value);
            // Available only when T is an enumeration type
            template <typename T>
            global_control(parameter p, T value);
            ~global_control();

            static size_t active_value(parameter param);
        };
    } // namespace tbb
    } // namespace oneapi

Member types and constants
--------------------------

.. cpp:enum:: parameter::max_allowed_parallelism

    **Selection rule**: minimum

    Limits total number of worker threads that can be active in the task scheduler to ``parameter_value - 1``.

    .. note::

        With ``max_allowed_parallelism`` set to ``1``, ``global_control`` enforces serial execution
        of all tasks by the application thread(s), that is, the task scheduler does not allow worker threads to run.
        There is one exception: if some work is submitted for execution via ``task_arena::enqueue``,
        a single worker thread will still run ignoring the ``max_allowed_parallelism`` restriction.

.. cpp:enum:: parameter::thread_stack_size

    **Selection rule**: maximum

    Set stack size for working threads created by the library.

.. cpp:enum:: parameter::terminate_on_exception

    **Selection rule**: logical disjunction

    Setting the parameter to 1 causes termination in any condition that would throw or rethrow an exception.
    If set to 0 (default), the parameter does not affect the implementation behavior.

.. cpp:enum:: parameter::leave_policy

    **Selection rule**: the first active request for ``task_arena::leave_policy::fast`` determines
    the active value and stays in effect until the corresponding object is destroyed. The effect
    of other ``leave_policy`` requests made while it is active is unspecified.

    Sets the application-wide default for how quickly worker threads leave an arena when there is no more
    work available. The value must be one of the ``task_arena::leave_policy`` enumerators.

    See :ref:`Worker Thread Retention <worker_retention>` for the description of leave policies
    and how this parameter interacts with the per-arena ``task_arena::leave_policy`` setting.


Member functions
----------------

.. cpp:function:: global_control(parameter param, size_t value)

    Constructs a ``global_control`` object with a specified control parameter and its value.

.. cpp:function:: template <typename T> global_control(parameter param, T value)

    Constructs a ``global_control`` object with a specified control parameter and its value
    given as an enumerator. Participates in overload resolution only if ``T`` is an enumeration type.
    The behavior is equivalent to ``global_control(param, static_cast<size_t>(value))``.

    Currently, this constructor is intended for the ``leave_policy`` parameter, which accepts
    ``task_arena::leave_policy`` enumerators.

.. cpp:function:: ~global_control()

    Destructs a control variable object and ends it's impact.

.. cpp:function:: static size_t active_value(parameter param)

    Returns the currently active value of the setting defined by ``param``.

See also:

* :doc:`task_arena <../task_arena/task_arena_cls>`
* :doc:`Worker Thread Retention <worker_retention>`
