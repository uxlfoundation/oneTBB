.. _assertion_handler:

Custom Assertion Handler
========================

.. note::
    The availability of the extension can be checked with the ``TBB_EXT_CUSTOM_ASSERTION_HANDLER`` macro after
    including either the ``oneapi/tbb/global_control.h`` or the ``oneapi/tbb/version.h`` header.

.. contents::
    :local:
    :depth: 1

Description
***********

The custom assertion handler mechanism allows applications to set their own assertion handling functions. This proposal
is modeled on the assertion handler API that existed in TBB and is semantically similar to the standard library's
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

.. note:: The handler should not return. If it eventually returns, the behavior is runtime-undefined.

Functions
---------

.. cpp:function:: assertion_handler_type set_assertion_handler(assertion_handler_type new_handler) noexcept

Set assertion handler and return its previous value. If ``new_handler`` is ``nullptr``, reset to the default handler.

.. cpp:function:: assertion_handler_type get_assertion_handler() noexcept

Return the current assertion handler.

Example
*******

.. literalinclude:: ./examples/assertion_handler.cpp
    :language: c++
    :start-after: /*begin_assertion_handler_example*/
    :end-before: /*end_assertion_handler_example*/

.. rubric:: See also

* `Enabling Debugging Features specification <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/configuration/enabling_debugging_features>`_
