# Shared compiler flags. Kept conservative on purpose — we add per-target flags
# rather than blanket-applying anything that could surprise a future contributor.

add_library(dafeng_compiler_flags INTERFACE)
add_library(dafeng::compiler_flags ALIAS dafeng_compiler_flags)

target_compile_features(dafeng_compiler_flags INTERFACE cxx_std_17)

if(MSVC)
  target_compile_options(dafeng_compiler_flags INTERFACE
    /W4 /permissive- /Zc:__cplusplus /utf-8
    $<$<CONFIG:RelWithDebInfo>:/Zi>
  )
  target_compile_definitions(dafeng_compiler_flags INTERFACE
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )
else()
  target_compile_options(dafeng_compiler_flags INTERFACE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -Wshadow
    -Wnon-virtual-dtor
    -fno-rtti  # librime itself disables RTTI; stay compatible.
  )
  if(APPLE)
    target_compile_options(dafeng_compiler_flags INTERFACE
      -fobjc-arc
    )
  endif()
endif()
