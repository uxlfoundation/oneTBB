.. _namespaces

Namespaces
==========
**[configuration.namespaces]**

This section describes the |short_name| namespace conventions.

tbb Namespace
-------------

The ``tbb`` namespace contains public identifiers defined by the library
that you can reference in your program.

tbb::flow Namespace
-------------------

The ``tbb::flow`` namespace contains public identifiers defined by the
library that you can reference in your program related to the flow graph feature. See
:doc:`Flow Graph <../flow_graph>` for more information.

tbb::info Namespace
-------------------

The ``tbb::info`` namespace contains public identifiers defined by the library
that you can reference in your program to query information about the execution environment.
See :doc:`info Namespace <../../aux_interfaces/info_namespace>` for more information.

oneapi::tbb Namespace
---------------------

The ``tbb`` namespace is part of the top-level ``oneapi`` namespace.
Therefore, all APIs from the ``tbb`` namespace (including ``tbb::flow`` and ``tbb::info`` namespaces)
are available in the ``oneapi::tbb`` namespace.
The ``oneapi::tbb`` namespace can be considered as an alias for the ``tbb`` namespace:

.. code:: cpp

    namespace oneapi { namespace tbb = ::tbb; }