.. _version_information

Version Information
===================
**[configuration.version_information]**

|short_name| has macros, functions, and an environment variable, that reveal
version and runtime information.

Header
------

.. code:: cpp
    #include <oneapi/tbb/version.h>

Synopsis
--------

.. code:: cpp
    #define ONETBB_SPEC_VERSION /*unspecified*/

    #define TBB_VERSION_MAJOR   /*unspecified*/
    #define TBB_VERSION_MINOR   /*unspecified*/
    #define TBB_VERSION_STRING  /*unspecified*/

    #define TBB_INTERFACE_VERSION_MAJOR /*unspecified*/
    #define TBB_INTERFACE_VERSION_MINOR /*unspecified*/
    #define TBB_INTERFACE_VERSION       /*unspecified*/

    const char* TBB_runtime_version();
    int TBB_runtime_interface_version();

Version Macros
--------------

|short_name| defines macros related to versioning, as described below.

.. TODO: should ONETBB_SPEC_VERSION be removed

* ``ONETBB_SPEC_VERSION`` is a decimal literal macro those value equals to
  ``x * 100 + y``, where ``x`` is the major version and ``y`` is the minor version
  of the latest oneTBB specification that is fully supported by this library version.
* ``TBB_VERSION_MAJOR`` is an integral macro those value represents a major library version.
* ``TBB_VERSION_MINOR`` is an integral macro those value represents a minor library version.
* ``TBB_VERSION_STRING`` is a string macro those value represents a full library version.
* ``TBB_INTERFACE_VERSION`` is a decimal literal value those value equals to
  ``x * 1000 + y * 10 + z``, where ``x`` is the major interface version number, ``y`` is the
  minor interface version number, and ``z`` is a decimal digit. This macro is increased in each
  |short_name| release.
* ``TBB_INTERFACE_VERSION_MAJOR`` is an integral macro those value equals to ``TBB_INTERFACE_VERSION/1000``,
  which is the major interface version number.
* ``TBB_INTERFACE_VERSION_MINOR`` is an integral macro those value equals to ``TBB_INTERFACE_VERSION/%1000/10``,
  which is the minor interface version number.

``TBB_runtime_interface_version`` Function
------------------------------------------

Function that returns the interface version of the |short_name| library that was loaded at runtime.

The value returned by ``TBB_runtime_interface_version()`` may differ from the value of
``TBB_INTERFACE_VERSION`` obtained at compile time. This can be used to identify whether an
application was compiled against a compatible version of the |short_name| headers.

In general, the runtime value ``TBB_runtime_interface_version()`` must be greater than
or equal to the compile-time value of ``TBB_INTERFACE_VERSION``. Otherwise, the application may fail to
resolve all symbols at run time.

``TBB_runtime_version`` Function
--------------------------------

Function that returns the version string of the |short_name| library that was loaded at runtime.

The value returned by ``TBB_runtime_version()`` may differ from the value of
``TBB_VERSION_STRING`` obtained at compile time.

``TBB_VERSION`` Environment Variable
------------------------------------

Set the environment variable ``TBB_VERSION`` to ``1`` to cause the library to print information on 
``stderr``. Each line is of the form ``“TBB: tag value”``, where *tag* and *value* provide additional
library information below.

.. caution::

    This output is intended for diagnostics only. Its exact format is not specified and may change in future releases.
