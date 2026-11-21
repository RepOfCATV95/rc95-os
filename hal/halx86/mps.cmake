# Intel MultiProcessor Specification backend.
# Keep this separate from the generic legacy bus component: only HALMP
# consumes the MPS topology parser and PCI INTx route lookup.
add_library(lib_hal_mps OBJECT
    smp/mps/mps.c)
add_dependencies(lib_hal_mps bugcodes xdk)
