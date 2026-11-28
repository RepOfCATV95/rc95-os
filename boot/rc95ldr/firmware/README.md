# RC95LDR firmware restart action

The native BIOS and UEFI candidate menus append **Restart computer** after the
discovered boot candidates. It is an action, not a BCD object, so selecting it
does not return a candidate to the OS-loader dispatch path.

The common firmware-menu contract exposes the action through
`RC95LDR_FIRMWARE_MENU_OPS.RestartSystem` and `RestartContext`. Candidate-menu
callers must bind the callback before displaying the menu:

- BIOS: `Rc95LdrBiosBindFirmwareMenuOps` (also performed by
  `Rc95LdrBiosRunCandidateMenu`) binds the BIOS interrupt service. The adapter
  invokes INT 19h to restart the BIOS bootstrap sequence.
- UEFI: `Rc95LdrUefiBindFirmwareMenuOps` binds the active image's EFI runtime
  services. The adapter calls `ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL)`.

UEFI reset is firmware-managed and should not return. If a reset service does
return, the adapter reports an I/O failure. Likewise, if the BIOS bootstrap
interrupt unexpectedly returns successfully, the adapter reports an I/O
failure rather than treating the action as a boot candidate.

The text UI currently supports at most 12 entries. Since one entry is reserved
for Restart computer, candidate menus accept at most 11 discovered candidates.
The lower-level generic boot-menu API remains unchanged and does not add a
restart entry automatically.
