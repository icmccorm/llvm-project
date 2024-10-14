include_directories(..)

set(BSAN_RTL_SOURCES
bsan.cpp
  )

set(BSAN_RTL_CXX_SOURCES
  )

set(BSAN_RTL_HEADERS
  bsan_interface_internal.h
  )

set(BSAN_RTL_CFLAGS ${SANITIZER_COMMON_CFLAGS})
append_rtti_flag(OFF BSAN_RTL_CFLAGS)
append_list_if(COMPILER_RT_HAS_FPIE_FLAG -fPIE BSAN_RTL_CFLAGS)
append_list_if(COMPILER_RT_HAS_FFREESTANDING_FLAG -ffreestanding BSAN_RTL_CFLAGS)

set(BSAN_RUNTIME_LIBRARIES)

add_compiler_rt_component(bsan)

foreach(arch ${BSAN_SUPPORTED_ARCH})
	add_compiler_rt_runtime(clang_rt.bsan
		STATIC
		ARCHS ${arch}
		SOURCES ${BSAN_RTL_SOURCES}
            $<TARGET_OBJECTS:RTInterception.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommonLibc.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommonSymbolizer.${arch}>
    ADDITIONAL_HEADERS ${BSAN_RTL_HEADERS}
		CFLAGS ${BSAN_RTL_CFLAGS}
		PARENT_TARGET bsan)
  list(APPEND BSAN_RUNTIME_LIBRARIES clang_rt.bsan-${arch})
  if(SANITIZER_USE_SYMBOLS)
    add_sanitizer_rt_symbols(clang_rt.bsan
      ARCHS ${arch}
      EXTRA BSAN.syms.extra)
    add_dependencies(bsan clang_rt.bsan-${arch}-symbols)
  endif()
endforeach()