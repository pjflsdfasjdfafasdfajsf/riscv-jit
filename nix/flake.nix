{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";

    zig-overlay = {
      url = "github:mitchellh/zig-overlay";
      inputs.nixpkgs.follows = "nixpkgs";
    };

    zls-overlay = {
      url = "github:zigtools/zls/0.16.0";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, nixpkgs, zig-overlay, zls-overlay }:
    let
      supportedSystems = [
        "x86_64-linux"
      ];

      forAllSystems = f: nixpkgs.lib.genAttrs supportedSystems (system: f {
        pkgs = import nixpkgs { inherit system; };
        
        zig = zig-overlay.packages.${system}."0.16.0"; 
        zls = zls-overlay.packages.${system}.zls;
        
        inherit system;
      });
    in
    {
      packages = forAllSystems ({ pkgs, zig, ... }: 
        let
          libs = [
            pkgs.libGL
            pkgs.wayland
            pkgs.libxkbcommon
          ];
        in
        {
          default = pkgs.stdenv.mkDerivation {
            pname = "riscv-jit";
            version = "0.1.0";

            src = ./.;

            nativeBuildInputs = [ 
              zig 
              pkgs.pkg-config
              pkgs.wayland-scanner
            ];

            buildInputs = libs;

            preBuild = ''
              export ZIG_GLOBAL_CACHE_DIR=$TMPDIR/zig-cache
            '';

            buildPhase = ''
              zig build -Doptimize=ReleaseSmall --prefix $out
            '';
          };
        }
      );

      devShells = forAllSystems ({ pkgs, zig, zls, ... }: 
        let
          libs = [
            pkgs.libGL
            pkgs.wayland
            pkgs.libxkbcommon
          ];
        in
        {
          default = pkgs.mkShell {
            nativeBuildInputs = [
              zig
              # dependencies
              pkgs.pkg-config
              pkgs.wayland-scanner
              # debug tools
              pkgs.gdb
              pkgs.pkgsCross.riscv64.buildPackages.gdb
              pkgs.pkgsCross.riscv64-embedded.buildPackages.binutils
              # lsp
              zls
              pkgs.clang-tools
            ];

            buildInputs = libs;
            shellHook = ''
              export ZIG_GLOBAL_CACHE_DIR="$PWD/.zig-cache"
              export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath libs}:$LD_LIBRARY_PATH"
            '';
          };
        }
      );
    };
}
