cmake_minimum_required(VERSION 3.20)
project(CodeTranslationMouse LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(CodeTranslationMouse WIN32
  src/main.cpp
  src/App.cpp
  src/TextCapture.cpp
  src/Translate.cpp
  src/Ui.cpp
  resources/app.rc
)

target_include_directories(CodeTranslationMouse PRIVATE src)
target_compile_definitions(CodeTranslationMouse PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
  target_compile_options(CodeTranslationMouse PRIVATE /utf-8 /W4)
else()
  target_compile_options(CodeTranslationMouse PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall)
  target_link_options(CodeTranslationMouse PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()

target_link_libraries(CodeTranslationMouse PRIVATE
  ole32 oleaut32 oleacc uuid user32 gdi32 shell32 comctl32 advapi32
)

set_source_files_properties(resources/app.rc PROPERTIES
  COMPILE_FLAGS "--include-dir=${CMAKE_SOURCE_DIR} --include-dir=${CMAKE_SOURCE_DIR}/resources"
)

add_custom_command(TARGET CodeTranslationMouse POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/dictionary.txt
          $<TARGET_FILE_DIR:CodeTranslationMouse>/dictionary.txt
)
