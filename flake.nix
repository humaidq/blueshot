# Copyright 2026 Humaid Alqasimi
# SPDX-License-Identifier: Apache-2.0
{
  description = "Blueshot Linux editor development environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    let
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      mkPackages =
        pkgs:
        let
          dotnetSdk = pkgs.dotnetCorePackages.sdk_9_0;
          dotnetRuntime = pkgs.dotnetCorePackages.runtime_9_0;

          runtimeDeps = with pkgs; [
            fontconfig
            freetype
            libGL
            libice
            libsm
            libx11
            libxkbcommon
            libxext
            libxi
            libxrandr
            libxrender
            wayland
          ];

          blueshotAvalonia = pkgs.buildDotnetModule {
            pname = "blueshot-avalonia";
            version = "0.1.0";
            src = ./.;
            projectFile = "src/Blueshot.Editor.Linux/Blueshot.Editor.Linux.csproj";
            executables = [ "Blueshot.Editor.Linux" ];
            dotnet-sdk = dotnetSdk;
            dotnet-runtime = dotnetRuntime;
            runtimeDeps = runtimeDeps;
            nugetDeps = ./nix/nuget-deps.json;
            selfContainedBuild = false;
            dotnetBuildFlags = [ "--property:ContinuousIntegrationBuild=true" ];
            dotnetInstallFlags = [ "--property:ContinuousIntegrationBuild=true" ];
            meta = {
              description = "Standalone Linux port of the Blueshot editor shell";
              mainProgram = "Blueshot.Editor.Linux";
              platforms = supportedSystems;
            };
          };

          blueshot = pkgs.stdenv.mkDerivation {
            pname = "blueshot";
            version = "0.1.0";
            src = ./.;

            nativeBuildInputs = [
              pkgs.cmake
              pkgs.makeBinaryWrapper
              pkgs.ninja
              pkgs.pkg-config
              pkgs.qt6.wrapQtAppsHook
            ];

            buildInputs = [
              pkgs.qt6.qtbase
              pkgs.qt6.qtwayland
            ];

            qtWrapperArgs = [
              "--set-default"
              "QT_QPA_PLATFORM"
              "wayland;xcb"
            ];

            configurePhase = ''
              runHook preConfigure
              cmake -S src/Blueshot.Editor.Qt -B build -GNinja
              runHook postConfigure
            '';

            buildPhase = ''
              runHook preBuild
              cmake --build build
              runHook postBuild
            '';

            installPhase = ''
              runHook preInstall
              cmake --install build --prefix "$out"
              mv "$out/bin/Blueshot.Editor.Qt" "$out/bin/.Blueshot.Editor.Qt-wrapped"
              makeBinaryWrapper "$out/bin/.Blueshot.Editor.Qt-wrapped" "$out/bin/blueshot" \
                --set-default QT_QPA_PLATFORM "wayland;xcb"
              runHook postInstall
            '';

            meta = {
              description = "Standalone Linux port of the Blueshot editor using Qt6";
              mainProgram = "blueshot";
              platforms = supportedSystems;
            };
          };
        in
        {
          inherit blueshot blueshotAvalonia dotnetSdk;
        };
    in
    (flake-utils.lib.eachSystem supportedSystems (
      system:
      let
        pkgs = import nixpkgs {
          inherit system;
        };

        packagesForSystem = mkPackages pkgs;
      in
      {
        packages = {
          default = packagesForSystem.blueshot;
          blueshot = packagesForSystem.blueshot;
          blueshot-avalonia = packagesForSystem.blueshotAvalonia;
        };

        apps = {
          default = flake-utils.lib.mkApp {
            drv = packagesForSystem.blueshot;
            exePath = "/bin/blueshot";
          };
          blueshot = flake-utils.lib.mkApp {
            drv = packagesForSystem.blueshot;
            exePath = "/bin/blueshot";
          };
          blueshot-avalonia = flake-utils.lib.mkApp {
            drv = packagesForSystem.blueshotAvalonia;
            exePath = "/bin/Blueshot.Editor.Linux";
          };
          fetch-deps = {
            type = "app";
            program = toString packagesForSystem.blueshotAvalonia."fetch-deps";
          };
        };

        devShells.default = pkgs.mkShell {
          packages = [
            packagesForSystem.dotnetSdk
            pkgs.icu
            pkgs.libGL
            pkgs.libx11
            pkgs.libxkbcommon
            pkgs.cmake
            pkgs.ninja
            pkgs.nixfmt
            pkgs.pkg-config
            pkgs.qt6.qtbase
            pkgs.qt6.qtwayland
            pkgs.wayland
            pkgs.libxrandr
          ];

          shellHook = ''
            export DOTNET_CLI_TELEMETRY_OPTOUT=1
            export DOTNET_NOLOGO=1
          '';
        };

        formatter = pkgs.nixfmt;
      }
    ))
    // {
      overlays = {
        default = final: _prev: {
          blueshot = (mkPackages final).blueshot;
        };
        blueshot = self.overlays.default;
      };
    };
}
