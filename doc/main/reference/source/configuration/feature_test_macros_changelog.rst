.. _feature_test_macros_changelog:

=================================
Changelog for Feature-test Macros
=================================
**[configuration.feature_test_macros_changelog]**

The table below contains the full history of :ref:`Feature-Test Macro<feature_test_macros>` value updates
and the corresponding changes in the features.

Features are listed in the same order as in the :ref:`main table<feature_test_macros>`, and the values of
each feature are listed from the oldest to the most recent.

.. container:: tablenoborder

    +------------------------------------------------------------------------------------+---------------------------------------------+------------+----------------------------------------------+
    | Feature                                                                            | Macro Name                                  | Value      | Changes                                      |
    +====================================================================================+=============================================+============+==============================================+
    | :ref:`Resource Limiting in the Flow Graph<fg_resource_limiting>`                   | ``TBB_HAS_FLOW_GRAPH_RESOURCE_LIMITING``    | ``202603`` | The feature was introduced.                  |
    |                                                                                    |                                             +------------+----------------------------------------------+
    |                                                                                    |                                             | ``202608`` | Changed the order in which access to the     |
    |                                                                                    |                                             |            | resources is granted to reduce starvation of |
    |                                                                                    |                                             |            | consumers that need several resources at     |
    |                                                                                    |                                             |            | once.                                        |
    |                                                                                    |                                             |            +----------------------------------------------+
    |                                                                                    |                                             |            | Extended the set of ``resource_limiter``     |
    |                                                                                    |                                             |            | constructors to support a number of          |
    |                                                                                    |                                             |            | resource handles that is known only at       |
    |                                                                                    |                                             |            | run time.                                    |
    +------------------------------------------------------------------------------------+---------------------------------------------+------------+----------------------------------------------+
    | :ref:`parallel_phase Interface for Task Arena<parallel_phase_for_task_arena>`      | ``TBB_HAS_PARALLEL_PHASE``                  | ``202603`` | The feature was introduced.                  |
    |                                                                                    |                                             +------------+----------------------------------------------+
    |                                                                                    |                                             | ``202608`` | The feature became fully supported.          |
    +------------------------------------------------------------------------------------+---------------------------------------------+------------+----------------------------------------------+
    | :ref:`Core Type Selector for Task Arena Constraints<core_type_selector>`           | ``TBB_HAS_TASK_ARENA_CORE_TYPE_SELECTOR``   | ``202603`` | The feature was introduced.                  |
    +------------------------------------------------------------------------------------+---------------------------------------------+------------+----------------------------------------------+
    | :ref:`task_group Dynamic Dependencies<dynamic_dependencies>`                       | ``TBB_HAS_TASK_GROUP_DEPENDENCIES``         | ``202603`` | The feature was introduced.                  |
    +------------------------------------------------------------------------------------+---------------------------------------------+------------+----------------------------------------------+
    | :ref:`Waiting for Individual Tasks in task_group<wait_single_task>`                | ``TBB_HAS_TASK_GROUP_WAIT_FOR_SINGLE_TASK`` | ``202603`` | The feature was introduced.                  |
    +------------------------------------------------------------------------------------+---------------------------------------------+------------+----------------------------------------------+
    | :ref:`Allocate Memory Interleaved between NUMA Nodes<numa_interleaved_allocation>` | ``TBB_HAS_NUMA_ALLOCATION``                 | ``202605`` | The feature was introduced.                  |
    +------------------------------------------------------------------------------------+---------------------------------------------+------------+----------------------------------------------+
    | :ref:`Custom Assertion Handler<custom_assertion_handler>`                          | ``TBB_HAS_CUSTOM_ASSERTION_HANDLER``        | ``202608`` | The feature moved from the ``tbb::ext``      |
    |                                                                                    |                                             |            | namespace into ``tbb``.                      |
    +------------------------------------------------------------------------------------+---------------------------------------------+------------+----------------------------------------------+
