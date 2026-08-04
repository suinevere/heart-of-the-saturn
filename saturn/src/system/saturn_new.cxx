/*----------------------
 | saturn_new.cxx
 | Description: The global C++ allocation operators, forwarded to malloc --
 |   which saturn_compat.cxx points at SRL's High Work RAM arena, so there is
 |   still exactly one heap.
 |
 |   Why this is a file of its own, and why it must NOT include <srl.hpp>:
 |   SRL already defines these operators, but `inline`, in srl_memory.hpp. An
 |   inline definition is emitted only in translation units that include it, so
 |   any file that includes no SRL header emits calls to the standard
 |   _Znwj/_ZdlPv symbols that nothing then defines, and the link fails.
 |   Defining them here supplies exactly those symbols. Folding them into
 |   saturn_compat.cxx does not work: that file needs <srl.hpp> for the arena,
 |   and the compiler rejects them there as redefinitions of SRL's inline
 |   versions.
 |
 |   Same ordering constraint as malloc: the arena does not exist until
 |   SRL::Core::Initialize() runs, so no global or static object whose
 |   constructor allocates may exist in this build.
 | Author: suinevere
 | Dependencies: saturn_compat.h
 ----------------------*/
#include "saturn_compat.h"

/*----------------------
 | operator new / operator new[]
 | Description: Plain forwards to malloc.
 | Author: suinevere
 ----------------------*/
void *operator new(size_t size)   { return malloc(size); }
void *operator new[](size_t size) { return malloc(size); }

/*----------------------
 | operator delete / operator delete[]
 | Description: Plain forwards to free, which already tolerates NULL. The sized
 |   overloads are what GCC 14 actually emits at the call site; they ignore the
 |   size because the allocator tracks block sizes itself.
 | Author: suinevere
 ----------------------*/
void operator delete(void *ptr) noexcept             { free(ptr); }
void operator delete[](void *ptr) noexcept           { free(ptr); }
void operator delete(void *ptr, size_t) noexcept     { free(ptr); }
void operator delete[](void *ptr, size_t) noexcept   { free(ptr); }
