.. _collaborative_once_flag_cls

collaborative_once_flag
=======================
**[algorithms.collaborative_call_once.collaborative_once_flag]**

Class used with ``collaborative_call_once`` to ensure that a function is called only once.

.. code:: cpp
    // Defined in header <oneapi/tbb/collaborative_call_once.h>

    namespace oneapi {
        namespace tbb {

            class collaborative_once_flag {
            public:
                collaborative_once_flag();
                collaborative_once_flag(const collaborative_once_flag&) = delete;
                collaborative_once_flag& operator=(const collaborative_once_flag&) = delete;
            };

        } // namespace tbb
    } // namespace oneapi

Member functions
----------------

.. cpp:function:: collaborative_once_flag()

    Constructs a ``collaborative_once_flag`` object. The initial state indicates that no function has been called.
