.. SPDX-FileCopyrightText: 2019-2021 Intel Corporation
..
.. SPDX-License-Identifier: CC-BY-4.0

=================
task_group_status
=================
**[scheduler.task_group_status]**

A ``task_group_status`` type represents the status of a ``task_group``.

.. code:: cpp
    // Defined in header <oneapi/tbb/task_group.h>
    // Defined in header <oneapi/tbb/task_arena.h>

    namespace oneapi {
    namespace tbb {
        enum task_group_status {
            not_complete,
            complete,
            canceled,
            task_completed // Preview feature: Waiting an Individual Task
        };
    } // namespace tbb
    } // namespace oneapi

Member constants
----------------

.. c:macro:: not_complete

    Not cancelled and not all tasks in a group have completed.

.. c:macro:: complete

    Not cancelled and all tasks in a group have completed.

.. c:macro:: canceled

    Task group received cancellation request.

Preview Features
----------------

:ref:`Waiting for Individual tasks<wait_single_task>` extends the ``task_group_status`` API
to allow waiting for an individual task to complete in ``task_group``.
