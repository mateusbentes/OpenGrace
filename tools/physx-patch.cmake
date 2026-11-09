# Patch the PhysX 3.4 SDK checkout in place before building it.
#
# The SDK's makefiles ship with -Werror, which modern GCC rejects (the 2018
# codebase predates the current compiler); the sed replaces it with
# -Wno-error -fpermissive -Wno-maybe-uninitialized, the same patch the tw2
# project measured. -fpermissive is essential for the deprecated-copy
# warnings in the 2018 codebase. One header needs a const-correctness fix
# (GuGJKType.h: a non-const member returns a non-const reference to a const
# member, which GCC 13 rejects outright even with -fpermissive).
#
# The checkout is patched in place, so the physx-3.4 submodule is marked
# "ignore = dirty" in .gitmodules: the gitlink stays pinned, and the patched
# files are the SDK's own build-time adaptation, not engine content.
#
# The patch is idempotent: it is applied only when the files still carry the
# unpatched state, so a reconfigure or rebuild runs it again harmlessly.

if(NOT DEFINED SDK_DIR)
    message(FATAL_ERROR "tools/physx-patch.cmake: SDK_DIR is not set")
endif()

set(MK_DIR "${SDK_DIR}/PhysX_3.4/Source/compiler/linux64")

# 1. The makefiles: -Werror -> -Wno-error -fpermissive
#    -Wno-maybe-uninitialized.
file(GLOB _physx_mk "${MK_DIR}/Makefile*.mk")
foreach(_mk ${_physx_mk})
    file(READ "${_mk}" _content)
    string(FIND "${_content}" "-Wno-error -fpermissive" _found)
    if(_found EQUAL -1)
        string(REPLACE "-Werror"
            "-Wno-error -fpermissive -Wno-maybe-uninitialized"
            _content "${_content}")
        file(WRITE "${_mk}" "${_content}")
        message(STATUS "physx-patch: patched ${_mk}")
    endif()
endforeach()

# 2. The header: GuGJKType.h returns a non-const reference to a const member.
set(_gjk "${SDK_DIR}/PhysX_3.4/Source/GeomUtils/src/gjk/GuGJKType.h")
if(EXISTS "${_gjk}")
    file(READ "${_gjk}" _content)
    string(FIND "${_content}" "const_cast<Ps::aos::PsMatTransformV&>" _found)
    if(_found EQUAL -1)
        set(_old
            "PX_FORCE_INLINE Ps::aos::PsMatTransformV& getRelativeTransform()")
        set(_old_tail "{ return mAToB; }")
        set(_new
            "PX_FORCE_INLINE Ps::aos::PsMatTransformV& getRelativeTransform()")
        set(_new_tail
            "{ return const_cast<Ps::aos::PsMatTransformV&>(mAToB); }")
        string(CONCAT _old "${_old}" "${_old_tail}")
        string(CONCAT _new "${_new}" "${_new_tail}")
        string(REPLACE "${_old}" "${_new}" _content "${_content}")
        file(WRITE "${_gjk}" "${_content}")
        message(STATUS "physx-patch: patched GuGJKType.h")
    endif()
endif()
