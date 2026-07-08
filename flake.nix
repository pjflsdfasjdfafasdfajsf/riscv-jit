{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    zig2nix.url = "github:Cloudef/zig2nix";
    zig2nix.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs = { zig2nix, ... }: let
    flake-utils = zig2nix.inputs.flake-utils;
  in (flake-utils.lib.eachDefaultSystem (system: let
      env = zig2nix.outputs.zig-env.${system} {
        zig = zig2nix.outputs.packages.${system}.zig-0_16_0;
      };
    in with builtins; with env.pkgs.lib; rec {
      # Produces clean binaries meant to be ship'd outside of nix
      # nix build .#foreign
      packages.foreign = env.package {
        pname = "riscv-jit";
        version = "1.0.0";
        src = cleanSource ./.;

        zigBuildZon = ./build.zig.zon;
        zigBuildZonLock = ./build.zig.zon2json-lock;

        zigBuildFlags = [ "-Doptimize=ReleaseSmall" ];

        # Packages required for compiling
        nativeBuildInputs = with env.pkgs; [ pkg-config wayland-scanner ];

        # Packages required for linking
        buildInputs = with env.pkgs; [ libGL wayland libxkbcommon ];

        # Smaller binaries and avoids shipping glibc.
        zigPreferMusl = true;
      };

      # nix build .
      packages.default = packages.foreign.override (attrs: {
        # Prefer nix friendly settings.
        zigPreferMusl = false;
      });

      # nix run .#build
      apps.build = env.app [] "zig build \"$@\"";

      # nix run .#test
      apps.test = env.app [] "zig build test -- \"$@\"";

      # nix run .#zig2nix
      apps.zig2nix = env.app [] "zig2nix \"$@\"";

      # nix develop
      devShells.default = env.mkShell {
        # Packages required for compiling, linking and running
        # Libraries added here will be automatically added to the LD_LIBRARY_PATH and PKG_CONFIG_PATH
        nativeBuildInputs = [
          env.pkgs.clang-tools
          env.pkgs.gdb
          # RISC-V Sail reference model
          env.pkgs.sail-riscv
        ]
          ++ (with env.pkgs.pkgsCross.riscv64-embedded.buildPackages; [ binutils ])
          ++ packages.default.nativeBuildInputs
          ++ packages.default.buildInputs;
      };
    }));
}
