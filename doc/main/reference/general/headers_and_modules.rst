.. _headers_and_modules

Headers and Modules
===================
**[headers_and_modules]**

Headers
-------

Library interfaces are defined in headers.
Specific headers for each interface are listed in the corresponding API description.

Umbrella Header
---------------

The ``oneapi/tbb.h`` header is an umbrella header that includes all |short_name| library headers.
Use it for convenience when fine-grained header inclusion is not required.

.. TODO: do we want to add information about the compatibility headers like tbb/parallel_for.h

TBB Module (experimental)
-------------------------

.. caution::
    TBB C++20 module is experimental and subject to change in future releases.

|short_name| provides a C++20 module interface unit, ``tbb.cppm``, that lets you use
|short_name| via ``import tbb;`` instead of regular library headers.
The module is installed as a source file under ``<install-prefix>/include/oneapi/tbb.cppm`` and must be
compiled as part of your own build target.

Translation units that use ``import tbb;`` are ABI-compatible with those that use library headers.

For C++20 module integration details, see :ref:`cxx20_modules_support`.
