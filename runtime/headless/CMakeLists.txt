add_executable(reim-headless
  main.cpp
)
target_link_libraries(reim-headless PRIVATE reim_headers)

if(MSVC)
  target_compile_options(reim-headless PRIVATE /EHsc)
endif()
