# Playback metadata examples

For firmware **2.0.2**.

These are sidecar templates; the corresponding tape images are not included.
Copy the template beside your tape, give it the same basename and select
`LOADER=AUTO` in PLAY settings.

| Template | Matching tape | Example selection |
|---|---|---|
| [EXAMPLE.MFI](EXAMPLE.MFI) | `EXAMPLE.MZF` | UL_MZ800 |
| [EXAMPLE.M2I](EXAMPLE.M2I) | `EXAMPLE.M12` | IC 1:2 |
| [EXAMPLE.MTI](EXAMPLE.MTI) | `EXAMPLE.MZT` | A separate profile for each `RECORD=n` |

For `GAME.M12`, rename `EXAMPLE.M2I` to `GAME.M2I`. M2I uses the same
single-record TYPE/SPEED syntax as MFI, without a `RECORD=` field. Edit the
profile to suit your program and target computer; M12 does not select a machine.

MZF/MFI and M12/M2I pairs can coexist with the same basename. Lookup never
substitutes MFI for M2I or vice versa. Manual loader selection overrides
metadata. Missing/invalid AUTO metadata uses NORMAL 1:1; an unsafe generated
loader also falls back to full NORMAL 1:1 timing. READY/the record selector
shows the effective profile. Sidecars are hidden in the firmware browser.

QuickDisk MZQ/QDF/QD uses manual loader selection or NORMAL 1:1 in AUTO;
these sidecars do not apply to QuickDisk images.

See [all supported TYPE/SPEED combinations](../guide/MFI_MTI_FORMAT.md).
