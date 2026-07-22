.. _make_edges_function:

``make_edges`` Function (preview)
=================================
**[flow_graph.helper_functions.make_edges]**

.. note::
    To enable this feature, define the ``TBB_PREVIEW_FLOW_GRAPH_FEATURES`` macro to ``1``.

The ``make_edges`` function creates edges connecting a node to all nodes in a set, or all nodes in a set to a node.

``make_edges`` supports two patterns:

.. figure:: ./Resources/make_edges_usage.png
    :align: center

.. code:: cpp

    // Defined in header <oneapi/tbb/flow_graph.h>
    
    namespace oneapi {
    namespace tbb {
    namespace flow {

        // node_set is an exposition-only name for the type returned from make_node_set function

        template <typename NodeType, typename Node, typename... Nodes>
        void make_edges(node_set<Node, Nodes...>& set, NodeType& node);

        template <typename NodeType, typename Node, typename... Nodes>
        void make_edges(NodeType& node, node_set<Node, Nodes...>& set);

    } // namespace flow
    } // namespace tbb
    } // namespace oneapi

Example
-------

The following example builds the graph structure shown below:

.. figure:: ./Resources/make_edges_example.png
    :align: center

.. literalinclude:: ./examples/make_edges_function_example.cpp
    :language: c++
    :start-after: /*begin_make_edges_function_example*/
    :end-before: /*end_make_edges_function_example*/

.. rubric:: See Also

    :ref:`Preview Features<preview_features>`