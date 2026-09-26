file_size() {
  stat -c%s "$1" 2>/dev/null || stat -f%z "$1"
}

platform_subdir() {
  if [ -n "${OS:-}" ]; then echo win; return; fi
  case "$(uname -s)" in
    Linux)  echo lin;;
    Darwin) echo mac;;
    *)      echo unknown;;
  esac
}

resolve_tool() {
  name="$1"; plat=$(platform_subdir)
  case "$name" in
    iso2raw)
      case "$plat" in
        win) echo "./bin/win/iso2raw.exe";;
        lin) echo "./bin/lin/iso2raw";;
        mac) if [ "$(uname -m)" = "arm64" ]; then echo "./bin/mac/arm64/iso2raw";
             else echo "./bin/mac/amd64/iso2raw"; fi;;
        *)   echo "ERROR: unsupported platform for iso2raw" >&2; return 1;;
      esac;;
    xorriso)
      if [ "$plat" = win ]; then echo "./bin/win/xorriso.exe"; return 0; fi
      sys=$(command -v xorriso 2>/dev/null || true)
      if [ -n "$sys" ]; then echo "$sys"; return 0; fi
      echo "ERROR: xorriso is not installed." >&2
      if [ "$plat" = mac ]; then echo "  Install it with: brew install xorriso" >&2;
      else echo "  Install it with: sudo apt-get install xorriso" >&2; fi
      return 1;;
    *) command -v "$name";;
  esac
}

inject_data() {
  base="$1"; ddir="$2"; out="$3"; name="$4"
  mkdir -p "$out"
  XORRISO=$(resolve_tool xorriso) || return 1
  ISO2RAW=$(resolve_tool iso2raw) || return 1
  inj="$out/${name}_injected.iso"

  dd if="$base" of="$out/ip.bin" bs=2048 count=16 2>/dev/null

  set -- -indev "$base" -outdev "$inj" -rockridge off -joliet off
  n=0; blobs=0; part1=0
  for f in "$ddir"/*; do
    [ -f "$f" ] || continue
    b=$(basename "$f" | tr 'a-z' 'A-Z')
    set -- "$@" -map "$f" "/$b"
    n=$((n + 1))
    case "$b" in
      0.BIN|ANOTHER.BIN|MEMLIST.BIN) ;;
      *.BIN) blobs=$((blobs + 1));;
    esac
    case "$b" in
      BANK??|MEMLIST.BIN) part1=$((part1 + 1));;
    esac
  done
  [ "$blobs" -eq 19 ] || { echo "ERROR: expected 19 Heart of the Alien data blobs in $ddir, found $blobs -- run data.bat first" >&2; return 1; }
  if [ "$part1" -eq 14 ]; then echo "Part I's data is present -- OUT OF THIS WORLD will be playable."
  else echo "Part I's data is absent ($part1 of 14) -- OUT OF THIS WORLD will not be playable on this disc."; fi

  if ! "$XORRISO" "$@" -commit >/dev/null 2>&1; then
    echo "ERROR: xorriso injection failed" >&2; return 1
  fi
  [ -s "$inj" ] && [ "$(file_size "$inj")" -gt 32768 ] || {
    echo "ERROR: xorriso produced no injected ISO (output missing or only IP.BIN-sized)" >&2; return 1; }

  dd if="$out/ip.bin" of="$inj" bs=2048 count=16 conv=notrunc 2>/dev/null

  head -c 32768 "$inj" > "$out/ip.check"
  cmp -s "$out/ip.bin" "$out/ip.check" || {
    echo "ERROR: IP.BIN not preserved after injection" >&2; return 1; }

  "$ISO2RAW" "$inj" -o "$out/${name}.bin" || { echo "ERROR: iso2raw conversion failed" >&2; return 1; }

  { printf 'FILE "%s.bin" BINARY\n' "$name";
    printf '  TRACK 01 MODE1/2352\n';
    printf '    INDEX 01 00:00:00\n'; } > "$out/${name}.cue"

  rm -f "$inj" "$out/ip.bin" "$out/ip.check"
  echo "Injected $n data files ($blobs Part II blobs) -> $out/${name}.bin"
}
