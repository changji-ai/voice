cmake_minimum_required(VERSION 3.21)
project(VoiceAssistant VERSION 1.0.0 LANGUAGES CXX)

# CI 发布时由 workflow 注入 tag 版本（-DVOICE_APP_VERSION=1.2.3）
if(DEFINED VOICE_APP_VERSION)
  set(PROJECT_VERSION ${VOICE_APP_VERSION})
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(BUILD_TESTS "Build local test executables" OFF)

# Ubuntu 24.04 自带 Qt 6.4，桌面安装器用 6.8 —— 取交集 6.4
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Network WebSockets Multimedia Concurrent)

# ---- sherpa-onnx C API（预编译库，纯 C 接口跨编译器）----
if(NOT DEFINED SHERPA_ROOT)
  set(SHERPA_ROOT ${CMAKE_SOURCE_DIR}/third_party/sherpa-onnx)
endif()
add_library(sherpa-onnx-c-api SHARED IMPORTED GLOBAL)
# 官方发布包头文件在 include/sherpa-onnx/c-api/，本地 third_party 在 include/ —— 两个都加
set(SHERPA_INC_DIRS ${SHERPA_ROOT}/include)
if(EXISTS ${SHERPA_ROOT}/include/sherpa-onnx/c-api/c-api.h)
  list(APPEND SHERPA_INC_DIRS ${SHERPA_ROOT}/include/sherpa-onnx/c-api)
endif()
if(WIN32)
  if(EXISTS ${SHERPA_ROOT}/lib/sherpa-onnx-c-api.lib)
    # MSVC / 官方包：导入库 + DLL
    set_target_properties(sherpa-onnx-c-api PROPERTIES
      IMPORTED_LOCATION ${SHERPA_ROOT}/bin/sherpa-onnx-c-api.dll
      IMPORTED_IMPLIB ${SHERPA_ROOT}/lib/sherpa-onnx-c-api.lib
      INTERFACE_INCLUDE_DIRECTORIES "${SHERPA_INC_DIRS}")
  else()
    # MinGW：直接把 DLL 当导入库链接
    set_target_properties(sherpa-onnx-c-api PROPERTIES
      IMPORTED_LOCATION ${SHERPA_ROOT}/bin/sherpa-onnx-c-api.dll
      IMPORTED_IMPLIB ${SHERPA_ROOT}/bin/sherpa-onnx-c-api.dll
      INTERFACE_INCLUDE_DIRECTORIES "${SHERPA_INC_DIRS}")
  endif()
elseif(APPLE)
  set_target_properties(sherpa-onnx-c-api PROPERTIES
    IMPORTED_LOCATION ${SHERPA_ROOT}/lib/libsherpa-onnx-c-api.dylib
    INTERFACE_INCLUDE_DIRECTORIES "${SHERPA_INC_DIRS}")
else()
  set_target_properties(sherpa-onnx-c-api PROPERTIES
    IMPORTED_LOCATION ${SHERPA_ROOT}/lib/libsherpa-onnx-c-api.so
    INTERFACE_INCLUDE_DIRECTORIES "${SHERPA_INC_DIRS}")
endif()

set(SOURCES
  src/main.cpp
  src/MainWindow.cpp
  src/SettingsDialog.cpp
  src/util/Settings.cpp
  src/audio/AudioCapture.cpp
  src/audio/AudioPlayer.cpp
  src/audio/Vad.cpp
  src/stt/SttEngine.cpp
  src/stt/LocalStt.cpp
  src/stt/ParaformerStt.cpp
  src/stt/OpenAiStt.cpp
  src/tts/TtsEngine.cpp
  src/tts/LocalTts.cpp
  src/tts/CosyVoiceTts.cpp
  src/tts/OpenAiTts.cpp
  src/tts/AudioDecoder.cpp
  src/llm/LlmClient.cpp
  src/models/ModelManager.cpp
  src/clone/VoiceClone.cpp
  src/update/Updater.cpp
)

add_executable(VoiceAssistant WIN32 MACOSX_BUNDLE ${SOURCES})
target_link_libraries(VoiceAssistant PRIVATE
  Qt6::Widgets Qt6::Network Qt6::WebSockets Qt6::Multimedia Qt6::Concurrent sherpa-onnx-c-api
)
target_include_directories(VoiceAssistant PRIVATE src)
target_compile_definitions(VoiceAssistant PRIVATE
  VOICE_VERSION="${PROJECT_VERSION}"
  VOICE_REPO="changji-ai/voice"
)
if(MSVC)
  target_compile_options(VoiceAssistant PRIVATE /utf-8)
endif()
if(UNIX AND NOT APPLE)
  set_target_properties(VoiceAssistant PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/lib")
endif()
set_target_properties(VoiceAssistant PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER "ai.changji.voice"
  MACOSX_BUNDLE_BUNDLE_NAME "VoiceAssistant"
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

# 拷贝 sherpa DLL 到输出目录（Windows）
if(WIN32 AND EXISTS ${SHERPA_ROOT}/bin/sherpa-onnx-c-api.dll)
  add_custom_command(TARGET VoiceAssistant POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${SHERPA_ROOT}/bin/sherpa-onnx-c-api.dll
      ${SHERPA_ROOT}/bin/onnxruntime.dll
      $<TARGET_FILE_DIR:VoiceAssistant>
  )
endif()

# 本地冒烟测试（不进 CI）
if(BUILD_TESTS)
  add_executable(test_chain tests/test_chain.cpp)
  target_link_libraries(test_chain PRIVATE Qt6::Core Qt6::Network sherpa-onnx-c-api)
  set_target_properties(test_chain PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  add_custom_command(TARGET test_chain POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${SHERPA_ROOT}/bin/sherpa-onnx-c-api.dll
      ${SHERPA_ROOT}/bin/onnxruntime.dll
      $<TARGET_FILE_DIR:test_chain>
  )
  add_executable(test_llm tests/test_llm.cpp src/llm/LlmClient.cpp src/util/Settings.cpp)
  target_link_libraries(test_llm PRIVATE Qt6::Core Qt6::Network)
  target_include_directories(test_llm PRIVATE src)
  set_target_properties(test_llm PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  add_executable(test_update tests/test_update.cpp src/update/Updater.cpp)
  target_link_libraries(test_update PRIVATE Qt6::Core Qt6::Network)
  target_include_directories(test_update PRIVATE src)
  set_target_properties(test_update PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()
