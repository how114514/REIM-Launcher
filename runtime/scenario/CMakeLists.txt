add_executable(reim-scenario
  main.cpp
)
target_link_libraries(reim-scenario PRIVATE reim_headers reim_contract)

if(MSVC)
  target_compile_options(reim-scenario PRIVATE /EHsc)
endif()
