.. _fg_resource_limiting:

===================================
Resource Limiting in the Flow Graph
===================================
**[flow_graph.resource_limiting]**

.. note::
   To enable this :ref:`preview feature<preview_features>`, define the
   ``TBB_PREVIEW_FLOW_GRAPH_RESOURCE_LIMITING`` or ``TBB_PREVIEW_FLOW_GRAPH_FEATURES`` macro to 1.

The Resource Limiting feature enables Flow Graph nodes to safely coordinate access to shared external
resources such as database connections, thread-unsafe libraries, etc.

The feature consists of two components:

* ``flow::resource_limiter`` class - a *provider* that manages a set of resources.
* ``flow::resource_limited_node`` class - a *consumer* node whose body is invoked only after the node
  acquires access to a resource from each associated ``resource_limiter``.

A node that must hold resources from several limiters at once can make little progress if the limiters keep
granting access to nodes that need only one resource. To avoid such starvation, a ``resource_limiter``
arbitrates between competing consumers on a best-effort priority basis, preferring the consumer whose request
was made earlier, rather than granting access in an unspecified order. A request that cannot be satisfied
retains its position, so a consumer waiting for several resources becomes preferred over later requests as it
waits.

.. toctree::
    :titlesonly:

    resource_limiting/resource_limiter_cls.rst
    resource_limiting/resource_limited_node_cls.rst

Example
*******

In the example below, two nodes share an exclusive database connection through
a ``resource_limiter`` managing a single handle:

.. literalinclude:: ./examples/resource_limiting.cpp
    :language: c++
    :start-after: /*begin_fg_resource_limiting_example*/
    :end-before: /*end_fg_resource_limiting_example*/

Because ``db_limiter`` holds only one resource handle, the bodies of ``db_reader`` and ``db_writer``
are never invoked at the same time - even though both nodes allow ``unlimited`` concurrency.
