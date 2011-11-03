//===- Context.h - Object File Linking Context ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_CONTEXT_H
#define LLVM_OBJECT_CONTEXT_H

#include "llvm/Object/Atom.h"
#include "llvm/Support/Allocator.h"

namespace llvm {
namespace object {

/// Context object for object files and linking. Owns all atoms it creates.
class Context {
  Context(const Context&); // = delete;
  Context &operator=(const Context&); // = delete;
private:
  BumpPtrAllocator Allocator;

public:
  Context();
  ~Context();

  void *Allocate(unsigned Size, unsigned Align = 8) {
    return Allocator.Allocate(Size, Align);
  }
  void Deallocate(void *Ptr) {
  }
};

} // end namespace object
} // end namespace llvm

// operator new and delete aren't allowed inside namespaces.
// The throw specifications are mandated by the standard.
/// @brief Placement new for using the object::Context's allocator.
///
/// This placement form of operator new uses the object::Context's allocator for
/// obtaining memory. It is a non-throwing new, which means that it returns
/// null on error. (If that is what the allocator does. The current does, so if
/// this ever changes, this operator will have to be changed, too.)
/// Usage looks like this (assuming there's an object::Context 'C' in scope):
/// @code
/// // Default alignment (16)
/// IntegerLiteral *Ex = new (C) IntegerLiteral(arguments);
/// // Specific alignment
/// IntegerLiteral *Ex2 = new (C, 8) IntegerLiteral(arguments);
/// @endcode
/// Please note that you cannot use delete on the pointer; it must be
/// deallocated using an explicit destructor call followed by
/// @c C.Deallocate(Ptr).
///
/// @param Bytes The number of bytes to allocate. Calculated by the compiler.
/// @param C The object::Context that provides the allocator.
/// @param Alignment The alignment of the allocated memory (if the underlying
///                  allocator supports it).
/// @return The allocated memory. Could be NULL.
inline void *operator new(size_t Bytes, llvm::object::Context &C,
                          size_t Alignment = 16) throw () {
  return C.Allocate(Bytes, Alignment);
}
/// @brief Placement delete companion to the new above.
///
/// This operator is just a companion to the new above. There is no way of
/// invoking it directly; see the new operator for more details. This operator
/// is called implicitly by the compiler if a placement new expression using
/// the object::Context throws in the object constructor.
inline void operator delete(void *Ptr, llvm::object::Context &C, size_t)
              throw () {
  C.Deallocate(Ptr);
}

/// This placement form of operator new[] uses the object::Context's allocator
/// for obtaining memory. It is a non-throwing new[], which means that it
/// returns null on error.
/// Usage looks like this (assuming there's an object::Context 'C' in scope):
/// @code
/// // Default alignment (16)
/// char *data = new (C) char[10];
/// // Specific alignment
/// char *data = new (C, 8) char[10];
/// @endcode
/// Please note that you cannot use delete on the pointer; it must be
/// deallocated using an explicit destructor call followed by
/// @c Context.Deallocate(Ptr).
///
/// @param Bytes The number of bytes to allocate. Calculated by the compiler.
/// @param C The object::Context that provides the allocator.
/// @param Alignment The alignment of the allocated memory (if the underlying
///                  allocator supports it).
/// @return The allocated memory. Could be NULL.
inline void *operator new[](size_t Bytes, llvm::object::Context& C,
                            size_t Alignment = 16) throw () {
  return C.Allocate(Bytes, Alignment);
}

/// @brief Placement delete[] companion to the new[] above.
///
/// This operator is just a companion to the new[] above. There is no way of
/// invoking it directly; see the new[] operator for more details. This operator
/// is called implicitly by the compiler if a placement new[] expression using
/// the object::Context throws in the object constructor.
inline void operator delete[](void *Ptr, llvm::object::Context &C) throw () {
  C.Deallocate(Ptr);
}

#endif
