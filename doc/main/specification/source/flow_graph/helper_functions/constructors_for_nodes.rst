.. _constructors_for_fg_nodes:

Constructors for Flow Graph nodes (preview)
===========================================
**[flow_graph.helper_functions.constructors]**

.. note::
   To enable this feature, define the ``TBB_PREVIEW_FLOW_GRAPH_FEATURES`` macro to ``1``.

The "Helper Functions for Expressing Graphs" feature adds a set of new constructors
that allow constructing nodes as successors or predecessors.

Nodes can be constructed and connected to other nodes using two patterns:

- **As a successor**: ``NodeType node(follows(pred1, pred2, ...), node-specific-arguments)``.
- **As a predecessor**: ``NodeType node(precedes(succ1, succ2, ...), node-specific-arguments)``.

The ``follows()`` and ``precedes()`` functions return an unspecified type that the node constructor
accepts. The node automatically inherits its graph from the nodes passed to ``follows()`` or ``precedes()``.

All Flow Graph nodes support the ``precedes()`` pattern, and all except ``input_node`` support ``follows()``.

Where possible, the constructors support Class Template Argument Deduction (since C++17).

.. note::

    The exact type of the constructor arguments is unspecified. Constructor template parameters in the synopsis below
    are exposition only - the actual set of parameters may differ.

The synopsis below uses ``decltype(follows(...))`` to indicate that the node can be constructed as a successor,
and ``decltype(precedes(...))`` to indicate that the node can be constructed as a predecessor.

.. code:: cpp
    // Defined in header <oneapi/tbb/flow_graph.h>

    namespace oneapi {
    namespace tbb {
    namespace flow {

        // node_set is an exposition-only name for the type returned from make_node_set function

        template < typename NodeType, typename... NodeTypes >
        /*unspecified*/ follows( node_set<NodeType, NodeTypes...>& set );

        template < typename NodeType, typename... NodeTypes >
        /*unspecified*/ follows( NodeType& node, NodeType&... nodes );

        template < typename NodeType, typename... NodeTypes >
        /*unspecified*/ precedes( node_set<NodeType, NodeTypes...>& set );

        template < typename NodeType, typename... NodeTypes >
        /*unspecified*/ precedes( NodeType& node, NodeType&... nodes );

        template< typename Output, typename Policy >
        class continue_node {
        public:
            template <typename Body>
            continue_node(decltype(follows(...)), Body body, Policy = Policy());
            template <typename Body>
            continue_node(decltype(precedes(...)), Body body, Policy = Policy());

            template <typename Body>
            continue_node(decltype(follows(...)), int number_of_predecessors, Body body, Policy = Policy());
            template <typename Body>
            continue_node(decltype(precedes(...)), int number_of_predecessors, Body body, Policy = Policy());
        };

        template < typename Input, typename Output, typename Policy >
        class function_node {
        public:
            template <typename Body>
            function_node(decltype(follows(...)), std::size_t concurrency, Body body, Policy = Policy());
            template <typename Body>
            function_node(decltype(precedes(...)), std::size_t concurrency, Body body, Policy = Policy());
        };

        template < typename Output >
        class input_node {
        public:
            template <typename Body>
            input_node(decltype(precedes(...)), Body body);
        };

        template < typename Input, typename Output, typename Policy >
        class multifunction_node {
        public:
            template <typename Body>
            multifunction_node(decltype(follows(...)), std::size_t concurrency, Body body, Policy = Policy());
            template <typename Body>
            multifunction_node(decltype(precedes(...)), std::size_t concurrency, Body body, Policy = Policy());
        };

        template < typename Input, typename Output, typename Policy >
        class async_node {
        public:
            template <typename Body>
            async_node(decltype(follows(...)), std::size_t concurrency, Body body, Policy = Policy());
            template <typename Body>
            async_node(decltype(precedes(...)), std::size_t concurrency, Body body, Policy = Policy());
        };

        template < typename T >
        class overwrite_node {
        public:
            explicit overwrite_node(decltype(follows(...)));
            explicit overwrite_node(decltype(precedes(...)));
        };

        template < typename T >
        class write_once_node {
        public:
            explicit write_once_node(decltype(follows(...)));
            explicit write_once_node(decltype(precedes(...)));
        };

        template < typename T >
        class buffer_node {
        public:
            explicit buffer_node(decltype(follows(...)));
            explicit buffer_node(decltype(precedes(...)));
        };

        template < typename T >
        class queue_node {
        public:
            explicit queue_node(decltype(follows(...)));
            explicit queue_node(decltype(precedes(...)));
        };

        template < typename T, typename Compare >
        class priority_queue_node {
        public:
            explicit priority_queue_node(decltype(follows(...)), const Compare& comp = Compare());
            explicit priority_queue_node(decltype(precedes(...)), const Compare& comp = Compare());
        };

        template< typename T >
        class sequencer_node {
        public:
            template <typename Sequencer>
            sequencer_node(decltype(follows(...)), const Sequencer& s);
            template <typename Sequencer>
            sequencer_node(decltype(precedes(...)), const Sequencer& s);
        };

        template< typename T, typename DecrementType >
        class limiter_node {
        public:
            limiter_node(decltype(follows(...)), std::size_t threshold);
            limiter_node(decltype(precedes(...)), std::size_t threshold);
        };

        template < typename T >
        class broadcast_node {
        public:
            explicit broadcast_node(decltype(follows(...)));
            explicit broadcast_node(decltype(precedes(...)));
        };

        template < typename OutputTuple, typename JoinPolicy >
        class join_node {
        public:
            explicit join_node(decltype(follows(...)), JoinPolicy = JoinPolicy());
            explicit join_node(decltype(precedes(...)), JoinPolicy = JoinPolicy());
        };
        
        template < typename OutputTuple, typename K, typename KHash >
        class join_node< OutputTuple, key_matching<K, KHash> > {
        public:
            template <typename B0, typename... BN>
            join_node(decltype(follows(...)), B0 b0, BN... bn);
            template <typename B0, typename... BN>
            join_node(decltype(precedes(...)), B0 b0, BN... bn);
        };

        template < typename TupleType >
        class split_node {
        public:
            explicit split_node(decltype(follows(...)));
            explicit split_node(decltype(precedes(...)));
        };

        template <typename T0, typename... TN >
        class indexer_node {
        public:
            explicit indexer_node(decltype(follows(...)));
            explicit indexer_node(decltype(precedes(...)));
        };

    } // namespace flow
    } // namespace tbb
    } // namespace oneapi

.. rubric:: See Also

    :ref:`Preview Features<preview_features>`
