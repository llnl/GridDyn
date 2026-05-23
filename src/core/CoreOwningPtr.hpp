/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "core/CoreObject.h"
#include <memory>
#include <type_traits>
#include <utility>

namespace griddyn {
/** define the function type for the deleter*/
using RemoveFunction = void (*)(CoreObject* obj);

/** template class for defining a (potentially shared) owning ptr for the CoreObject
@details uses a custom deleter to operate on the reference counter inside of the core object
intended to be used when there are multiple owners with independent lives and for direct
instantiated objects where the delete function should not be called shared pointers of coreObjects
are not recommended due to the hierarchal nature of the objects in a block
*/
template<class X>
class CoreOwningPtr {
  private:
    std::unique_ptr<X, RemoveFunction> ptr;  //!< the reference to the object
  public:
    constexpr CoreOwningPtr() noexcept: ptr(nullptr, removeReference) {}
    /*IMPLICIT*/ CoreOwningPtr(X* obj): ptr(obj, removeReference)
    {
        static_assert(std::is_base_of<CoreObject, X>::value,
                      "owning ptr type must have a base of CoreObject");
        if (obj != nullptr) {
            obj->addOwningReference();
        }
    }
    /*IMPLICIT*/ CoreOwningPtr(const CoreOwningPtr& optr): CoreOwningPtr(optr.get()) {}
    template<class Y>
    /*IMPLICIT*/ CoreOwningPtr(CoreOwningPtr<Y>&& ref) noexcept: ptr(ref.release(), removeReference)
    {
    }

    template<class Y>
    CoreOwningPtr& operator=(CoreOwningPtr<Y>&& ref) noexcept
    {
        ptr.reset(ref.release());
        return *this;
    }
    CoreOwningPtr& operator=(const CoreOwningPtr& optr) = delete;
    CoreOwningPtr& operator=(std::nullptr_t /*null*/) noexcept
    {
        ptr = nullptr;
        return *this;
    }
    auto operator->() const noexcept
    {  // return pointer to class object
        return ptr.operator->();
    }

    auto get() const noexcept
    {  // return pointer to object
        return (ptr.get());
    }

    explicit operator bool() const noexcept
    {  // test for non-null pointer
        return (static_cast<bool>(ptr));
    }
    auto release() { return ptr.release(); }
};

template<typename X, typename... Args>
CoreOwningPtr<X> makeOwningPtr(Args&&... args)
{
    return CoreOwningPtr<X>(new X(std::forward<Args>(args)...));
}

template<class X>
using coreOwningPtr = CoreOwningPtr<X>;  // NOLINT(readability-identifier-naming)

template<typename X, typename... Args>
CoreOwningPtr<X> make_owningPtr(Args&&... args)  // NOLINT(readability-identifier-naming)
{
    return makeOwningPtr<X>(std::forward<Args>(args)...);
}

/*
template <class X>
class childObject
{
private:
    X* ptr = nullptr;
    CoreObject *parent = nullptr;
public:
    childObject() noexcept{};
    childObject(X* obj,CoreObject* parentObj):ptr(obj),parent(parentObj)
    {
        static_assert (std::is_base_of<CoreObject, X>::value, "child Object ptr type must have a
base of CoreObject"); if (ptr != nullptr)
        {
            ptr->setParent(parent);
            ptr->addOwningReference();
        }
    };
    childObject(childObject &&ref) noexcept:ptr(ref.ptr)
    {

    }
    ~childObject()
    {
        if (ptr != nullptr)
        {
            removeReference(ptr, parent);
        }

    }
    childObject &operator=(childObject &&ref) noexcept
    {
        ptr=ref.ptr;
        return *this;
    }
    childObject &operator=(std::nullptr_t) noexcept
    {
        if (ptr != nullptr)
        {
            removeReference(ptr, ptr->getParent());
        }
        ptr = nullptr;
        parent=nullptr;
        return *this;
    }
    auto operator->() const noexcept
    {    // return pointer to class object
        return ptr;
    }

    auto get() const noexcept
    {    // return pointer to object
        return (ptr);
    }

    explicit operator bool() const noexcept
    {    // test for non-null pointer
        return (ptr!=nullptr);
    }
}
*/

}  // namespace griddyn
