add_executable(m1-tests
  test_main.cpp
  test_s2.cpp
)
target_include_directories(m1-tests PRIVATE
  "${CMAKE_SOURCE_DIR}/third_party/catch2"
)
target_link_libraries(m1-tests PRIVATE
  reim_core
  reim_contract
  mc_26_3
  reim_thirdparty
)
if(MSVC)
  target_compile_options(m1-tests PRIVATE /EHsc)
endif()

add_test(NAME m1-tests COMMAND m1-tests)
