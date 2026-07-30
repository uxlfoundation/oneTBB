The Thread Composability Manager Protocol
=========================================

Clients request for and release permits using the Thread Composability Manager API. Each permit
contains info about resources a client can use. Clients can have multiple requests at the same time,
grouping them together using unique client IDs that are assigned by the Thread Composability Manager
upon connecting to it.

It is expected that clients follow TCM recommendations on the resource usage and does not misbehave.

Connecting to and disconnecting from the Thread Composability Manager
---------------------------------------------------------------------

Before asking for a permit every client should register itself with the Thread Composability Manager
using:

.. code:: cpp

    tcm_result_t tcmConnect(tcm_callback_t callback, tcm_client_id_t* client_id)

+-------------------+--------+--------------------------------------------------------------------------------+
| Parameter         | Type   | Description                                                                    |
+===================+========+================================================================================+
| :code:`callback`  | In     | Permit renegotiation callback.                                                 |
+-------------------+--------+--------------------------------------------------------------------------------+
| :code:`client_id` | Out    | Client ID assigned by the Thread Composability Manager for further relation.   |
+-------------------+--------+--------------------------------------------------------------------------------+

If the client does not expect to request or release resources anymore, it should close the
connection by calling:

*Warning*: Function returns :code:`TCM_RESULT_ERROR_UNKNOWN` status if :code:`TCM_ENABLE`
 environment variable is not set to :code:`1`.

.. code:: cpp

    tcm_result_t tcmDisconnect(tcm_client_id_t client_id)

+-------------------+--------+---------------------------+
| Parameter         | Type   | Description               |
+===================+========+===========================+
| :code:`client_id` | In     | Client ID to disconnect   |
+-------------------+--------+---------------------------+

Requesting a permit
-------------------

Clients request for a resources permit using:

.. code:: cpp

    tcm_result_t tcmRequestPermit(tcm_client_id_t client_id,
                                  tcm_permit_request_t request,
                                  void* callback_arg,
                                  tcm_permit_handle_t* permit_handle,
                                  tcm_permit_t* permit)

+-----------------------+----------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| Parameter             | Type     | Description                                                                                                                                                                                                                                                     |
+=======================+==========+=================================================================================================================================================================================================================================================================+
| :code:`client_id`     | In       | Client ID obtained by tcmConnect.                                                                                                                                                                                                                               |
+-----------------------+----------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`request`       | In       | Specification of resources requested.                                                                                                                                                                                                                           |
+-----------------------+----------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`callback_arg`  | In       | The argument to pass into the callback function (set previously using :code:`tcmConnect`) in case of a subsequent permit renegotiation.                                                                                                                         |
+-----------------------+----------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`permit_handle` | In/Out   | Descriptor of resources permitted by the Thread Composability Manager for use by the client. Assign :code:`nullptr` before passing to this function to request a new permit. Pass a descriptor of an existing permit to request updates to permit parameters.   |
+-----------------------+----------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`permit`        | In/Out   | The description of resources given to the client as a response to this request. Allocated/deallocated by a client, filled in by the Thread Composability Manager.                                                                                               |
+-----------------------+----------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+

The function return value is used to indicate possible execution errors, not the availability of
resources. After a successful invocation, the caller should check the permit state and fields to
ensure resource usage is allowed.

Updating a permit request
~~~~~~~~~~~~~~~~~~~~~~~~~

Updating of a permit request is done using the :code:`tcmRequestPermit` API.

To indicate that it is an update of an existing permit request rather than a request of a new one,
client passes a :code:`permit_handle` value that was returned by a previous call to
:code:`tcmRequestPermit`.

The parameters of a permit request that can be changed are:

- Callback argument (:code:`callback_arg` parameter of the :code:`tcmRequestPermit`)

- Minimum and maximum software threads (see `Permit Requests <#permit-requests>`__)

- Permit properties (see `Properties of Permits <#properties-of-permits>`__)

Reading Latest Permit Data
--------------------------

To get the latest values from the Thread Composability Manager on resources allotted to a particular
permit, the client may use the following API:

.. code:: cpp

    tcm_result_t tcmGetPermitData(tcm_permit_handle_t* permit_handle,
                                  tcm_permit_t* permit)

+-----------------------+----------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| Parameter             | Type     | Description                                                                                                                                                               |
+=======================+==========+===========================================================================================================================================================================+
| :code:`permit_handle` | In       | Existing descriptor of resources permitted by the Thread Composability Manager for use by the client.                                                                     |
+-----------------------+----------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`permit`        | In/Out   | The description of the resources given to the client as a response to this request. Allocated/deallocated by the client, filled in by the Thread Composability Manager.   |
+-----------------------+----------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------+

**Note**: Due to possible concurrent requests from clients, resulting in redistribution of resources
by the Thread Composability Manager, the data received in a :code:`permit` argument might be already
outdated by the time the thread returns from the :code:`tcmGetPermitData` function. In some
situations, the Thread Composability Manager can detect this is happening during the call to
:code:`tcmGetPermitData`, in which case a :code:`stale` flag of the received permit is set to true
(see section about `permit properties <#properties-of-permits>`__). In any case, the client is
responsible for synchronization of multiple, possibly different, copies of permit’s data on its own
side.

**Note:** :code:`tcmGetPermitData` is designed to be lightweight, lock-free function.

Threads of the client
---------------------

The client utilizes granted CPU resources by running one or more software threads.

To register a thread that will be working as part of a resource permit, user calls:

.. code:: cpp

    tcm_result_t tcmRegisterThread(tcm_permit_handle_t permit_handle)

+-----------------------+--------+--------------------------------------------------------------------------------+
| Parameter             | Type   | Description                                                                    |
+=======================+========+================================================================================+
| :code:`permit_handle` | In     | Descriptor of the granted resources current thread is going to be a part of.   |
+-----------------------+--------+--------------------------------------------------------------------------------+

To unregister a particular thread from being a part of a resources permit with which it was last
registered, user calls:

.. code:: cpp

    tcm_result_t tcmUnregisterThread()

This API is meant to be called by every thread that is going to be a part of the permit, including
the thread that requested the permit.

Idling, Activating and Deactivating a Permit
--------------------------------------------

There might be situations when a client having an active permit has just finished work and does not
have anything else to process right away. Though, new work may appear soon. In this case, the client
can avoid releasing the permit, but tell the Thread Composability Manager that it is in idle state
using :code:`tcmIdlePermit`:

.. code:: cpp

    tcm_result_t tcmIdlePermit(tcm_permit_handle_t permit_handle)

+-----------------------+--------+------------------------------------------------+
| Parameter             | Type   | Description                                    |
+=======================+========+================================================+
| :code:`permit_handle` | In     | Descriptor of the resources to mark as idle.   |
+-----------------------+--------+------------------------------------------------+

The idle state indicates that threads do not process payload but still can spend CPU cycles actively
looking for work.

If resources are not immediately needed and client does not anticipate new work soon, but still does
not want to release the permit, it calls :code:`tcmDeactivatePermit`:

.. code:: cpp

    tcm_result_t tcmDeactivatePermit(tcm_permit_handle_t permit_handle)

+-----------------------+--------+----------------------------------------------+
| Parameter             | Type   | Description                                  |
+=======================+========+==============================================+
| :code:`permit_handle` | In     | Descriptor of the resources to deactivate.   |
+-----------------------+--------+----------------------------------------------+

TCM can also deactivate an idle permit and initiate a permit negotiation – particularly, if idle
resources are needed to satisfy another request.

Once the work appears again, the client can reactivate the permit (either idle or inactive) using
:code:`tcmActivatePermit`:

.. code:: cpp

    tcm_result_t tcmActivatePermit(tcm_permit_handle_t permit_handle)

+-----------------------+----------+----------------------------------------------+
| Parameter             | Type     | Description                                  |
+=======================+==========+==============================================+
| :code:`permit_handle` | In/Out   | Descriptor of the resources to reactivate.   |
+-----------------------+----------+----------------------------------------------+

Reactivating an idle permit is typically expected to succeed; however, the client might not (yet) be
aware of TCM concurrently deactivating the permit. Reactivating an inactive permit is not guaranteed
to succeed as its resources might be in use by another client. Therefore, the caller should check
the permit state and fields to ensure resource usage is allowed.

Releasing a permit
------------------

When the resources allocated as part of the permit are not required anymore, the client releases the
permit by calling :code:`tcmReleasePermit`:

.. code:: cpp

    tcm_result_t tcmReleasePermit(tcm_permit_handle_t permit_handle)

+-----------------------+--------+------------------------------------------------------------------------------------+
| Parameter             | Type   | Description                                                                        |
+=======================+========+====================================================================================+
| :code:`permit_handle` | In     | Descriptor of the resources to release back to the Thread Composability Manager.   |
+-----------------------+--------+------------------------------------------------------------------------------------+

Data Structures of the Thread Composability Manager
===================================================

Result of the Thread Composability Manager Function Invocation
--------------------------------------------------------------

:code:`tcm_result_t` enum defines a set of possible return codes that the API may use.

.. code:: cpp

    typedef enum _tcm_result_t {
      TCM_RESULT_SUCCESS,
      TCM_RESULT_ERROR_INVALID_ARGUMENT,
      TCM_RESULT_ERROR_UNKNOWN
    } tcm_result_t;

+-------------------------------------------+-------------------------------------------------------------+
| Value                                     | Description                                                 |
+===========================================+=============================================================+
| :code:`TCM_RESULT_SUCCESS`                | Indicates successful execution of the function.             |
+-------------------------------------------+-------------------------------------------------------------+
| :code:`TCM_RESULT_ERROR_INVALID_ARGUMENT` | Indicates that one or more function arguments are invalid.  |
+-------------------------------------------+-------------------------------------------------------------+
| :code:`TCM_RESULT_ERROR_UNKNOWN`          | Indicates erroneous situation during the function execution.|
+-------------------------------------------+-------------------------------------------------------------+

States of Permits
-----------------

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

+------------------------------------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------+
| Value                              | Description                                                                                                                                                     |
+====================================+=================================================================================================================================================================+
| :code:`TCM_PERMIT_STATE_VOID`      | No permit. Neither client owns any resources associated with permit, nor does the Thread Composability Manager know about corresponding request existence.      |
+------------------------------------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`TCM_PERMIT_STATE_INACTIVE`  | Client does not own and therefore should not be using resources related to this permit.                                                                         |
+------------------------------------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`TCM_PERMIT_STATE_PENDING`   | Resources are given to another permit and cannot be re-assigned to this permit immediately, but will be considered as soon as they become available.            |
+------------------------------------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`TCM_PERMIT_STATE_IDLE`      | Resources are not used for payload processing. However, they can be made so by activation of this or the other permit describing the same resources.            |
+------------------------------------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`TCM_PERMIT_STATE_ACTIVE`    | Resources are owned by client, and they are used for payload processing.                                                                                        |
+------------------------------------+-----------------------------------------------------------------------------------------------------------------------------------------------------------------+

Properties of Permits
---------------------

The :code:`tcm_permit_flags_t` describes the properties of permits.

.. code:: cpp

    typedef struct _tcm_permit_flags_t {
      uint32_t stale : 1;
      uint32_t rigid_concurrency : 1;
      uint32_t request_as_inactive : 1;
    } tcm_permit_flags_t;

+-----------------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| Value                       | Description                                                                                                                                                                                              |
+=============================+==========================================================================================================================================================================================================+
| :code:`stale`               | Indicates whether permit data is up to date and can be relied upon.                                                                                                                                      |
+-----------------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`rigid_concurrency`   | Indicates permit requests whose concurrency cannot be changed once granted and in :code:`TCM_PERMIT_STATE_ACTIVE` state. Useful for a client that cannot adjust threads usage during payload processing. |
+-----------------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`request_as_inactive` | Indicates that TCM should not try satisfying a request, but rather return valid :code:`permit_handle` that can be used for future API calls.                                                             |
+-----------------------------+----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+

Callback Type
-------------

The type of a function to pass into :code:`tcmConnect`. The callback is called each time permit of a
client has been changed due to API calls either from same or different client. It is not called when
change is initiated by a client itself on a permit in question.

The purpose of invoking this callback function is to tell a client that the data of a permit has
been changed. Client may call :code:`tcmGetPermitData` inside callback function in order to obtain
the latest permit data.

.. code:: cpp

    typedef tcm_result_t (*tcm_callback_t)(tcm_permit_handle_t permit_handle, void* arg,
                                           tcm_callback_flags_t flags);

+-----------------------+---------------------------------------------------------------------------------+
| Value                 | Description                                                                     |
+=======================+=================================================================================+
| :code:`permit_handle` | The unique permit handle, whose data has been changed.                          |
+-----------------------+---------------------------------------------------------------------------------+
| :code:`arg`           | The callback argument a client passed to the :code:`tcmRequestPermit` function. |
+-----------------------+---------------------------------------------------------------------------------+
| :code:`flags`         | The reasons of callback invocation.                                             |
+-----------------------+---------------------------------------------------------------------------------+

Callback Invocation Reasons
---------------------------

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
-------

The :code:`tcm_permit_t` structure represents the permit data that is filled in by the Thread
Composability Manager. The client is responsible for allocating and deallocating memory for objects
of this structure, including the arrays of necessary size.

.. code:: cpp

    typedef struct _tcm_permit_t {
      uint32_t* concurrencies;
      tcm_cpu_mask_t* cpu_masks;
      uint32_t size;
      tcm_permit_state_t state;
      tcm_permit_flags_t flags;
    } tcm_permit_t;

+-----------------------+--------------------------------------------------------------------------------------------------------------------+
| Field                 | Description                                                                                                        |
+=======================+====================================================================================================================+
| :code:`concurrencies` | The array of permitted concurrencies.                                                                              |
+-----------------------+--------------------------------------------------------------------------------------------------------------------+
| :code:`cpu_masks`     | The array of permitted masks. The array items correspond to respective items of the :code:`concurrencies` array.   |
+-----------------------+--------------------------------------------------------------------------------------------------------------------+
| :code:`size`          | The size of the arrays.                                                                                            |
+-----------------------+--------------------------------------------------------------------------------------------------------------------+
| :code:`state`         | The state of the permit.                                                                                           |
+-----------------------+--------------------------------------------------------------------------------------------------------------------+
| :code:`flags`         | The flags of the permit data.                                                                                      |
+-----------------------+--------------------------------------------------------------------------------------------------------------------+

**Note**: :code:`cpu_masks` is :code:`nullptr` in case subset of resources were not specified as a
:code:`tcm_cpu_constraints_t` structure during permit request. In this case, the array of
:code:`concurrencies` contains single element and :code:`size` equals to :code:`1`.

Constraints of Permits
----------------------

Constraints describe subset of CPU resources where the requested number of software threads execute.

**Note**: The less constrained a resource request is the more composable with other requests it is
going to be. Therefore, it is better to avoid specifying constraints unless absolutely necessary. In
cases where constraints are needed, specify them as loosely as possible so that TCM has more
opportunities to balance resources between conflicting permit requests.

The subset of resources can be specified either using high-level or low-level description. For
high-level description client specifies values for :code:`numa_id`, :code:`core_type_id`, and
:code:`threads_per_core` struct fields. For low-level client speicifies the mask. In case both
low-level and high-level description are specified, Thread Composability Manager uses low-level
mask.

Objects of :code:`tcm_cpu_constraints_t` type are required to be initialized using
:code:`TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER`:

.. code:: cpp

    tcm_cpu_constraints_t constraints =
    TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;

The :code:`numa_id`, :code:`core_type_id`, and :code:`threads_per_core` can be assigned a natural
number, in which case the meaning is:

+---------------------------------------+-------------------------------------------------------------------------+
| Field                                 | Semantics of assigning an integer value                                 |
+=======================================+=========================================================================+
| :code:`numa_id`, :code:`core_type_id` | Requesting resources from item with the index equal to specified value. |
+---------------------------------------+-------------------------------------------------------------------------+
| :code:`threads_per_core`              | The number of threads to use per core.                                  |
+---------------------------------------+-------------------------------------------------------------------------+

Besides natural numbers, these fields can be assigned to special values. Special values are:

+------------------------+----------------------------------------------------------------------------------------------------------------------------------+
| Value                  | Description                                                                                                                      |
+========================+==================================================================================================================================+
| :code:`tcm_automatic`  | The Thread Composability Manager chooses the value based on the internal heuristics and current load of the platform.            |
+------------------------+----------------------------------------------------------------------------------------------------------------------------------+
| :code:`tcm_any`        | The Thread Composability Manager chooses one specific value based on the internal heuristics and current load of the platform.   |
+------------------------+----------------------------------------------------------------------------------------------------------------------------------+

.. code:: cpp

    typedef /*implementation-defined*/ tcm_cpu_mask_t;
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

+--------------------------+-------------------------------------------------------------------------------------------------------------------+
| Field                    | Description                                                                                                       |
+==========================+===================================================================================================================+
| :code:`min_concurrency`  | Minimum value of concurrency for the described hardware subset.                                                   |
+--------------------------+-------------------------------------------------------------------------------------------------------------------+
| :code:`max_concurrency`  | Maximum value of concurrency for the described hardware subset.                                                   |
+--------------------------+-------------------------------------------------------------------------------------------------------------------+
| :code:`mask`             | The low-level mask of the resources subset. If non-NULL, then it is preferred over high-level mask description.   |
+--------------------------+-------------------------------------------------------------------------------------------------------------------+
| :code:`numa_id`          | High-level mask description. The logical index of the NUMA node to restrict the search for resources within.      |
+--------------------------+-------------------------------------------------------------------------------------------------------------------+
| :code:`core_type_id`     | High-level mask description. The logical index of the core type to restrict the search for resources within.      |
+--------------------------+-------------------------------------------------------------------------------------------------------------------+
| :code:`threads_per_core` | High-level mask description. The number of threads per core to consider while searching for resources.            |
+--------------------------+-------------------------------------------------------------------------------------------------------------------+

**Note**: To avoid issues with interpretation of logical indices used to enumerate NUMA nodes and
core types, the specified values should correspond to logical indices used by HWLOC library with
which Thread Composability Manager is linked.

Permit Requests
---------------
.. _tcm_permit_request_t:

The :code:`tcm_permit_request_t` structure is the data structure that describes resources to be
requested from the Thread Composability Manager.

.. code:: cpp

    typedef struct _tcm_permit_request_t {
      int32_t min_sw_threads;
      int32_t max_sw_threads;
      tcm_cpu_constraints_t* cpu_constraints;
      uint32_t constraints_size;
      tcm_permit_flags_t flags;
    } tcm_permit_request_t;

+--------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------+
| Field                    | Description                                                                                                                                                |
+==========================+============================================================================================================================================================+
| :code:`min_sw_threads`   | The minimum number of software threads to satisfy. Permit requests that cannot be satisfied right away get :code:`TCM_PERMIT_STATE_PENDING` state.         |
+--------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`max_sw_threads`   | The maximum number of software threads desired.                                                                                                            |
+--------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`cpu_constraints`  | The array of hardware constraints, where the Thread Composability Manager should look for available resources. :code:`NULL` means no constraints are set.  |
+--------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`constraints_size` | The size of the :code:`cpu_constraints` array.                                                                                                             |
+--------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------+
| :code:`flags`            | The properties of the request.                                                                                                                             |
+--------------------------+------------------------------------------------------------------------------------------------------------------------------------------------------------+

Objects of :code:`tcm_permit_request_t` type are required to be initialized using
:code:`TCM_PERMIT_REQUEST_INITIALIZER`:

.. code:: cpp

    tcm_permit_request_t request = TCM_PERMIT_REQUEST_INITIALIZER;

**Note**: The specified values for :code:`min_sw_threads` and :code:`max_sw_threads` in the
:code:`tcm_permit_request_t` should be compatible with the :code:`min_concurrency` and
:code:`max_concurrency` values in the :code:`tcm_cpu_constraints_t` array if the latter is
specified. Otherwise, the behaviour is undefined.

The compatibility rule:

1. The sum of minimum concurrencies specified in the constraints array should be less or equal to
   the :code:`min_sw_threads` specified in the request.
2. The value of :code:`min_sw_threads` should be less or equal to :code:`max_sw_threads`.
3. The value of :code:`max_sw_threads` should be less or equal to the sum of maximum concurrencies
   specified in the constraints array.

Or using inequality notation, the compatibility rule can be written as:

.. math:: \sum_{i = 1}^{N}m_{i} \leq m \leq M \leq \sum_{i = 1}^{N}M_{i}

where:

- :math:`m_{i}` is the :code:`min_concurrency` values from the :code:`cpu_constraints` array
- :math:`M_{i}` is the :code:`max_concurrency` values from the :code:`cpu_constraints` array
- :math:`N` - the value of :code:`constraints_size` field
- :math:`m` - the value of :code:`min_sw_threads` field
- :math:`M` - the value of :code:`max_sw_threads` field
