# test a bug appeared in Ubuntu/Linaro 4.6.3-1ubuntu5
# Since it is related to the treatment of amd64 asm constraints, we may
# skip it in other cases (or we get a spurious "Error (cannot compile)"
# message).
if(HAVE_GCC_STYLE_AMD64_INLINE_ASM)
message(STATUS "Testing known bugs for compiler")
set(GCC_BUGS_LIST "")

# Test for Ubuntu bug

try_run(gcc-ubuntu-bug_runs gcc-ubuntu-bug_compiles
            ${PROJECT_BINARY_DIR}/config
            ${PROJECT_SOURCE_DIR}/config/gcc-ubuntu-bug.c)

if(gcc-ubuntu-bug_compiles)
    if (gcc-ubuntu-bug_runs EQUAL 0)
	# Ubuntu bug not detected
        set(VOLATILE_IF_GCC_UBUNTU_BUG 0)
    else()
	# Ubuntu bug detected
        set(GCC_BUGS_LIST "${GCC_BUGS_LIST}" "Ubuntu/Linaro 4.6.3-1ubuntu5")
        set(VOLATILE_IF_GCC_UBUNTU_BUG 1)
    endif()
else()
  message(STATUS "Testing known bugs for compiler -- Error (cannot compile)")
  set(VOLATILE_IF_GCC_UBUNTU_BUG 0)
endif()


# Test for gcc bug 58805

try_run(gcc-bug-58805_runs gcc-bug-58805_compiles
            ${PROJECT_BINARY_DIR}/config
            ${PROJECT_SOURCE_DIR}/config/gcc-bug-58805.c)

if(gcc-bug-58805_compiles)
    if (gcc-bug-58805_runs EQUAL 0)
        # gcc bug 58805 not detected
        set(VOLATILE_IF_GCC_58805_BUG 0)
    else()
        # gcc bug 58805 detected
	set(GCC_BUGS_LIST "${GCC_BUGS_LIST}" "gcc bug 58805")
        set(VOLATILE_IF_GCC_58805_BUG 1)
    endif()
else()
  message(STATUS "Testing known bugs for compiler -- Error (cannot compile)")
  set(VOLATILE_IF_GCC_58805_BUG 0)
endif()

if(GCC_BUGS_LIST STREQUAL "")
  set(GCC_BUGS_LIST "None found")
endif()

message(STATUS "Testing known bugs for compiler -- ${GCC_BUGS_LIST}")

else()
  set(VOLATILE_IF_GCC_UBUNTU_BUG 0)
  set(VOLATILE_IF_GCC_58805_BUG 0)
endif()
