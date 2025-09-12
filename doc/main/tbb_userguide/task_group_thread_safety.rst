.. _task_group_thread_safety:

task_group Thread Safety
========================

The use of a shared ``task_group`` object across different threads is safe and easy to reason about
in many common cases, such as recursive algorithms. But there are some cases where it is difficult
to reason about the concurrent use of a single shared ``task_group`` across threads.

In :ref:`creating_tasks_with_task_group`, tasks that are executing within a ``task_group`` add additional tasks
by calling ``run`` on the same ``task_group`` object. The call to ``task_group::wait`` is then made
from the thread that started the recursive parallel algorithm. The calls to ``run`` are logically nested
within a recursive algorithm and that single call to ``wait`` from the main thread is guaranteed to wait
for all of the children tasks, even those added from a worker thread.

Generally, calls to ``task_group::run`` and ``task_group::wait`` on the same object from different threads
do not offer simple guarantees. Here is a diagram that shows a single ``task_group``
object that is accessed in parallel by three different threads. Each thread runs some tasks and then calls wait
on the shared ``task_group``:


.. container:: fignone
   :name: conurrent_tasks


   .. container:: imagecenter


      |image0|


If none of the tasks run in the ``task_group`` throw an exception or cancel the execution of the
``task_group``, there two execution guarantees. 

First, all tasks created by calls to ``run`` that *happen before* a call to ``wait`` on the same thread 
are guaranteed to be complete when the call to wait returns. So for example, the thread that runs the
`A` tasks is guaranteed to wait for all the `A` tasks in its call to ``wait``. 

Second, any ``run`` that *inter-thread happens before* a call to ``wait`` on another thread will be complete
when that call to ``wait`` returns.

Both of these guarantees mean that if you use C++ mechanism to order the calls to ``run``
and ``wait`` on the same ``task_group``, this ordering will be respected. But if you do not
enforce an ordering, then there can be races between when a task is ``run`` in a ``task_group`` and
the ``wait`` on that ``task_group`` in another thread.

Use of cancelation or exceptions complicates the semantics of concurrent calls to ``wait``
on the same ``task_group`` object. The ``task_group::wait`` function resets the ``task_group_context`` associated
with the ``task_group``. Cancellations or exceptions combined with concurrent calls to ``wait`` on a shared
``task_group`` therefore result in undefined behavior. 

The following diagram shows three threads that call ``run``, ``wait`` and ``cancel`` on a
shared ``task_group``.  The execution guarantees described above no longer hold for this example.

.. container:: fignone
   :name: conurrent_tasks_canceled


   .. container:: imagecenter


      |image1|

.. |image0| image:: Images/concurrent_tasks.png
   :width: 600px
.. |image1| image:: Images/concurrent_tasks_canceled.png
   :width: 600px

In this diagram, there is no guarantee that all of the `A` tasks complete, since an intervening call
to ``cancel`` on another thread may cancel their execution. For the thread that runs the `B` tasks,
a ``wait`` on another thread, which results in a reset of the ``task_group_context``, may cause the call
to ``wait`` on thread `B` to return a status other than ``canceled``. Similarly, a thread that has canceled
a ``task_group`` and then ``runs`` more tasks may see those tasks execute because a ``wait`` on another
thread completed and reset the ``task_group`` before those tasks were ``run``, effectively uncanceling
the ``task_group``.  Exceptions cause similar issues.

Due to the lack of meaningful guarantees for cancelation and exception handling in these situations,
we recommend that concurrent calls to ``wait`` be used only in cases where there is no possibility of
concurrent cancelations or exceptions.
