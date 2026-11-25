cmake_minimum_required(VERSION 3.22)

# --- macOS: binario universal (Intel + Apple Silicon) y la versión más baja que soporta Xcode moderno
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Arquitecturas" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "macOS mínimo" FORCE)

project(ECAudio VERSION 1.0.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(ECAudio
    PRODUCT_NAME "EC Audio"
    COMPANY_NAME "EC"
    BUNDLE_ID    com.ec.audio
    VERSION      1.0.0
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "EC Audio necesita el micrófono para grabar y monitorear tu voz."
    HARDENED_RUNTIME_ENABLED TRUE
    HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input com.apple.security.cs.disable-library-validation)

juce_generate_juce_header(ECAudio)

target_sources(ECAudio PRIVATE
    Source/Main.cpp
    Source/Engine.cpp
    Source/Engine.h
    Source/Theme.h
    Source/UI.h)

target_compile_features(ECAudio PRIVATE cxx_std_17)

target_compile_definitions(ECAudio PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_APPLICATION_NAME_STRING="EC Audio"
    JUCE_APPLICATION_VERSION_STRING="1.0.0")

target_link_libraries(ECAudio PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
