{
  lib,
  stdenv,
  makeWrapper,
  kwinWe,
  noctalia,
  coreutils,
  findutils,
  gnused,
  gnugrep,
  systemd,
  xdg-desktop-portal,
  kdePackages,
  xwayland,
}:
stdenv.mkDerivation {
  pname = "kineticwe-session";
  version = "unstable";

  dontUnpack = true;
  dontConfigure = true;
  dontBuild = true;

  nativeBuildInputs = [ makeWrapper ];

  installPhase = ''
    runHook preInstall

    mkdir -p "$out/bin"
    cat > "$out/bin/start-kineticwe" << 'SCRIPTEOF'
#!/bin/bash
# KineticWE — session startup script (Nix-built).
set -e

KWIN_WE_BIN="${kwinWe}/bin/kinetic-we"
NOCTALIA_BIN="${noctalia}/bin/noctalia"
XDP_BIN="${xdg-desktop-portal}/libexec/xdg-desktop-portal"
XDP_KDE_BIN="${kdePackages.xdg-desktop-portal-kde}/libexec/xdg-desktop-portal-kde"

export PATH="${lib.makeBinPath [ coreutils findutils gnused gnugrep systemd ]}:$PATH"
export QT_PLUGIN_PATH="''${QT_PLUGIN_PATH:-}:${kwinWe}/lib/qt6/plugins"
export QML2_IMPORT_PATH="''${QML2_IMPORT_PATH:-}:${kwinWe}/lib/qt6/qml"
export QML_IMPORT_PATH="''${QML_IMPORT_PATH:-}:${kwinWe}/lib/qt6/qml"
export XDG_CURRENT_DESKTOP="''${XDG_CURRENT_DESKTOP:-KDE}"
export XDG_SESSION_TYPE="''${XDG_SESSION_TYPE:-wayland}"
export XDG_SESSION_DESKTOP="''${XDG_SESSION_DESKTOP:-KDE}"

STARTUP_PAYLOAD_DIR="''${XDG_RUNTIME_DIR:-/tmp}/kineticwe-''${USER:-user}"
mkdir -p "$STARTUP_PAYLOAD_DIR"
STARTUP_PAYLOAD="$STARTUP_PAYLOAD_DIR/startup.sh"

cat > "$STARTUP_PAYLOAD" << PAYLOAD
#!/bin/bash
set -e

export PATH="''${PATH}"

pkill -x xdg-desktop-portal-kde  2>/dev/null || true
pkill -x xdg-desktop-portal     2>/dev/null || true
pkill -x xdg-desktop-portal-gtk 2>/dev/null || true
pkill -x xdg-document-portal    2>/dev/null || true
sleep 1

mkdir -p "\$HOME/.local/share"

if [[ -x "$XDP_KDE_BIN" ]]; then
    nohup "$XDP_KDE_BIN" >"\$HOME/.local/share/xdg-desktop-portal-kde.log" 2>&1 &
fi
sleep 2
if [[ -x "$XDP_BIN" ]]; then
    nohup "$XDP_BIN" >"\$HOME/.local/share/xdg-desktop-portal.log" 2>&1 &
fi

if [[ -x "$NOCTALIA_BIN" ]]; then
    exec "$NOCTALIA_BIN"
else
    echo "kineticwe: noctalia binary not found at $NOCTALIA_BIN" >&2
    exec bash
fi
PAYLOAD

chmod +x "$STARTUP_PAYLOAD"
exec "$KWIN_WE_BIN" --xwayland "$STARTUP_PAYLOAD"
SCRIPTEOF
    chmod +x "$out/bin/start-kineticwe"

    # Wrap so kwin-we finds its own plugins, QML modules, and the
    # noctalia + portal binaries without callers having to set anything.
    wrapProgram "$out/bin/start-kineticwe" \
      --prefix PATH : "${lib.makeBinPath [ noctalia xdg-desktop-portal kdePackages.xdg-desktop-portal-kde ]}" \
      --prefix QT_PLUGIN_PATH : "${kwinWe}/lib/qt6/plugins" \
      --prefix QML2_IMPORT_PATH : "${kwinWe}/lib/qt6/qml" \
      --prefix QML_IMPORT_PATH  : "${kwinWe}/lib/qt6/qml" \
      --suffix LD_LIBRARY_PATH : "${kwinWe}/lib"

    runHook postInstall
  '';

  meta = with lib; {
    description = "KineticWE session entry (start-kineticwe wrapper)";
    license = licenses.gpl2Plus;
    platforms = platforms.linux;
    mainProgram = "start-kineticwe";
  };
}
