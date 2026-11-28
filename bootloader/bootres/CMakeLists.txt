# RC95-OS-owned resource-only bootres.dll.
# Keep the resource aliases stable: the source animation assets retain their
# historical hyphenated filenames, while the embedded resource names do not.
add_library(bootres MODULE bootres.rc)
set_target_properties(bootres PROPERTIES
    PREFIX ""
    OUTPUT_NAME "bootres"
)

if(MSVC)
    target_link_options(bootres PRIVATE "/NOENTRY")
else()
    target_link_options(bootres PRIVATE "-Wl,--no-entry")
endif()

# Ship the resource DLL beside the RC95-OS system binaries. WINLOAD opens it
# relative to the selected installation's SystemRoot, not the boot volume.
add_cd_file(TARGET bootres DESTINATION reactos/system32 FOR all)
