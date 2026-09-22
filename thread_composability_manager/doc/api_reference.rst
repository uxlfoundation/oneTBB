TCM API Reference
#################

Protocol
********

Clients request for and release permits using the Thread Composability Manager API. Each permit
contains info about resources a client can use. Clients can have multiple requests at the same time,
grouping them together using unique client IDs that are assigned by the Thread Composability Manager
upon connecting to it.

It is expected that clients follow TCM recommendations on the resource usage and do not misbehave.

Connecting to and disconnecting from TCM
****************************************

Before asking for a permit every client should register itself with the Thread Composability Manager
using the following function:

.. code:: cpp

    tcm_result_t tcmConnect(tcm_callback_t callback, tcm_client_id_t* client_id)

+-------------------+--------+--------------------------------------------------------------------+
| Parameter         | Type   | Description                                                        |
+===================+========+====================================================================+
| :code:`callback`  | In     | Permit renegotiation callback. See `Callback Type`_ for more info. |
+-------------------+--------+--------------------------------------------------------------------+
| :code:`client_id` | Out    | Client ID assigned by the Thread Composability Manager for further |
|                   |        | relation.                                                          |
+-------------------+--------+--------------------------------------------------------------------+

.. warning:: Function returns unsuccessful status if :code:`TCM_ENABLE` environment variable is not
             set to :code:`1`.

If the client does not expect to request or release resources anymore, it should close the
connection by calling the function:

.. code:: cpp

    tcm_result_t tcmDisconnect(tcm_client_id_t client_id)

+-------------------+--------+---------------------------------------------------------------+
| Parameter         | Type   | Description                                                   |
+===================+========+===============================================================+
| :code:`client_id` | In     | Client ID that was previously assigned by :code:`tcmConnect`. |
+-------------------+--------+---------------------------------------------------------------+

Requesting a permit
*******************

Clients request a permit for resources using:

.. code:: cpp

    tcm_result_t tcmRequestPermit(tcm_client_id_t client_id, tcm_permit_request_t request,
                                  void* callback_arg, tcm_permit_handle_t* permit_handle,
                                  tcm_permit_t* permit)

+-----------------------+----------+---------------------------------------------------------------+
| Parameter             | Type     | Description                                                   |
+=======================+==========+===============================================================+
| :code:`client_id`     | In       | Client ID obtained by :code:`tcmConnect`.                     |
+-----------------------+----------+---------------------------------------------------------------+
| :code:`request`       | In       | Description of the resources being requested. See             |
|                       |          | `Permit Requests`_ section for more info.                     |
+-----------------------+----------+---------------------------------------------------------------+
| :code:`callback_arg`  | In       | The argument to pass into the callback function (set          |
|                       |          | previously using :code:`tcmConnect`) in case of a subsequent  |
|                       |          | permit renegotiation.                                         |
+-----------------------+----------+---------------------------------------------------------------+
| :code:`permit_handle` | In/Out   | Descriptor of resources permitted by the Thread Composability |
|                       |          | Manager for use by the client. Assign :code:`nullptr` before  |
|                       |          | passing to this function to request a new permit. Pass a      |
|                       |          | descriptor of an existing permit to re-request it with        |
|                       |          | different parameters.                                         |
+-----------------------+----------+---------------------------------------------------------------+
| :code:`permit`        | In/Out   | The description of resources recommended to the client for    |
|                       |          | use. Allocated/deallocated by a client, filled in by TCM.     |
+-----------------------+----------+---------------------------------------------------------------+

The function return value is used to indicate possible execution errors, not the availability of
resources. After a successful invocation, the caller should check the permit state and fields to see
what resources are recommended for use.

Updating a permit request
=========================

Updating of a permit request is done by using the :code:`tcmRequestPermit` function.

To indicate that it is an update of an existing permit request rather than a request of a new one,
client passes a :code:`permit_handle` value that was returned by a previous call to
:code:`tcmRequestPermit`.

The parameters of a permit request that can be changed are:

- Callback argument (:code:`callback_arg` parameter of the :code:`tcmRequestPermit`)

- Minimum and maximum software threads (see `Permit Requests`_ section)

- Permit properties (see `Permit properties`_ section)

Reading Permit Data
*******************

To read current permit state and associated data, client calls :code:`tcmGetPermitData` function.

.. code:: cpp

    tcm_result_t tcmGetPermitData(tcm_permit_handle_t permit_handle, tcm_permit_t* permit)

+-----------------------+--------+-----------------------------------------------------------+
| Parameter             | Type   | Description                                               |
+=======================+========+===========================================================+
| :code:`permit_handle` | In     | Existing descriptor of resources permitted by the Thread  |
|                       |        | Composability Manager for use by the client.              |
+-----------------------+--------+-----------------------------------------------------------+
| :code:`permit`        | In/Out | The description of the resources given to the client as a |
|                       |        | response to this request. Allocated/deallocated by the    |
|                       |        | client, filled in by the Thread Composability Manager.    |
+-----------------------+--------+-----------------------------------------------------------+

.. warning:: Due to possible concurrent requests from clients, resulting in redistribution of
             resources by the Thread Composability Manager, the data received in a :code:`permit`
             argument might be already outdated by the time the thread returns from the
             :code:`tcmGetPermitData` function. In some situations, the Thread Composability Manager
             can detect this is happening during the call to :code:`tcmGetPermitData`, in which case
             a :code:`stale` flag of the received permit is set to true (see section about `Permit
             Properties`_). However, it is the responsibility of the client to synchronize multiple,
             possibly different, copies of permit’s data.

Threads of a client
*******************

A TCM client utilizes granted CPU resources by running one or more software threads.

To register a thread that will be working as part of a resource permit, hence consuming its
resource, client calls :code:`tcmRegisterThread` function.

.. code:: cpp

    tcm_result_t tcmRegisterThread(tcm_permit_handle_t permit_handle)

+-----------------------+--------+----------------------------------------------------------------+
| Parameter             | Type   | Description                                                    |
+=======================+========+================================================================+
| :code:`permit_handle` | In     | Descriptor of the granted resources the invoking thread is     |
|                       |        | going to consume.                                              |
+-----------------------+--------+----------------------------------------------------------------+

To unregister a thread from being a part of a resource permit hence stop consuming its resource,
user calls:

.. code:: cpp

    tcm_result_t tcmUnregisterThread()

This API is meant to be called by every thread that is going to participate in a parallel region,
for which the resource permit was obtained.

Idling, Activating and Deactivating a Permit
********************************************

When resources are not immediately needed, the client may mark them as idle by calling
:code:`tcmIdlePermit` function. The idle state indicates that threads do not process payload but
still can spend CPU cycles actively looking for work. This allows to re-activate the permit
relatively quickly in case the resources become needed again.

.. code:: cpp

    tcm_result_t tcmIdlePermit(tcm_permit_handle_t permit_handle)

+-----------------------+--------+----------------------------------------------+
| Parameter             | Type   | Description                                  |
+=======================+========+==============================================+
| :code:`permit_handle` | In     | Descriptor of the resources to mark as idle. |
+-----------------------+--------+----------------------------------------------+

If the usage of resources is not anticipated soon, the client deactivates the permit by calling
:code:`tcmDeactivatePermit` function.

.. code:: cpp

    tcm_result_t tcmDeactivatePermit(tcm_permit_handle_t permit_handle)

+-----------------------+--------+--------------------------------------------+
| Parameter             | Type   | Description                                |
+=======================+========+============================================+
| :code:`permit_handle` | In     | Descriptor of the resources to deactivate. |
+-----------------------+--------+--------------------------------------------+

TCM can also deactivate an idle permit and initiate a permit negotiation – particularly, if idle
resources are needed to satisfy another request.

Once the resources are needed again, the client can re-activate the permit (either idle or inactive)
by using :code:`tcmActivatePermit` function.

.. code:: cpp

    tcm_result_t tcmActivatePermit(tcm_permit_handle_t permit_handle)

+-----------------------+----------+---------------------------------------------+
| Parameter             | Type     | Description                                 |
+=======================+==========+=============================================+
| :code:`permit_handle` | In/Out   | Descriptor of the resources to re-activate. |
+-----------------------+----------+---------------------------------------------+

Re-activating an idle permit is typically expected to succeed; however, the client might not (yet)
be aware of TCM concurrently deactivating the permit. Re-activating an inactive permit is not
guaranteed to succeed as its resources might be in use by another client. Therefore, the caller
should check the permit state and fields to ensure resource usage is allowed.

Releasing a permit
******************

When the resources are not required anymore, the client releases the permit by calling
:code:`tcmReleasePermit` function.

.. code:: cpp

    tcm_result_t tcmReleasePermit(tcm_permit_handle_t permit_handle)

+-----------------------+--------+-----------------------------------------------------------+
| Parameter             | Type   | Description                                               |
+=======================+========+===========================================================+
| :code:`permit_handle` | In     | Descriptor of the resources to release back to the Thread |
|                       |        | Composability Manager.                                    |
+-----------------------+--------+-----------------------------------------------------------+

TCM Data Structures
*******************

Dependency on HWLOC
===================

TCM uses `HWLOC library <https://www.open-mpi.org/projects/hwloc/>`_ to parse platform topology and
obtain process concurrency, process CPU mask, NUMA node, and core type indices.

To make sure CPU masks, NUMA node and core type indices are interpreted by HWLOC library correctly,
TCM client can either link with compatible version of HWLOC or write adapters for CPU masks.

.. warning:: Even compatible versions of HWLOC might have different results of parsing of platform
             topology. Therefore, it is recommended to ensure that single HWLOC library is used
             within the process.

CPU Mask Adapter
----------------

.. note:: While NUMA node and core type indices are logical and thus may not correspond to physical
          indices provided by an operating system, CPU masks represented using
          :code:`tcm_cpu_mask_t` are always filled with physical indices of an operating system,
          which can be used to bind software threads to hardware CPUs specified in the mask.

:code:`tcm_cpu_mask_t` is a typedef-declaration of a pointer to :code:`hwloc_bitmap_s`, which is
defined in HWLOC as the following:

.. code-block:: cpp

     struct hwloc_bitmap_s {
         unsigned ulongs_count; /* how many ulong bitmasks are valid, >= 1 */
         unsigned ulongs_allocated; /* how many ulong bitmasks are allocated, >= ulongs_count */
         unsigned long *ulongs;
         int infinite; /* set to 1 if all bits beyond ulongs are set */
     };

, where:

- :code:`ulongs_count` is the number of :code:`unsigned long` elements used for bitmask
  representation.
- :code:`ulongs_allocated` is the size of allocated elements of an array.
- :code:`ulongs` is an array that holds the mask bits.
- :code:`infinite` is used as a flag to indicate whether bits not represented by :code:`ulongs`
  array are set or not.

.. note:: :code:`hwloc_bitmap_s` is one of the main data structures that HWLOC uses when it
          describes platform entities such as NUMA node, core type, or even CPUs that share certain
          level of cache in terms of a CPU mask, it is unlikely that its layout changes in backward
          incompatible way.

Since :code:`hwloc_bitmap_s` is filled with physical, operating system indices, the conversion
between :code:`hwloc_bitmap_s` and CPU masks used in operating system involves going over the mask
bits in a loop and setting corresponding bits in a platform-specific mask representation.

TCM Function Result
===================

:code:`tcm_result_t` enum defines a set of possible values that the TCM API may return.

.. code:: cpp

    typedef enum _tcm_result_t {
      TCM_RESULT_SUCCESS,
      TCM_RESULT_ERROR_INVALID_ARGUMENT,
      TCM_RESULT_ERROR_UNKNOWN
    } tcm_result_t;

+-------------------------------------------+---------------------------------------------------+
| Value                                     | Description                                       |
+===========================================+===================================================+
| :code:`TCM_RESULT_SUCCESS`                | Indicates successful execution of the function.   |
+-------------------------------------------+---------------------------------------------------+
| :code:`TCM_RESULT_ERROR_INVALID_ARGUMENT` | Indicates that one or more function arguments are |
|                                           | invalid.                                          |
+-------------------------------------------+---------------------------------------------------+
| :code:`TCM_RESULT_ERROR_UNKNOWN`          | Indicates erroneous situation during the function |
|                                           | execution.                                        |
+-------------------------------------------+---------------------------------------------------+

Permit State
============

The :code:`tcm_permit_state_t` structure describes various states of a permit that the Thread
Composability Manager uses to indicate ownership of resources described by a permit.

.. code:: cpp

    enum tcm_permit_states_t {
      TCM_PERMIT_STATE_VOID,
      TCM_PERMIT_STATE_INACTIVE,
      TCM_PERMIT_STATE_PENDING,
      TCM_PERMIT_STATE_IDLE,
      TCM_PERMIT_STATE_ACTIVE
    };

    typedef uint8_t tcm_permit_state_t;

+------------------------------------+-------------------------------------------------------------+
| Value                              | Description                                                 |
+====================================+=============================================================+
| :code:`TCM_PERMIT_STATE_VOID`      | No permit. Neither client owns any resources associated     |
|                                    | with permit, nor does the Thread Composability Manager know |
|                                    | about existence of a corresponding request.                 |
+------------------------------------+-------------------------------------------------------------+
| :code:`TCM_PERMIT_STATE_INACTIVE`  | Client does not own and therefore should not be using       |
|                                    | resources related to this permit.                           |
+------------------------------------+-------------------------------------------------------------+
| :code:`TCM_PERMIT_STATE_PENDING`   | Resources are given to another permit and cannot be         |
|                                    | re-assigned to this permit immediately, but will be         |
|                                    | considered as soon as they become available.                |
+------------------------------------+-------------------------------------------------------------+
| :code:`TCM_PERMIT_STATE_IDLE`      | Resources are not used for payload processing. However,     |
|                                    | they can be made so by activation of this or the other      |
|                                    | permit describing the same resources.                       |
+------------------------------------+-------------------------------------------------------------+
| :code:`TCM_PERMIT_STATE_ACTIVE`    | Resources are owned by the client, and are used for payload |
|                                    | processing.                                                 |
+------------------------------------+-------------------------------------------------------------+

Permit Properties
=================

The :code:`tcm_permit_flags_t` describes the properties of a permit.

.. code:: cpp

    typedef struct _tcm_permit_flags_t {
      uint32_t stale : 1;
      uint32_t rigid_concurrency : 1;
      uint32_t request_as_inactive : 1;
    } tcm_permit_flags_t;

+-----------------------------+--------------------------------------------------------------------+
| Value                       | Description                                                        |
+=============================+====================================================================+
| :code:`stale`               | Indicates whether permit data is up to date and can be relied upon.|
+-----------------------------+--------------------------------------------------------------------+
| :code:`rigid_concurrency`   | Indicates permit requests whose concurrency cannot be changed once |
|                             | granted and in :code:`TCM_PERMIT_STATE_ACTIVE` state. Useful for   |
|                             | the client that cannot adjust the number of active threads during  |
|                             | payload processing.                                                |
+-----------------------------+--------------------------------------------------------------------+
| :code:`request_as_inactive` | Indicates that TCM should not try satisfying the request, but      |
|                             | rather return valid :code:`permit_handle` that can be used in      |
|                             | future API calls.                                                  |
+-----------------------------+--------------------------------------------------------------------+

Callback Type
=============

The type of a function to pass into :code:`tcmConnect`. The callback is called each time the permit
has been changed due to API calls either from same or different client. It is not called when change
is initiated by a client itself on a permit in question.

The purpose of invoking this callback function is to tell a client that the data of a permit has
been changed. Client may call :code:`tcmGetPermitData` inside callback function in order to obtain
the latest permit data.

.. code:: cpp

    typedef tcm_result_t (*tcm_callback_t)(tcm_permit_handle_t permit_handle, void* arg,
                                           tcm_callback_flags_t flags);

+-----------------------+--------------------------------------------------------+
| Value                 | Description                                            |
+=======================+========================================================+
| :code:`permit_handle` | The unique permit handle, whose data has been changed. |
+-----------------------+--------------------------------------------------------+
| :code:`arg`           | The callback argument that was previously passed to    |
|                       | the :code:`tcmRequestPermit` function.                 |
+-----------------------+--------------------------------------------------------+
| :code:`flags`         | The reasons of callback invocation.                    |
+-----------------------+--------------------------------------------------------+

Callback Invocation Reasons
===========================

The :code:`tcm_callbacks_flags_t` describes the reasons client callbacks were invoked by the Thread
Composability Manager.

.. code:: cpp

    typedef struct _tcm_callback_flags_t {
      bool new_concurrency : 1;
      bool new_state : 1;
    } tcm_callback_flags_t;

+-------------------------+----------------------------------------------------------+
| Value                   | Description                                              |
+=========================+==========================================================+
| :code:`new_concurrency` | Indicates whether permit's concurrency has been updated. |
+-------------------------+----------------------------------------------------------+
| :code:`new_state`       | Indicates whether permit's state has been updated.       |
+-------------------------+----------------------------------------------------------+

Permits
=======

The :code:`tcm_permit_t` structure represents the permit data that is filled in by the Thread
Composability Manager. The client is responsible for allocating and deallocating memory for objects
of this type, including the arrays of necessary size.

.. code:: cpp

    typedef struct _tcm_permit_t {
      uint32_t* concurrencies;
      tcm_cpu_mask_t* cpu_masks;
      uint32_t size;
      tcm_permit_state_t state;
      tcm_permit_flags_t flags;
    } tcm_permit_t;

+-----------------------+------------------------------------------------------------------------+
| Field                 | Description                                                            |
+=======================+========================================================================+
| :code:`concurrencies` | The array of permitted concurrencies.                                  |
+-----------------------+------------------------------------------------------------------------+
| :code:`cpu_masks`     | The array of permitted masks. The array items correspond to respective |
|                       | items of the :code:`concurrencies` array.                              |
+-----------------------+------------------------------------------------------------------------+
| :code:`size`          | The size of the arrays.                                                |
+-----------------------+------------------------------------------------------------------------+
| :code:`state`         | The state of the permit. See `Permit State`_ for details.              |
+-----------------------+------------------------------------------------------------------------+
| :code:`flags`         | The flags of the permit. See `Permit Properties`_ for details.         |
+-----------------------+------------------------------------------------------------------------+

.. note:: :code:`cpu_masks` is :code:`nullptr` in case subset of resources were not specified via
          :code:`tcm_cpu_constraints_t` during the permit request. In this case, the array of
          :code:`concurrencies` contains single element and :code:`size` equals to :code:`1`.

Permit Constraints
==================

Constraints describe subset of CPU resources where the requested number of software threads will
execute.

.. note:: The less constrained a resource request is, the more composable with other requests it is
          going to be. Therefore, it is better to avoid specifying constraints unless absolutely
          necessary. In cases where constraints are needed, specify them as loosely as possible so
          that TCM has more opportunities to balance resources between conflicting permit requests.

The subset of resources can be specified either by using high-level or low-level description. For
high-level description client sets values for :code:`numa_id`, :code:`core_type_id`, and
:code:`threads_per_core` struct fields. For low-level client specifies the mask. In case both
low-level and high-level description are specified, the TCM prefers low-level description.

Objects of :code:`tcm_cpu_constraints_t` type are required to be initialized using
:code:`TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER`:

.. code:: cpp

    tcm_cpu_constraints_t constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;

The :code:`numa_id`, :code:`core_type_id`, and :code:`threads_per_core` can be assigned a natural
number, in which case the meaning is the following:

+---------------------------------------+--------------------------------------------------------+
| Field                                 | Semantics of Assigning a Natural Number                |
+=======================================+========================================================+
| :code:`numa_id`, :code:`core_type_id` | Requesting resources from item with the index equal to |
|                                       | specified value.                                       |
+---------------------------------------+--------------------------------------------------------+
| :code:`threads_per_core`              | The number of threads to use per core.                 |
+---------------------------------------+--------------------------------------------------------+

Besides natural numbers, these fields can be assigned the following special values:

+------------------------+----------------------------------------------------------------------+
| Value                  | Description                                                          |
+========================+======================================================================+
| :code:`tcm_automatic`  | The TCM decides on its own and may choose the value automatically    |
|                        | based on the internal heuristics and current load of the platform.   |
+------------------------+----------------------------------------------------------------------+
| :code:`tcm_any`        | The TCM chooses one specific value based on the internal heuristics  |
|                        | and current load of the platform.                                    |
+------------------------+----------------------------------------------------------------------+

.. code:: cpp

    typedef struct hwloc_bitmap_s* tcm_cpu_mask_t;
    typedef /*implementation-defined*/ tcm_numa_node_t;
    typedef /*implementation-defined*/ tcm_core_type_t;

    const /*implementation-defined*/ tcm_automatic =/*implementation-defined*/;
    const /*implementation-defined*/ tcm_any =/*implementation-defined*/;

    typedef struct _tcm_cpu_constraints_t {
      int32_t min_concurrency;
      int32_t max_concurrency;
      tcm_cpu_mask_t mask;
      tcm_numa_node_t numa_id;
      tcm_core_type_t core_type_id;
      int32_t threads_per_core;
    } tcm_cpu_constraints_t;

+--------------------------+---------------------------------------------------------------------+
| Field                    | Description                                                         |
+==========================+=====================================================================+
| :code:`min_concurrency`  | Minimum value of concurrency for the described hardware subset.     |
+--------------------------+---------------------------------------------------------------------+
| :code:`max_concurrency`  | Maximum value of concurrency for the described hardware subset.     |
+--------------------------+---------------------------------------------------------------------+
| :code:`mask`             | The low-level mask of the subset of CPU resources. Can be filled    |
|                          | with physical indices of an operating system. See                   |
|                          | `CPU Mask Adapter`_ for details.                                    |
+--------------------------+---------------------------------------------------------------------+
| :code:`numa_id`          | High-level mask description. The logical index of the NUMA node to  |
|                          | restrict the search for resources within.                           |
+--------------------------+---------------------------------------------------------------------+
| :code:`core_type_id`     | High-level mask description. The logical index of the core type to  |
|                          | restrict the search for resources within.                           |
+--------------------------+---------------------------------------------------------------------+
| :code:`threads_per_core` | High-level mask description. The number of threads per core to      |
|                          | consider while searching for resources.                             |
+--------------------------+---------------------------------------------------------------------+

.. note:: To avoid issues with interpretation of logical indices used to enumerate NUMA nodes and
          core types, the specified values should correspond to logical indices used by HWLOC
          library with which the Thread Composability Manager is linked. See `Dependency on HWLOC`_
          for more details.

.. _permit_requests:

Permit Requests
===============

The :code:`tcm_permit_request_t` structure is the data structure that allows describing resources to
be requested from the Thread Composability Manager.

.. code:: cpp

    typedef struct _tcm_permit_request_t {
      int32_t min_sw_threads;
      int32_t max_sw_threads;
      tcm_cpu_constraints_t* cpu_constraints;
      uint32_t constraints_size;
      tcm_permit_flags_t flags;
    } tcm_permit_request_t;

+--------------------------+--------------------------------------------------------------------+
| Field                    | Description                                                        |
+==========================+====================================================================+
| :code:`min_sw_threads`   | The minimum number of software threads to satisfy. Permit requests |
|                          | whose minimum number of software threads cannot be satisfied right |
|                          | away get :code:`TCM_PERMIT_STATE_PENDING` state.                   |
+--------------------------+--------------------------------------------------------------------+
| :code:`max_sw_threads`   | The maximum number of software threads desired.                    |
+--------------------------+--------------------------------------------------------------------+
| :code:`cpu_constraints`  | The array of hardware constraints, where the Thread Composability  |
|                          | Manager should look for available resources. :code:`nullptr` means |
|                          | no constraints are set. See `Permit Constraints`_ for details.     |
+--------------------------+--------------------------------------------------------------------+
| :code:`constraints_size` | The size of the :code:`cpu_constraints` array.                     |
+--------------------------+--------------------------------------------------------------------+
| :code:`flags`            | The properties of the request. See `Permit Properties`_ for        |
|                          | details.                                                           |
+--------------------------+--------------------------------------------------------------------+

Objects of :code:`tcm_permit_request_t` type are required to be initialized using
:code:`TCM_PERMIT_REQUEST_INITIALIZER`:

.. code:: cpp

    tcm_permit_request_t request = TCM_PERMIT_REQUEST_INITIALIZER;

.. note:: The specified values for :code:`min_sw_threads` and :code:`max_sw_threads` in the
          :code:`tcm_permit_request_t` should be compatible with the :code:`min_concurrency` and
          :code:`max_concurrency` values in the :code:`tcm_cpu_constraints_t` array if the latter is
          specified. Otherwise, the behaviour is undefined.

The compatibility rule:

1. The sum of minimum concurrencies specified in the constraints array should be less or equal to
   the :code:`min_sw_threads` specified in the request.
2. The value of :code:`min_sw_threads` should be less or equal to :code:`max_sw_threads`.
3. The value of :code:`max_sw_threads` should be less or equal to the sum of maximum concurrencies
   specified in the constraints array.

Or using inequality notation, the compatibility rule can be written as the following:

.. math::

   \sum_{i=1}^N m_i \leq m \leq M \leq \sum_{i=1}^N M_i


where:

- :math:`m_i` is the :code:`min_concurrency` values from the :code:`cpu_constraints` array
- :math:`M_i` is the :code:`max_concurrency` values from the :code:`cpu_constraints` array
- :math:`N` is the value of :code:`constraints_size` field
- :math:`m` is the value of :code:`min_sw_threads` field
- :math:`M` is the value of :code:`max_sw_threads` field
