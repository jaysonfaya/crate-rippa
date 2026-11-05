# CRATE RIPPA Desktop (JJ DA PLUG): VST3, AU, VST2 (personal use), Standalone. JUCE 8.0.10.
#   cmake -B build -DCMAKE_BUILD_TYPE=Release            (JUCE is fetched, or -DJUCE_DIR=<checkout>)
#   cmake --build build --config Release
# Options: CR_VST2=ON builds the VST2 plug-in (Windows .dll for MPC 2, macOS .vst) from the freely written ABI in vst2abi/.
#          CR_TESTS=ON builds the engine test and the host tests.
cmake_minimum_required(VERSION 3.22)
project(CrateRippaDesktop VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
endif()

option(CR_VST2 "Build the VST2 plug-in (for MPC 2 Desktop on Windows)" ON)
option(CR_TESTS "Build the tests" OFF)
option(CR_PLUGIN "Build the plug-in" ON)
set(JUCE_DIR "" CACHE PATH "A JUCE 8 checkout (empty: fetch JUCE 8.0.10)")

if(JUCE_DIR)
  add_subdirectory(${JUCE_DIR} juce EXCLUDE_FROM_ALL)
else()
  include(FetchContent)
  FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.10 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(juce)
endif()

set(CR_ENGINE_SOURCES
  Source/CrEngine.cpp
  Source/Tools.cpp
  Source/WinMFDecode.cpp
  Source/CrateStore.cpp)

# Linux machines without the X11 extension headers (the test machine): JUCE works without them
set(CR_LINUX_DEFS "")
if(UNIX AND NOT APPLE)
  include(CheckIncludeFile)
  check_include_file("X11/extensions/Xrandr.h" HAVE_XRANDR)
  check_include_file("X11/extensions/Xinerama.h" HAVE_XINERAMA)
  check_include_file("X11/Xcursor/Xcursor.h" HAVE_XCURSOR)
  check_include_file("alsa/asoundlib.h" HAVE_ALSA)
  check_include_file("curl/curl.h" HAVE_CURL)
  if(NOT HAVE_XRANDR)
    list(APPEND CR_LINUX_DEFS JUCE_USE_XRANDR=0)
  endif()
  if(NOT HAVE_XINERAMA)
    list(APPEND CR_LINUX_DEFS JUCE_USE_XINERAMA=0)
  endif()
  if(NOT HAVE_XCURSOR)
    list(APPEND CR_LINUX_DEFS JUCE_USE_XCURSOR=0)
  endif()
  if(NOT HAVE_ALSA)
    list(APPEND CR_LINUX_DEFS JUCE_ALSA=0)
  endif()
  if(HAVE_CURL)
    list(APPEND CR_LINUX_DEFS JUCE_USE_CURL=1)
  else()
    list(APPEND CR_LINUX_DEFS JUCE_USE_CURL=0)
  endif()
endif()

juce_add_binary_data(CrateRippaData SOURCES
  Resources/BebasNeue.ttf
  Resources/dog_logo.png
  Resources/jf_gold.png)

if(CR_PLUGIN)
  set(CR_FORMATS VST3 Standalone)
  if(APPLE)
    list(APPEND CR_FORMATS AU)
  endif()
  if(CR_VST2 AND (WIN32 OR APPLE OR CR_TESTS))
    juce_set_vst2_sdk_path(${CMAKE_CURRENT_SOURCE_DIR}/vst2abi)
    list(APPEND CR_FORMATS VST)
  endif()

  juce_add_plugin(CrateRippa
    PRODUCT_NAME "Crate Rippa"
    COMPANY_NAME "JJ DA PLUG"
    COMPANY_WEBSITE ""
    BUNDLE_ID "com.jjdaplug.craterippa"
    PLUGIN_MANUFACTURER_CODE JJdp
    PLUGIN_CODE JJcR
    FORMATS ${CR_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler
    VST2_CATEGORY kPlugCategSynth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE
    HARDENED_RUNTIME_ENABLED FALSE
    VERSION ${PROJECT_VERSION})

  target_sources(CrateRippa PRIVATE
    ${CR_ENGINE_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Ui.cpp
    Source/Tabs.cpp
    Source/CrateTab.cpp)

  target_compile_definitions(CrateRippa PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    ${CR_LINUX_DEFS})

  target_link_libraries(CrateRippa
    PRIVATE
      CrateRippaData
      juce::juce_audio_utils
      juce::juce_audio_formats
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_lto_flags
      juce::juce_recommended_warning_flags)
  if(WIN32)
    target_link_libraries(CrateRippa PRIVATE mfplat mfreadwrite mfuuid ole32)
  endif()
endif()

if(CR_TESTS)
  juce_add_console_app(cr_engine_test PRODUCT_NAME "cr_engine_test")
  target_sources(cr_engine_test PRIVATE test/engine_test.cpp ${CR_ENGINE_SOURCES})
  target_compile_definitions(cr_engine_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_MP3AUDIOFORMAT=1 ${CR_LINUX_DEFS})
  target_include_directories(cr_engine_test PRIVATE Source)
  target_link_libraries(cr_engine_test PRIVATE juce::juce_audio_formats juce::juce_audio_basics juce::juce_core
    PUBLIC juce::juce_recommended_config_flags)
  if(CR_SANITIZE)
    target_compile_options(cr_engine_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(cr_engine_test PRIVATE -fsanitize=address,undefined)
  endif()

  juce_add_console_app(cr_host_test PRODUCT_NAME "cr_host_test")
  target_sources(cr_host_test PRIVATE test/host_test.cpp)
  target_compile_definitions(cr_host_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_VST=0 ${CR_LINUX_DEFS})
  target_link_libraries(cr_host_test PRIVATE juce::juce_audio_processors juce::juce_audio_formats PUBLIC juce::juce_recommended_config_flags)

  add_executable(cr_vst2_test test/vst2_test.c)
  target_link_libraries(cr_vst2_test PRIVATE ${CMAKE_DL_LIBS})
endif()

if(CR_TESTS AND CR_PLUGIN)
  # renders every tab to a PNG without a window (for checking the look)
  juce_add_console_app(cr_ui_shot PRODUCT_NAME "cr_ui_shot")
  target_sources(cr_ui_shot PRIVATE test/ui_shot.cpp ${CR_ENGINE_SOURCES} Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Ui.cpp Source/Tabs.cpp Source/CrateTab.cpp)
  target_compile_definitions(cr_ui_shot PRIVATE JUCE_WEB_BROWSER=0 JucePlugin_VersionString="1.0.0" JucePlugin_Name="Crate Rippa" ${CR_LINUX_DEFS})
  target_include_directories(cr_ui_shot PRIVATE Source)
  target_link_libraries(cr_ui_shot PRIVATE CrateRippaData juce::juce_audio_utils juce::juce_audio_formats PUBLIC juce::juce_recommended_config_flags)
endif()
