cmake_minimum_required(VERSION 3.24)

project(DesktopNote VERSION 2.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(desktopnote_core STATIC
  src/app_state.cpp
  src/base64.cpp
  src/data_store.cpp
  src/win_util.cpp
)
target_include_directories(desktopnote_core PUBLIC src third_party)
target_compile_definitions(desktopnote_core PUBLIC
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00
)
target_link_libraries(desktopnote_core PUBLIC crypt32 ole32 shell32)

add_executable(DesktopNote WIN32
  src/main.cpp
  src/application.cpp
  src/desktop_embedder.cpp
  src/note_renderer.cpp
  src/note_toolbar.cpp
  src/note_window.cpp
  src/rich_edit_host.cpp
  resources/DesktopNote.rc
)
target_include_directories(DesktopNote PRIVATE resources)
target_link_libraries(DesktopNote PRIVATE
  desktopnote_core
  comctl32 d2d1 dwrite dwmapi gdi32 imm32 msimg32 oleaut32 rpcrt4 shcore shlwapi user32 uuid
)

if(MSVC)
  target_compile_options(desktopnote_core PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(DesktopNote PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(desktopnote_core PRIVATE $<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/GL> $<$<CONFIG:Release>:/Gy>)
  target_compile_options(DesktopNote PRIVATE $<$<CONFIG:Release>:/O2> $<$<CONFIG:Release>:/GL> $<$<CONFIG:Release>:/Gy>)
  target_link_options(DesktopNote PRIVATE
    /MANIFEST:NO
    /SUBSYSTEM:WINDOWS,10.00
    $<$<CONFIG:Release>:/LTCG>
    $<$<CONFIG:Release>:/OPT:REF>
    $<$<CONFIG:Release>:/OPT:ICF>
  )
else()
  target_compile_options(desktopnote_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(DesktopNote PRIVATE -Wall -Wextra -Wpedantic)
  target_link_options(DesktopNote PRIVATE -static -static-libgcc -static-libstdc++ -municode)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(DesktopNoteTests
    tests/test_main.cpp
  )
  target_link_libraries(DesktopNoteTests PRIVATE desktopnote_core rpcrt4)

  add_executable(DesktopNoteRichEditTests
    tests/rich_edit_test.cpp
    src/rich_edit_host.cpp
  )
  target_link_libraries(DesktopNoteRichEditTests PRIVATE
    desktopnote_core
    d2d1 gdi32 imm32 oleaut32 user32 uuid
  )
  add_executable(DesktopNoteWindowTests
    tests/note_window_test.cpp
    src/desktop_embedder.cpp
    src/note_renderer.cpp
    src/note_toolbar.cpp
    src/note_window.cpp
    src/rich_edit_host.cpp
    resources/DesktopNote.rc
  )
  target_include_directories(DesktopNoteWindowTests PRIVATE resources)
  target_link_libraries(DesktopNoteWindowTests PRIVATE
    desktopnote_core
    comctl32 d2d1 dwrite dwmapi gdi32 imm32 msimg32 oleaut32 rpcrt4 shcore shlwapi user32 uuid
  )
  if(MSVC)
    target_compile_options(DesktopNoteTests PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(DesktopNoteRichEditTests PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(DesktopNoteWindowTests PRIVATE /W4 /permissive- /utf-8)
    target_link_options(DesktopNoteWindowTests PRIVATE /MANIFEST:NO)
  endif()
  add_test(NAME DesktopNoteTests COMMAND DesktopNoteTests)
  add_test(NAME DesktopNoteRichEditTests COMMAND DesktopNoteRichEditTests)
  add_test(NAME DesktopNoteWindowTests COMMAND DesktopNoteWindowTests)
endif()

install(TARGETS DesktopNote RUNTIME DESTINATION .)
