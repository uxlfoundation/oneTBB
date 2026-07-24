.. _thread_safery

Thread Safety
=============
**[thread_safety]**

Unless otherwise stated, the library follows these thread-safety rules:

* Two threads may call a method or function concurrently on different objects.
* Concurrent calls on the same object are not safe.

Exceptions to this convention are documented in class descriptions.
For example, concurrent containers allow certain concurrent operations on the same container.
