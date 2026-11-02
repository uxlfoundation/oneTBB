.. _constructors_for_fg_nodes:

=================================
Constructors for Flow Graph nodes
=================================
**[flow_graph.helper_functions.constructors]**

.. note::
   To enable this :ref:`preview feature<preview_features>`, define the
   ``TBB_PREVIEW_FLOW_GRAPH_FEATURES`` macro to 1.

The "Helper Functions for Expressing Graphs" feature adds a set of new constructors
that can be used to construct a node that ``follows`` or ``precedes`` a set of nodes.

Where possible, the constructors support Class Template Argument Deduction (since C++17).

.. code:: cpp

    // Defined in header <oneapi/tbb/flow_graph.h>

    // continue_node
    continue_node(decltype(follows(...)), Body body, Policy = Policy());
    continue_node(decltype(precedes(...)), Body body, Policy = Policy());

    continue_node(decltype(follows(...)), int number_of_predecessors, Body body, Policy = Policy());
    continue_node(decltype(precedes(...)), int number_of_predecessors, Body body, Policy = Policy());

    // function_node
    function_node(decltype(follows(...)), Body body, std::size_t concurrency, Policy = Policy());
    function_node(decltype(precedes(...)), Body body, std::size_t concurrency, Policy = Policy());

    // input_node
    input_node(decltype(precedes(...)), Body body);

    // multifunction_node
    multifunction_node(decltype(follows(...)), std::size_t concurrency, Body body);
    multifunction_node(decltype(precedes(...)), std::size_t concurrency, Body body);

    // async_node
    async_node(decltype(follows(...)), std::size_t concurrency, Body body);
    async_node(decltype(precedes(...)), std::size_t concurrency, Body body);

    // overwrite_node
    explicit overwrite_node(decltype(follows(...)));
    explicit overwrite_node(decltype(precedes(...)));

    // write_once_node
    explicit write_once_node(decltype(follows(...)));
    explicit write_once_node(decltype(precedes(...)));

    // buffer_node
    explicit buffer_node(decltype(follows(...)));
    explicit buffer_node(decltype(precedes(...)));

    // queue_node
    explicit queue_node(decltype(follows(...)));
    explicit queue_node(decltype(precedes(...)));

    // priority_queue_node
    explicit priority_queue_node(decltype(follows(...)), const Compare& comp = Compare());
    explicit priority_queue_node(decltype(precedes(...)), const Compare& compare = Compare());

    // sequencer_node
    sequencer_node(decltype(follows(...)), const Sequencer& s);
    sequencer_node(decltype(precedes(...)), const Sequencer& s);

    // limiter_node
    limiter_node(decltype(follows(...)), std::size_t threshold);
    limiter_node(decltype(precedes(...)), std::size_t threshold);

    // broadcast_node
    explicit broadcast_node(decltype(follows(...)));
    explicit broadcast_node(decltype(precedes(...)));

    // join_node
    explicit join_node(decltype(follows(...)), Policy = Policy());
    explicit join_node(decltype(precedes(...)), Policy = Policy());

    // key_matching join_node
    template <typename B0, typename... BN>
    join_node(decltype(follows(...)), B0 b0, BN... bn);
    template <typename B0, typename... BN>
    join_node(decltype(precedes(...)), B0 b0, BN... bn);

    // split_node
    explicit split_node(decltype(follows(...)));
    explicit split_node(decltype(precedes(...)));

    // indexer_node
    indexer_node(decltype(follows(...)));
    indexer_node(decltype(precedes(...)));

.. rubric:: See Also

:ref:`follows_precedes`
