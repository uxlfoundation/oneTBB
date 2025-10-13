.. _assertion_handler:

Custom Assertion Handler
========================

.. note::
    The availability of the extension can be checked with the ``TBB_EXT_CUSTOM_ASSERTION_HANDLER`` macro defined in
    ``oneapi/tbb/global_control.h`` and ``oneapi/tbb/version.h`` headers.

.. contents::
    :local:
    :depth: 2

Description
***********

OneTBB provides `internal assertion checking
<https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/configuration/enabling_debugging_features>`_
that prints an error message and terminates the application when errors are detected in oneTBB header files or
the debug version of the library. The custom assertion handler mechanism extends this by allowing developers to
implement their own assertion handling functions. The API is semantically similar to the standard library's
``std::set_terminate`` and ``std::get_terminate`` functions.

API
***

Header
------

.. code:: cpp

    #include <oneapi/tbb/global_control.h>

Synopsis
--------

.. code:: cpp

    #define TBB_EXT_CUSTOM_ASSERTION_HANDLER 202510

    namespace oneapi {
    namespace tbb {
    namespace ext {
        using assertion_handler_type = void(*)(const char* location, int line,
                                               const char* expression, const char* comment);

        assertion_handler_type set_assertion_handler(assertion_handler_type new_handler) noexcept;

        assertion_handler_type get_assertion_handler() noexcept;
    }}}

Types
-----

.. cpp:type:: assertion_handler_type

Type alias for assertion handler function pointer.

Functions
---------

.. cpp:function:: assertion_handler_type set_assertion_handler(assertion_handler_type new_handler) noexcept

Sets assertion handler and returns its previous value. If ``new_handler`` is ``nullptr``, resets to the default handler.

.. note:: ``new_handler`` must not return. If it does, the behavior is undefined.

.. cpp:function:: assertion_handler_type get_assertion_handler() noexcept

Returns the current assertion handler.

Example
*******

.. literalinclude:: ./examples/assertion_handler.cpp
    :language: c++
    :start-after: /*begin_assertion_handler_example*/
    :end-before: /*end_assertion_handler_example*/

.. rubric:: See also

* `Enabling Debugging Features specification <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/configuration/enabling_debugging_features>`_
