cmake_minimum_required(VERSION 3.22)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" release_version LIMIT_COUNT 1)
project(HandpanStudio VERSION ${release_version} LANGUAGES C CXX)
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../JUCE-9.0.2" CACHE PATH "Path to the JUCE 9.0.2 source tree")
add_subdirectory("${JUCE_DIR}" juce EXCLUDE_FROM_ALL)
file(WRITE "${CMAKE_BINARY_DIR}/sonform-juce-source.txt" "${JUCE_DIR}\n")
set(product_data "${CMAKE_CURRENT_SOURCE_DIR}/data")
juce_add_binary_data(HandpanData SOURCES
    "${product_data}/zones.csv" "${product_data}/bank.csv"
    "${product_data}/strike_texture.csv" "${product_data}/strike_tail.csv")
juce_add_plugin(HandpanStudio
    COMPANY_NAME "WTdusk"
    BUNDLE_ID "com.sonform.void"
    PLUGIN_MANUFACTURER_CODE Hpmd
    PLUGIN_CODE Hpst
    FORMATS VST3 Standalone
    PRODUCT_NAME "SONFORM Void"
    PLUGIN_NAME "SONFORM: Void"
    IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE NEEDS_CURL FALSE)
if(TARGET HandpanStudio_rc_lib)
    # JUCE 9.0.2 does not track Info.txt changes for the Windows resource.
    get_target_property(resource_dir HandpanStudio JUCE_GENERATED_SOURCES_DIRECTORY)
    get_target_property(resource_info HandpanStudio JUCE_INFO_FILE)
    add_custom_command(OUTPUT "${resource_dir}/HandpanStudio_resources.rc"
        APPEND DEPENDS "${resource_info}")
endif()
target_sources(HandpanStudio_Standalone PRIVATE StandaloneApp.cpp)
target_compile_definitions(HandpanStudio_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
set_target_properties(HandpanStudio_Standalone PROPERTIES OUTPUT_NAME "SONFORM Void ${PROJECT_VERSION}")
target_sources(HandpanStudio PRIVATE Plugin.cpp ../src/HandpanEngine.cpp)
target_compile_features(HandpanStudio PRIVATE cxx_std_17)
target_compile_definitions(HandpanStudio PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(HandpanStudio PRIVATE HandpanData juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
juce_add_console_app(HandpanPluginChecks PRODUCT_NAME "Handpan Plugin Checks")
target_sources(HandpanPluginChecks PRIVATE PluginChecks.cpp)
target_compile_features(HandpanPluginChecks PRIVATE cxx_std_17)
target_compile_definitions(HandpanPluginChecks PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(HandpanPluginChecks PRIVATE HandpanStudio HandpanData juce::juce_audio_utils)
enable_testing()
add_test(NAME plugin_checks COMMAND HandpanPluginChecks)

# A separate host loads the built VST3, including its embedded data and wrapper.
juce_add_console_app(HandpanPluginRender PRODUCT_NAME "Handpan Plugin Render")
target_sources(HandpanPluginRender PRIVATE PluginRender.cpp)
target_compile_features(HandpanPluginRender PRIVATE cxx_std_17)
target_compile_definitions(HandpanPluginRender PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(HandpanPluginRender PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
