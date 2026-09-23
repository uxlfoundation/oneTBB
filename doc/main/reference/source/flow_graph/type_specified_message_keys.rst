.. _class_join_node_extension:

=========================================
Type-specified message keys for join_node
=========================================
**[flow_graph.join_node.type_specified_message_keys]**

.. note::
   To enable this :ref:`preview feature<preview_features>`, define the
   ``TBB_PREVIEW_FLOW_GRAPH_FEATURES`` macro to 1.

The extension allows a key matching ``join_node`` to obtain keys via functions associated with
its input types. The extension simplifies the existing approach by removing the need to
provide a function object for each input port of ``join_node``.

.. code:: cpp

    // Defined in header <oneapi/tbb/flow_graph.h>

    namespace oneapi {
        namespace tbb {
            namespace flow {

                template < typename OutputTuple, typename K, typename KHash = tbb_hash_compare<K> >
                class join_node< OutputTuple, key_matching<K, KHash> >
                    : public graph_node, public sender< OutputTuple >
                {
                public:
                    join_node( graph& g );
                };

                template <typename K, typename T>
                K key_from_message( const T& t );

            } // namespace flow
        } // namespace tbb
    } // namespace oneapi

``join_node`` Constructor
-------------------------

.. code:: cpp

    join_node( graph& g );

The extension adds a special constructor to the ``join_node`` interface when the
``key_matching<typename K, class KHash=tbb_hash_compare>`` policy is
used.

When constructed this way, a ``join_node`` calls the
``key_from_message`` function for each incoming message to obtain the key associated
with it. 

``key_from_message`` Function
-----------------------------

.. code:: cpp

    template <typename K, typename T>
    K key_from_message( const T& t );

The default implementation of ``key_from_message`` function used by ``join_node`` constructor.

**Returns**: ``t.key()``.

``T`` is one of the user-provided types in ``OutputTuple``, and ``K`` is the key type
of the node.

Alternatively, the user can define its own ``key_from_message`` function in the
same namespace with the message type. This function will be found via C++ argument-dependent
lookup and used in place of the default implementation.

.. rubtic:: See Also

:ref:`join_node Specification<join_node_cls>`

