{
  lib,
  stdenv,
  makeWrapper,
  kineticWe,
  noctalia,
  xwayland,
  kdePackages,
  psmisc,
  procps,
  dbus,
  python3,
  qt6,
  qt6Packages,
}:

let
  # Qt plugin + QML import paths, built from the Qt6 + KDE packages that
  # provide the modules the session's apps load (org.kde.plasma.core and
  # org.kde.kquickcontrols are both shipped by libplasma). Qt ignores any
  # component that has no such dir, so listing a package without
  # lib/qt-6/qml is harmless. qt6ct supplies the platform theme the launcher
  # selects via QT_QPA_PLATFORMTHEME (avoids the crashing QKdeTheme path).
  qtDeps = [
    qt6.qtbase
    qt6.qtdeclarative
    qt6.qtsvg
    qt6.qtwayland
    qt6Packages.qt6ct
    kdePackages.libplasma
    kdePackages.plasma-workspace
    kdePackages.kirigami
    kdePackages.kconfig
    kdePackages.kcoreaddons
    kdePackages.kwindowsystem
    kdePackages.ki18n
    kdePackages.kiconthemes
  ];
  qtPluginPath = lib.concatStringsSep ":" (lib.map (p: "${p}/lib/qt-6/plugins") qtDeps);
  qmlImportPath = lib.concatStringsSep ":" (lib.map (p: "${p}/lib/qt-6/qml") qtDeps);
in
stdenv.mkDerivation {
  pname = "kineticwe-session";
  version = "unstable";

  dontUnpack = true;
  dontConfigure = true;
  dontBuild = true;

  nativeBuildInputs = [ makeWrapper ];
  # Declared so the QML/plugin paths embedded below reference packages that
  # are guaranteed to be in the store when the session runs.
  buildInputs = qtDeps;
  # This derivation depends on qtbase (via buildInputs) but is not a Qt app —
  # it's a bash session launcher that sets the Qt env manually via wrapProgram.
  # Tell the qt build system to skip its app-wrapping requirement.
  dontWrapQtApps = true;

  installPhase = ''
    runHook preInstall

    mkdir -p "$out/bin" "$out/share/wayland-sessions" \
      "$out/share/kineticwe" "$out/lib/kineticwe"

    # Desktop entry so SDDM / greetd can list KineticWE as a Wayland session.
    # The basename (KineticWE) must match passthru.providedSessions below.
    cat > "$out/share/wayland-sessions/KineticWE.desktop" << DESKTOPEOF
[Desktop Entry]
Name=KineticWE
Comment=KineticWE Wayland session
Type=Application
Exec=$out/bin/start-kineticwe
DesktopNames=KineticWE
DESKTOPEOF

    # Canonical 2.0 launcher (single source of truth). @INSTALL_PREFIX@ is
    # substituted with this package so the prefix-relative paths resolve.
    install -Dm755 ${../scripts/start-kineticwe.sh} "$out/bin/start-kineticwe"
    substituteInPlace "$out/bin/start-kineticwe" \
      --replace-fail "@INSTALL_PREFIX@" "$out" \
      --replace-fail '$INSTALL_PREFIX/bin/kinetic-we' '${kineticWe}/bin/kinetic-we'

    # Config migration/seeding and systemd user units (distro install layout).
    install -Dm755 ${../scripts/kineticwe-config-setup} \
      "$out/lib/kineticwe/kineticwe-config-setup"
    install -Dm644 ${../scripts/kineticwe-session.target} \
      "$out/share/kineticwe/kineticwe-session.target"
    install -Dm644 ${../scripts/kineticwe-shell.service.in} \
      "$out/share/kineticwe/kineticwe-shell.service"
    substituteInPlace "$out/share/kineticwe/kineticwe-shell.service" \
      --replace-fail "ExecStart=@INSTALL_PREFIX@/bin/noctalia-kwe" \
                     "ExecStart=${noctalia}/bin/noctalia-kwe"

    # Wrap so launcher helpers are on PATH, and set the Qt plugin + QML
    # import environment (QT_PLUGIN_PATH, NIXPKGS_QT6_QML_IMPORT_PATH) so the
    # KDE apps this session spawns resolve their QML modules.
    wrapProgram "$out/bin/start-kineticwe" \
      --prefix PATH : "${lib.makeBinPath [
        xwayland
        psmisc
        procps
        dbus
        python3
        noctalia
        kdePackages.kservice
      ]}" \
      --prefix QT_PLUGIN_PATH : "${qtPluginPath}" \
      --prefix NIXPKGS_QT6_QML_IMPORT_PATH : "${qmlImportPath}"

    runHook postInstall
  '';

  # Required by services.displayManager.sessionPackages — basename of the
  # wayland-sessions desktop file without .desktop.
  passthru.providedSessions = [ "KineticWE" ];

  meta = with lib; {
    description = "KineticWE session entry (start-kineticwe launcher)";
    license = licenses.gpl2Plus;
    platforms = platforms.linux;
    mainProgram = "start-kineticwe";
  };
}
