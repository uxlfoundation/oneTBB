/*
    Copyright (c) 2005-2025 Intel Corporation

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/*
    The original source for this example is
    Copyright (c) 1994-2008 John E. Stone
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
    3. The name of the author may not be used to endorse or promote products
       derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
    OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
    OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
    OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
    SUCH DAMAGE.
*/

#ifdef EMULATE_PTHREADS

#include <assert.h>
#include "pthread_w.hpp"

/*
    Basics
*/

int pthread_create(pthread_t *thread,
                   pthread_attr_t *attr,
                   void *(*start_routine)(void *),
                   void *arg) {
    pthread_t th;

    if (thread == nullptr)
        return EINVAL;
    *thread = nullptr;

    if (start_routine == nullptr)
        return EINVAL;

    th = (pthread_t)malloc(sizeof(pthread_s));
    memset(th, 0, sizeof(pthread_s));

    th->winthread_handle =
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)start_routine, arg, 0, &th->winthread_id);
    if (th->winthread_handle == nullptr)
        return EAGAIN; /*  GetLastError()  */

    *thread = th;
    return 0;
}

int pthread_join(pthread_t th, void **thread_return) {
    BOOL b_ret;
    DWORD dw_ret;

    if (thread_return)
        *thread_return = nullptr;

    if ((th == nullptr) || (th->winthread_handle == nullptr))
        return EINVAL;

    dw_ret = WaitForSingleObject(th->winthread_handle, INFINITE);
    if (dw_ret != WAIT_OBJECT_0)
        return ERROR_PTHREAD; /*  dw_ret == WAIT_FAILED; GetLastError()  */

    if (thread_return) {
        BOOL e_ret;
        DWORD exit_val;
        e_ret = GetExitCodeThread(th->winthread_handle, &exit_val);
        if (!e_ret)
            return ERROR_PTHREAD; /*  GetLastError()  */
        *thread_return = (void *)(std::size_t)exit_val;
    }

    b_ret = CloseHandle(th->winthread_handle);
    if (!b_ret)
        return ERROR_PTHREAD; /*  GetLastError()  */
    memset(th, 0, sizeof(pthread_s));
    free(th);
    th = nullptr;

    return 0;
}

void pthread_exit(void *retval) {
    /*  specific to PTHREAD_TO_WINTHREAD  */

    /* clang-format off */
    ExitThread((DWORD)(
        (std::size_t)retval)); /* thread becomes signalled so its death can be waited upon */
    /* clang-format on */
    /*NOTREACHED*/
    assert(0);
    return; /* void fnc; can't return an error code */
}

/*
    Mutex
*/

int pthread_mutex_init(pthread_mutex_t *mutex, pthread_mutexattr_t *mutex_attr) {
    InitializeCriticalSection(&mutex->critsec);
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    EnterCriticalSection(&mutex->critsec);
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    LeaveCriticalSection(&mutex->critsec);
    return 0;
}

#endif /*  EMULATE_PTHREADS  */
