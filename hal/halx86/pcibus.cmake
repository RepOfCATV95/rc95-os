# PCI bus support is shared by legacy HALs, but HALMP needs a build
# variant that can resolve firmware-provided MPS PCI INTx routes.
add_library(lib_hal_pcibus OBJECT
    legacy/bus/pcibus.c)
add_dependencies(lib_hal_pcibus bugcodes xdk)

add_library(lib_hal_pcibus_mps OBJECT
    legacy/bus/pcibus.c)
target_compile_definitions(lib_hal_pcibus_mps PRIVATE HALMP_MPS_PCI_ROUTING)
add_dependencies(lib_hal_pcibus_mps bugcodes xdk)
