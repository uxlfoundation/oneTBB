.. _collaborative_call_once

collaborative_call_once
=======================
**[algorithms.collaborative_call_once]**

Function template that executes a function exactly once.

.. code:: cpp
    // Defined in header <oneapi/tbb/collaborative_call_once.h>

    namespace oneapi {
        namespace tbb {

            template<typename Func, typename... Args>
            void collaborative_call_once(collaborative_once_flag& flag, Func&& func, Args&&... args);

        } // namespace tbb
    } // namespace oneapi

Requirements:

* The ``Func`` type must meet the `Function Objects` 
  requirements described in the [function.objects] section of the ISO C++ standard.

Executes ``func(std::forward<Args>(args)...)`` only once, even when called concurrently.
Other threads blocked on the same ``collaborative_once_flag`` can join the |short_name|
parallel construction invoked during execution of ``func``.

If an exception is thrown, the thread executing ``func`` 
receives it. One of the threads blocked on the same ``collaborative_once_flag``
calls the ``Func`` object again. 

collaborative_once_flag Class
-----------------------------

.. toctree::
    :titlesonly:

    collaborative_once_flag_cls.rst

Example
-------

The following example shows a class in which the "Lazy initialization" pattern is implemented for 
the ``cachedProperty`` field.

.. literalinclude:: ./examples/collaborative_call_once.cpp
    :language: c++
    :start-after: /*begin_collaborative_call_once_example*/
    :end-before: /*end_collaborative_call_once_example*/
