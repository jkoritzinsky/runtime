
macro(append_extra_compression_libs NativeLibsExtra)
  # TODO: remove the mono-style HOST_ variable checks once Mono is using eng/native/configureplatform.cmake to define the CLR_CMAKE_TARGET_ defines
  if (CLR_CMAKE_TARGET_BROWSER OR HOST_BROWSER OR CLR_CMAKE_TARGET_WASI OR HOST_WASI)
      # nothing special to link
  elseif (CLR_CMAKE_TARGET_ANDROID OR HOST_ANDROID)
      # need special case here since we want to link against libz.so but find_package() would resolve libz.a
      set(ZLIB_LIBRARIES z)
  elseif (CLR_CMAKE_TARGET_SUNOS OR HOST_SOLARIS)
      set(ZLIB_LIBRARIES z m)
  elseif (NOT CLR_CMAKE_TARGET_WIN32)
      find_package(ZLIB REQUIRED)
      set(ZLIB_LIBRARIES ${ZLIB_LIBRARIES} m)
  endif ()
  list(APPEND ${NativeLibsExtra} ${ZLIB_LIBRARIES})

  if (CLR_CMAKE_USE_SYSTEM_BROTLI)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(BROTLI REQUIRED brotlidec brotlienc brotlicommon)
  else()
    include(${CLR_SRC_NATIVE_DIR}/external/brotli.cmake)
  endif()

  list(APPEND ${NativeLibsExtra} ${BROTLI_LIBRARIES})
  include_directories(${BROTLI_INCLUDE_DIRS})
endmacro()
