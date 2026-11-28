# Native Selawik bitmap-font asset generator.
add_executable(bootfontgen bootfontgen.c)
target_link_libraries(bootfontgen PRIVATE host_freetype)
target_compile_definitions(bootfontgen PRIVATE
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS
)
set_target_properties(bootfontgen PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${TOOLS_FOLDER}
)
