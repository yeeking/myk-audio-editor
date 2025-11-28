cmake_minimum_required(VERSION 3.18)

project(NonDestructiveEditorApp VERSION 0.1.0)

# Bring in JUCE and Tracktion modules from the repo
add_subdirectory(libs/tracktion_engine/modules/juce ${CMAKE_CURRENT_BINARY_DIR}/cmake_build_juce)
add_subdirectory(libs/tracktion_engine/modules ${CMAKE_CURRENT_BINARY_DIR}/cmake_build_tracktion)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

juce_add_gui_app(NonDestructiveEditorApp
    BUNDLE_ID "com.tracktion.NonDestructiveEditorApp"
    COMPANY_NAME "Tracktion"
    PRODUCT_NAME "NonDestructiveEditorApp"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Audio recording"
    DOCUMENT_EXTENSIONS tracktionedit
    PLUGINHOST_AU TRUE
    PLUGINHOST_VST3 TRUE)

juce_generate_juce_header(NonDestructiveEditorApp)

set(APP_SOURCES
    src/main.cpp
    src/NonDestructiveEditorComponent.h
    src/common_sources.cpp
)

target_sources(NonDestructiveEditorApp PRIVATE ${APP_SOURCES})

target_compile_definitions(NonDestructiveEditorApp PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_LADSPA=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

target_compile_options(NonDestructiveEditorApp PRIVATE
    "$<$<CONFIG:Release>:-Werror>")

target_link_libraries(NonDestructiveEditorApp PRIVATE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_warning_flags)

if(APPLE)
    target_compile_options(NonDestructiveEditorApp PRIVATE "-fno-aligned-allocation")
endif()
