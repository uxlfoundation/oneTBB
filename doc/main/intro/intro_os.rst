.. _intro:

Introduction
============
**[intro]**

|full_name|(|short_name|) is a library that supports scalable parallel programming using standard ISO C++ code.

A program uses |short_name| to specify logical parallelism in algorithms, while |short_name| maps that parallelism
onto execution threads.

|short_name| does not require special languages or compilers. It is designed to promote scalable data parallel
programming. Additionally, it fully support nested parallelism, so you can build larger parallel components from
smaller ones. To use the library, you specify tasks, not threads, and let the library map tasks onto threads in
an efficient manner.

|short_name| employs generic programming via C++ templates, with most of its interfaces defined by requirements on
types and not specific types. Generic programming makes oneTBB flexible yet efficient through customizing APIs 
to specific needs of an application.

.. note::
  |short_name| requires C++11 standard compiler support.

The net result is that |short_name| enables you to specify parallelism far more conveniently that using raw threads,
and at the same time can improve performance.
