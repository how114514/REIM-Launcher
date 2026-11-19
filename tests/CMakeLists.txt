add_executable(m1-tests
  test_main.cpp
  test_s2.cpp
  test_s3.cpp
  test_s4.cpp
  "${CMAKE_SOURCE_DIR}/runtime/preview/render_math.cpp"
)
target_include_directories(m1-tests PRIVATE
  "${CMAKE_SOURCE_DIR}/third_party/catch2"
  "${CMAKE_SOURCE_DIR}/runtime/preview"
)
target_compile_definitions(m1-tests PRIVATE
  REIM_TEST_GENERATED_DIR="${CMAKE_SOURCE_DIR}/generated/26.3"
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
