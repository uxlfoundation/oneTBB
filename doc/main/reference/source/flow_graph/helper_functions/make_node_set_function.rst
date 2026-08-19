.. _make_node_set:

==========================
``make_node_set`` function
==========================
**[flow_graph.helper_functions.make_node_set]**

.. note::
   To enable this :ref:`preview feature<preview_features>`, define the
   ``TBB_PREVIEW_FLOW_GRAPH_FEATURES`` macro to 1.

The ``make_node_set`` function template creates a set of nodes that
can be passed as arguments to ``make_edges``, ``follows`` and ``precedes`` functions.

Syntax
------

.. code:: cpp

    // Defined in header <oneapi/tbb/flow_graph.h>

    namespace oneapi {
        namespace tbb {
            namespace flow {

            template <typename Node, typename... Nodes>
            /*unspecified*/ make_node_set( Node& node, Nodes&... nodes );

            } // namespace flow
        } // namespace tbb
    } // namespace oneapi


.. rubric:: See Also

:ref:`make_edges`
:ref:`follows_precedes`
