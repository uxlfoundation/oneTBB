.. _join_node_type_specified_message_keys::

Type-Specified Message Keys for ``join_node`` (preview)
=======================================================
**[flow_graph.join_node.type_specified_message_keys]**

.. note::
    To enable this feature, define the ``TBB_PREVIEW_FLOW_GRAPH_FEATURES`` macro to ``1``.

This extension lets you specify key extraction per input port using type-associated functions,
without writing explicit key extractor functions. Each port's key is extracted from its message
type either via a built-in ``key()`` method or a custom ``key_from_message`` function found via
argument-dependent lookup.

.. code:: cpp
    // Defined in header <oneapi/tbb/flow_graph.h>

    namespace oneapi {
    namespace tbb {
    namespace flow {

        template<typename OutputTuple, typename K, class KHash=tbb_hash_compare<K> >
        class join_node< OutputTuple, key_matching<K,KHash> > : public graph_node, public sender< OutputTuple >
        {
        public:
            join_node(graph &g);
        };

        template <typename K, typename T>
        K key_from_message(const T &t);

    } // namespace flow
    } // namespace tbb
    } // namespace oneapi

``join_node`` Constructor
-------------------------

.. code:: cpp
    join_node(graph &g);

The extension adds a special constructor to the ``join_node`` class when ``key_matching`` policy
is used.

When constructed this way, the ``join_node`` uses type-associated key extraction instead
of explicit key extractor functions.

For each incoming message the ``join_node`` calls ``key_from_message<K>(std::get<I>(message))`` to
extract the key from the ``I``-th tuple element, then matches by key as usual.

``key_from_message`` Function
-----------------------------

.. code:: cpp
    template <typename K, typename T>
    K key_from_message(const T &t);

The default implementation of the ``key_from_message`` function used by ``join_node`` constructor.

Returns ``t.key()``.

Alternatively, you can define custom ``key_from_message`` function in the same namespace as
the input message type. This function will be found via C++ argument-dependent lookup.

Example
*******

This example demonstrates enriching order data with customer context by joining two independent
streams (customer profile and purchase event) by customer id. It shows how to use type-associated
key extraction to avoid writing explicit key extractor functions for each port:

- Port 0 (customer profile) uses the default ``key()`` method.
- Port 1 (purchase event) uses a custom ``key_from_message`` function defined in the ``retail``
  namespace via ADL.

When messages with matching customer ids arrive on both ports, the join node emits
a single tuple containing both.

.. literalinclude:: ./examples/type_specified_message_keys.cpp
    :language: c++
    :start-after: /*begin_type_specified_message_keys_example*/
    :end-before: /*end_type_specified_message_keys_example*/

.. rubric:: See Also

    :ref:`join_node Specification<join_node_cls>`
    :ref:`join_node Policies<join_node_policies>`
    :ref:`Preview Features<preview_features>`
