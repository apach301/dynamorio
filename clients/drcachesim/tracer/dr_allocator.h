/* **********************************************************
 * Copyright (c) 2018-2023 Google, Inc.  All rights reserved.
 * **********************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of Google, Inc. nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/* dr_allocator: C++ STL allocator for DR.
 */

#ifndef _DR_ALLOCATOR_H_
#define _DR_ALLOCATOR_H_ 1

#include "dr_api.h"
#include <cstdlib>
#include <new>

namespace dynamorio {
namespace drmemtrace {

#if defined(WINDOWS) && _MSC_VER < 1910
#    define NOEXCEPT /* nothing: not supported */
#else
#    define NOEXCEPT noexcept
#endif

template <class T> struct dr_allocator_t {
    typedef T value_type;
    dr_allocator_t() = default;
    template <class U> dr_allocator_t(const dr_allocator_t<U> &) NOEXCEPT
    {
    }
    // Required for move semantics.
    template <class U> struct rebind {
        typedef dr_allocator_t<U> other;
    };
    T *
    allocate(std::size_t num)
    {
        void *ptr = dr_global_alloc(num * sizeof(T));
        // No exceptions: exit if we're out of memory.
        DR_ASSERT(ptr != NULL);
        return (T *)ptr;
    }
    void
    deallocate(T *ptr, std::size_t num) NOEXCEPT
    {
        dr_global_free(ptr, num * sizeof(T));
    }
};
template <class T, class U>
bool
operator==(const dr_allocator_t<T> &, const dr_allocator_t<U> &)
{
    return true;
}
template <class T, class U>
bool
operator!=(const dr_allocator_t<T> &, const dr_allocator_t<U> &)
{
    return false;
}

} // namespace drmemtrace
} // namespace dynamorio

#endif /* _DR_ALLOCATOR_H_ */
