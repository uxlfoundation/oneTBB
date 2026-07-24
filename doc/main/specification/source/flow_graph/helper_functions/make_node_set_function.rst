.. _make_node_set_function:

``make_node_set`` Function (preview)
====================================
**[flow_graph.helper_functions.make_node_set]**

.. note::
    To enable this feature, define the ``TBB_PREVIEW_FLOW_GRAPH_FEATURES`` macro to ``1``.

The ``make_node_set`` function returns an unspecified type representing a set of nodes
that can be passed to ``make_edges``, ``follows`` and ``precedes`` functions.

.. code:: cpp

    // Defined in header <oneapi/tbb/flow_graph.h>
    
    namespace oneapi {
    namespace tbb {
    namespace flow {

        template <typename NodeType, typename... NodeTypes>
        /*unspecified*/ make_node_set( NodeType& node, NodeTypes&... nodes );

    } // namespace flow
    } // namespace tbb
    } // namespace oneapi

.. rubric:: See Also

    :ref:`Preview Features<preview_features>`
