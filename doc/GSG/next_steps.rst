.. _next_steps:

Next Steps
===========

After installing oneTBB, complete the following steps to start working with the library.

Set the Environment Variables
*****************************

After installing |short_name|, set the environment variables:
  
#. Go to the oneTBB installation directory. 

#. Set the environment variables using the script in ``<install_dir>`` by running:
     
   * On Linux* OS: ``vars.{sh|csh} in <install_dir>/tbb/latest/env``
   * On Windows* OS: ``vars.bat in <install_dir>/tbb/latest/env``

.. tip::

   oneTBB can coordinate with Intel(R) OpenMP on CPU resources usage to avoid
   excessive oversubscription when both runtimes are used within a process. To
   enable this feature set ``TCM_ENABLE`` environment variable to ``1``. For
   more details, see :ref:`avoid_cpu_overutilization`.


Build and Run a Sample 
**********************

.. tab-set::

    .. tab-item:: Windows* OS

        #. Create a new C++ project using your IDE. In this example, Microsoft* Visual Studio* Code is used. 
        #. Create an ``example.cpp`` file in the project. 
        #. Copy and paste the code below. It is a typical example of a |short_name| algorithm. The sample calculates a sum of all integer numbers from 1 to 100. 
   
           .. code:: 

              #include <oneapi/tbb.h>
            
              int main (){
                  int sum = oneapi::tbb::parallel_reduce(
                      oneapi::tbb::blocked_range<int>(1,101), 0,
                      [](oneapi::tbb::blocked_range<int> const& r, int init) -> int {
                          for (int v = r.begin(); v != r.end(); v++) {
                              init += v;
                          }
                          return init;
                      },
                      [](int lhs, int rhs) -> int {
                          return lhs + rhs;
                      }
                  );
      
                  printf("Sum: %d\n", sum);
                  return 0;
              }
      
        #. Open the ``tasks.json`` file in the ``.vscode`` directory and paste the following lines to the args array:

           * ``-Ipath/to/oneTBB/include`` to add oneTBB include directory. 
           * ``path/to/oneTBB/`` to add oneTBB. 

           For example:

           .. code-block::

                  { 
                     "tasks": [
                          {
                             "label": "build & run",
                             "type": "cppbuild",
                             "group": {
                             "args": [ 
                                 "/IC:\\Program Files (x86)\\Intel\\oneAPI\\tbb\\2022.0.0\\include",
                                 "C:\\Program Files (x86)\\Intel\\oneAPI\\tbb\\2022.0.0\\lib\\tbb12.lib"
                           

        #. Build the project. 
        #. Run the example. 
        #. If oneTBB is configured correctly, the output displays ``Sum: 5050``.  

    .. tab-item:: Linux* OS

        #. Create an ``example.cpp`` file in the project. 
        #. Copy and paste the code below. It is a typical example of a |short_name| algorithm. The sample calculates a sum of all integer numbers from 1 to 100. 
         
           .. code:: 

              #include <oneapi/tbb.h>

              int main(){
                  int sum = oneapi::tbb::parallel_reduce(
                      oneapi::tbb::blocked_range<int>(1,101), 0,
                      [](oneapi::tbb::blocked_range<int> const& r, int init) -> int {
                          for (int v = r.begin(); v != r.end(); v++) {
                              init += v;
                          }
                          return init;
                      },
                      [](int lhs, int rhs) -> int {
                          return lhs + rhs;
                      }
                  );
      
                  printf("Sum: %d\n", sum);
                  return 0;
              }

        #. Compile the code using oneTBB. For example, 

           .. code-block:: 

                  g++ -std=c++11 example.cpp -o example -ltbb

      
        #. Run the executable:

           .. code-block:: 

                  ./example
      
        #. If oneTBB is configured correctly, the output displays ``Sum: 5050``. 



Enable Hybrid CPU and NUMA Support
***********************************

To enable NUMA and hybrid CPU optimizations in oneTBB, complete the following steps:

1. **Locate the ``tbbbind`` Library**
   
   Find the ``tbbbind`` library included in your oneTBB installation. Starting with oneTBB 2022.2, this library is statically linked with HWLOC* 2.x. You do not need to install HWLOC separately.

2. **Ensure ``tbbbind`` Is Accessible**
   
   Place the ``tbbbind`` library in a location where oneTBB can find it during execution. Use one of the following options:
   
   * Put it in the same directory as the core tbb library (``libtbb.so``, ``tbb.dll``, etc.)
   * Add its location to the system’s library search path (``LD_LIBRARY_PATH`` on Linux* OS, ``%PATH%`` on Windows* OS)

3. **Run Your Application**
   
   You do not need to link your application explicitly with ``tbbbind``. oneTBB automatically loads it at runtime when your application calls an API that requires NUMA or hybrid CPU support

.. tip:: To confirm that ``tbbbind`` is loaded successfully, set the environment variable ``TBB_VERSION=1`` before running your application.
