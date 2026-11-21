add_executable(REIMPreview
  main.cpp
  gl_loader.cpp
  gl_renderer.cpp
  hud.cpp
  ui_canvas.cpp
  render_math.cpp
)
target_link_libraries(REIMPreview PRIVATE
  reim_core
  reim_contract
  glfw
  opengl32
)
target_include_directories(REIMPreview PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(MSVC)
  target_compile_options(REIMPreview PRIVATE /EHsc)
endif()
