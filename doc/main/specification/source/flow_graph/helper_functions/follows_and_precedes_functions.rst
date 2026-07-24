.. _follows_and_precedes:

``follows`` and ``precedes`` Functions (preview)
================================================
**[flow_graph.helper_functions.follows_and_precedes]**

.. note::
    To enable this feature, define the ``TBB_PREVIEW_FLOW_GRAPH_FEATURES`` macro to ``1``.

The ``follows`` and ``precedes`` helper functions help express dependencies between nodes when building |short_name| Flow Graphs.
These helper functions can only be used as the first argument for a node constructor.

The ``follows`` helper function specifies that the constructed node is the successor of the set of nodes passed as an argument.

The ``precedes`` helper function specifies that the constructed node is the predecessor of the set of nodes passed as an argument.

``follows`` and ``precedes`` are meant to replace the graph argument as the first constructor parameter.
The constructed node inherits its graph from the nodes passed to these functions.

If the nodes passed to ``follows`` and ``precedes`` belong to different graphs, the behavior is undefined.

.. code:: cpp
    // Defined in header <oneapi/tbb/flow_graph.h>

    namespace oneapi {
    namespace tbb {
    namespace flow {

        // node_set is an exposition-only name for the type returned from make_node_set function

        template <typename NodeType, typename... NodeTypes>
        /*unspecified*/ follows( node_set<NodeType, NodeTypes...>& set );

        template <typename NodeType, typename... NodeTypes>
        /*unspecified*/ follows( NodeType& node, NodeTypes&... nodes );

        template <typename NodeType, typename... NodeTypes>
        /*unspecified*/ precedes( node_set<NodeType, NodeTypes...>& set );

        template <typename NodeType, typename... NodeTypes>
        /*unspecified*/ precedes( NodeType& node, NodeTypes&... nodes );

    } // namespace flow
    } // namespace tbb
    } // namespace oneapi

Input Parameters
----------------

``follows`` and ``precedes`` accept nodes as either a ``node_set`` or as individual variadic arguments.

The following expressions are equivalent:

.. code-block:: cpp
    :caption: A ``node_set`` as input

    auto handlers = make_node_set(n1, n2, n3);
    broadcast_node<int> input(precedes(handlers));

.. code-block:: cpp
    :caption: Individual nodes as variadic arguments
 
    broadcast_node<int> input(precedes(n1, n2, n3));

.. rubric:: See Also

    :ref:`Preview Features<preview_features>`
