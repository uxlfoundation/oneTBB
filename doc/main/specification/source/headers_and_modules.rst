.. _headers_and_modules:

Headers and Modules
===================
**[headers_and_modules]**

API Headers
-----------

|short_name| interfaces are defined in headers
located in ``<install-prefix>/include/oneapi/tbb`` directory.
Specific headers for each interface are listed in the
corresponding API description.

Compatibility Headers
---------------------

``<install-prefix>/include/tbb`` directory provides the same set of headers as
``<install-prefix>/include/oneapi/tbb`` for compatibility with old TBB releases.
For any header ``X.h``, including ``<tbb/X.h>`` is equivalent to
including ``<oneapi/tbb/X.h>``.

Umbrella Headers
----------------

The ``<oneapi/tbb.h>`` and ``<tbb/tbb.h>`` headers are umbrella headers
that include all |short_name| API headers.
Use them for convenience when fine-grained header inclusion is not
required.

TBB Module (experimental)
-------------------------

.. caution::
    TBB C++20 module is experimental and subject to change in
    future releases.

|short_name| provides a C++20 module interface unit, ``tbb.cppm``,
that lets you use |short_name| via ``import tbb;`` instead of regular
library headers.
The module is installed as a source file under
``<install-prefix>/include/oneapi/tbb.cppm`` and must be compiled
as part of your own build target.

Translation units that use ``import tbb;`` are ABI-compatible with
those that use library headers.

For C++20 module integration details, see :ref:`cxx20_modules_support`.
