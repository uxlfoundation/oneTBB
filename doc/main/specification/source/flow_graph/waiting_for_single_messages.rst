.. _waiting_for_single_message_in_flow_graph:

Waiting for Single Message in Flow Graph (preview)
==================================================
**[flow_graph.waiting_for_single_message]**

.. note::
    To enable this feature, define the ``TBB_PREVIEW_FLOW_GRAPH_TRY_PUT_AND_WAIT`` or the ``TBB_PREVIEW_FLOW_GRAPH_FEATURES`` macro to ``1``.

This feature adds a new ``try_put_and_wait`` interface to the receiving nodes in the Flow Graph.
This function puts a message as an input into a Flow Graph and waits until all work related to that
message is complete.

``try_put_and_wait`` may reduce latency compared to calling ``graph::wait_for_all`` since
``graph::wait_for_all`` waits for all work, including work that is unrelated to the input message, to complete.

``node.try_put_and_wait(msg)`` performs ``node.try_put(msg)`` on the node and waits until the work on ``msg`` is completed.
It means the following conditions are true:

* Any task initiated by any node in the Flow Graph that involves working with ``msg`` or any other intermediate result
  computed from ``msg`` is completed.
* No intermediate results computed from ``msg`` remain in any buffers in the graph.

.. caution::

    To prevent ``try_put_and_wait`` calls from infinite waiting, avoid using buffering nodes at the end of the Flow Graph since
    the final result will not be automatically consumed by the Flow Graph.

.. caution::

    The ``multifunction_node`` and ``async_node`` classes are not currently supported by this feature. Including one of these nodes in the
    Flow Graph may cause ``try_put_and_wait`` to exit early, even if the computations on the initial input message are still in progress.

.. code:: cpp

    namespace oneapi {
    namespace tbb {
    namespace flow {

        template< typename Output, typename Policy >
        class continue_node {
        public:
            bool try_put_and_wait(const continue_msg& input);
        };

        template < typename Input, typename Output, typename Policy >
        class function_node {
        public:
            bool try_put_and_wait(const Input& input);
        };

        template < typename T >
        class overwrite_node {
        public:
            bool try_put_and_wait(const T& input);
        };

        template < typename T >
        class write_once_node {
        public:
            bool try_put_and_wait(const T& input);
        };

        template < typename T >
        class buffer_node {
        public:
            bool try_put_and_wait(const T& input);
        };

        template < typename T >
        class queue_node {
        public:
            bool try_put_and_wait(const T& input);
        };

        template < typename T, typename Compare >
        class priority_queue_node {
        public:
            bool try_put_and_wait(const T& input);
        };

        template< typename T >
        class sequencer_node {
        public:
            bool try_put_and_wait(const T& input);
        };

        template< typename T, typename DecrementType >
        class limiter_node {
        public:
            bool try_put_and_wait(const T& input);
        };

        template < typename T >
        class broadcast_node {
        public:
            bool try_put_and_wait(const T& input);
        };

        template < typename TupleType >
        class split_node {
        public:
            bool try_put_and_wait(const TupleType& input);
        };

    } // namespace flow
    } // namespace tbb
    } // namespace oneapi

Example
-------

.. literalinclude:: ./examples/try_put_and_wait_example.cpp
    :language: c++
    :start-after: /*begin_try_put_and_wait_example*/
    :end-before: /*end_try_put_and_wait_example*/

Each iteration of ``parallel_for`` submits an input into the Flow Graph. After returning from ``try_put_and_wait(input)``, it is
guaranteed that all of the work related to the completion of ``input`` is done by all of the nodes in the graph. Tasks related to inputs
submitted by other calls are not guaranteed to be completed.

.. rubric:: See Also

    :ref:`Preview Features<preview_features>`
