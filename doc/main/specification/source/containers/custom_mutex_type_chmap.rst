.. _custom_mutex_chmap:

Customizing the Mutex Type for ``concurrent_hash_map`` (preview)
================================================================

.. note::
    To enable this feature, define the ``TBB_PREVIEW_CONCURRENT_HASH_MAP_EXTENSIONS`` macro to ``1``.

|short_name| ``concurrent_hash_map`` class uses a reader-writer mutex internally
to protect insertion, lookup, and erasure operations.

The default mutex type is ``spin_rw_mutex``. This feature adds an extra template parameter for ``concurrent_hash_map``
that allows customizing the type of the reader-writer mutex.

.. code:: cpp

    namespace oneapi {
    namespace tbb {

        template <typename Key, typename T,
            typename HashCompare = tbb_hash_compare<Key>,
            typename Allocator = tbb_allocator<std::pair<const Key, T>>,
            typename Mutex = spin_rw_mutex>
        class concurrent_hash_map {
        public:
            using mutex_type = Mutex;
        };

    } // namespace tbb
    } // namespace oneapi

Type Requirements
-----------------

The type of the mutex passed as a template argument for ``concurrent_hash_map`` should meet the requirements
of :ref:`ReaderWriterMutex<named_requirements/mutexes/rw_mutex>`. It should also provide the following API:

.. cpp:function:: bool ReaderWriterMutex::scoped_lock::is_writer() const;

**Returns**: ``true`` if the ``scoped_lock`` object acquired the mutex as a writer, ``false`` otherwise.

The behavior is undefined if the ``scoped_lock`` object does not acquire the mutex.

``spin_rw_mutex``, ``speculative_spin_rw_mutex``, ``queuing_rw_mutex`` and ``rw_mutex`` meet the requirements above.

Example
-------


The example below demonstrates how to wrap ``std::shared_mutex`` (C++17) to meet the requirements
of ``ReaderWriterMutex`` and how to customize ``concurrent_hash_map`` to use this mutex.

.. literalinclude:: ./examples/custom_mutex_chmap_example.cpp
    :language: c++
    :start-after: /*begin_custom_mutex_chmap_example*/
    :end-before: /*end_custom_mutex_chmap_example*/
