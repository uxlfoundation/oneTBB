.. _resource_limiter_cls:

==========================
``resource_limiter`` Class
==========================
**[flow_graph.resource_limiting.resource_limiter]**

.. note::
   To enable this :ref:`preview feature<preview_features>`, define the
   ``TBB_PREVIEW_FLOW_GRAPH_RESOURCE_LIMITING`` or ``TBB_PREVIEW_FLOW_GRAPH_FEATURES`` macro to 1.

The class ``resource_limiter<ResourceHandle>`` represents a *Provider* that manages one or more
resource handles of type ``ResourceHandle``.

It provides exclusive access to managed resources to the consumers -- ``resource_limited_node`` instances.

For some resource types, the ``ResourceHandle`` represents the resource itself. For other resource types, the
``ResourceHandle`` may represent a lightweight entity used to access the resource.

.. code:: cpp

    tbb::flow::resource_limiter<int> int_limiter{1, 2, 3};

    using db_resource_handle = std::unique_ptr<Database, CloseDatabase>;
    tbb::flow::resource_limiter<db_resource_handle> db_limiter{std::piecewise_construct,
                                                              std::forward_as_tuple(open_database())};

In the example above, ``int_limiter`` manages three resources of type ``int``, and ``db_limiter`` manages a single
handle to a resource of type ``Database``. Because ``db_resource_handle`` is not copyable, the handle is
constructed in place with the ``std::piecewise_construct_t`` constructor.

All the resource handles managed by the ``resource_limiter`` are considered equivalent, so which particular handle a
consumer receives is unspecified.

The order in which access is granted attempts to avoid starving consumers that need several resources at once.
The ``resource_limiter`` prefers the consumer whose request was made earlier. A request retains its position in
this order for as long as it is outstanding, including while it waits for the other resources it needs and if an
attempted acquisition is denied, so a consumer waiting for several resources becomes preferred over later
requests as it waits.

This is a best-effort policy rather than a guarantee: requests are arbitrated as they are observed, so a grant
may be made before a higher-priority request becomes visible to the limiter.

Synopsis
--------

.. code:: cpp

    namespace oneapi {
        namespace tbb {
            namespace flow {

                template <typename ResourceHandle>
                class resource_limiter {
                public:
                    using resource_handle_type = ResourceHandle;

                    resource_limiter() = delete;

                    template <typename InputIterator>
                    resource_limiter(InputIterator first, InputIterator last);

                    template <typename ContainerBasedSequence>
                    resource_limiter(ContainerBasedSequence&& sequence);

                    resource_limiter(std::initializer_list<ResourceHandle> init);

                    template <typename Tuple, typename... Tuples>
                    resource_limiter(std::piecewise_construct_t, Tuple&& tuple, Tuples&&... tuples);

                    ~resource_limiter();
                }; // class resource_limiter

            } // namespace flow
        } // namespace tbb
    } // namespace oneapi

Requirements
------------

``ResourceHandle`` type must meet the ``MoveConstructible`` requirements from [moveconstructible]
and the ``MoveAssignable`` requirements from [moveassignable] sections of the ISO C++ Standard.

Member Types
------------

.. code:: cpp

    using resource_handle_type = ResourceHandle;

An alias to the resource handle type.

Member Functions
----------------

.. code:: cpp

    template <typename InputIterator>
    resource_limiter(InputIterator first, InputIterator last);

**Requirements**:

* ``InputIterator`` type must satisfy the requirements of an input iterator from [input.iterators] section of the ISO C++ Standard.
* ``ResourceHandle`` type must be constructible from ``std::iterator_traits<InputIterator>::reference``.

Constructs a ``resource_limiter`` that manages the resource handles from the sequence ``[first, last)``.

Each handle is constructed from the corresponding element in the sequence.

If ``first == last``, the behavior is undefined.

------------------------------------------------------

.. code:: cpp

    template <typename ContainerBasedSequence>
    resource_limiter(ContainerBasedSequence&& sequence);

**Requirements**: ``ContainerBasedSequence`` type must meet the :doc:`ContainerBasedSequence requirements <../../named_requirements/algorithms/container_based_sequence>`.

Equivalent to ``resource_limiter(std::begin(sequence), std::end(sequence))``.

------------------------------------------------------

.. code:: cpp

    resource_limiter(std::initializer_list<ResourceHandle> init);

Equivalent to ``resource_limiter(init.begin(), init.end())``.

.. note::

    ``std::initializer_list`` imposes the requirement of copy constructibility on ``ResourceHandle``.

------------------------------------------------------

.. code:: cpp

    template <typename Tuple, typename... Tuples>
    resource_limiter(std::piecewise_construct_t, Tuple&& tuple, Tuples&&... tuples);

**Requirements**: for each ``T`` in ``{Tuple, Tuples...}`` and the corresponding ``t`` in ``{tuple, tuples...}``,
``ResourceHandle`` must be constructible from ``std::get<N>(std::forward<T>(t))`` for each ``N`` in
``[0, std::tuple_size<std::decay_t<T>>::value)``.

Constructs a ``resource_limiter`` that manages ``1 + sizeof...(Tuples)`` resource handles. Each handle is constructed
in place from the elements of the corresponding tuple, which are forwarded as its constructor arguments.

Use this constructor if ``ResourceHandle`` is not copyable or must be constructed in place.

**Example**:

.. code:: cpp

    tbb::flow::resource_limiter<Handle> limiter(std::piecewise_construct,
                                                std::forward_as_tuple(arg1, arg2),
                                                std::forward_as_tuple(arg3));

The limiter manages two handles: the first is constructed as ``Handle(arg1, arg2)``, the second as ``Handle(arg3)``.

------------------------------------------------------

.. code:: cpp

    ~resource_limiter();

Destroys the ``resource_limiter``.

If there are consumers that still reference the limiter, the behavior is undefined.
