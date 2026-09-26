# Bundled tools

These binaries let the kit build a disc without you installing a toolchain.
`iso2raw` and `extract_disc` are bundled for every platform. `xorriso` is
bundled only for Windows; on macOS and Linux you install it yourself
(`brew install xorriso` / `sudo apt-get install xorriso`).

| File | Upstream | License |
|------|----------|---------|
| win/xorriso.exe (+ cygwin dlls) | PeyTy/xorriso-exe-for-windows | GPLv3 — LICENSE-xorriso.txt |
| win/iso2raw.exe, lin/iso2raw, mac/{amd64,arm64}/iso2raw | sftwninja/iso2raw | see upstream release |
| win/extract_disc.exe, lin/extract_disc, mac/{amd64,arm64}/extract_disc | this project | same as this project |

Source for the GPL binaries is available from the upstream projects above.
