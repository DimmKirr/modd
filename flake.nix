{
  description = "modd firmware build environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        isAarch64 = pkgs.stdenv.hostPlatform.isAarch64;
        isLinux = pkgs.stdenv.hostPlatform.isLinux;
        x86Glibc = pkgs.pkgsCross.gnu64.glibc;
        x86GccLib = pkgs.pkgsCross.gnu64.stdenv.cc.cc.lib;
      in {
        devShells.default = pkgs.mkShell {
          buildInputs = [
            pkgs.qemu
            pkgs.libusb1
          ] ++ pkgs.lib.optionals isLinux [
            pkgs.fribidi
          ];
          # x86Glibc and x86GccLib are deliberately not buildInputs: that would put
          # the x86-64 glibc headers on the native cc's include path and break the
          # host-compiled C harnesses in tests/ (gnu/stubs-32.h not found). The
          # shellHook below references them, which is enough to pull them in.

          # Pillow's wheel loads libfribidi.so.0 at runtime; without it raqm is
          # off and the upstream font generators (gen_aa_font.py of Felucca and
          # Melodee, run on the first build of a defconfig and by `task docs`)
          # fail with "not supported without libraqm". uv installs the wheel,
          # so the library has to come from here.
          shellHook = pkgs.lib.optionalString isLinux ''
            export LD_LIBRARY_PATH="${pkgs.fribidi}/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
          '' + pkgs.lib.optionalString isAarch64 ''
            # tools/build/build.py reads these two first when it picks the x86-64
            # loader and libstdc++ for the JieLi compiler. Without them it globs
            # /nix/store and can pair another glibc with the QEMU_SET_ENV below,
            # which fails with "symbol lookup error ... __pointer_chk_guard".
            export JIELI_X86_GLIBC="${x86Glibc}"
            export JIELI_X86_STDCPP="${x86GccLib}"
            export QEMU_LD_PREFIX="${x86Glibc}"
            export QEMU_SET_ENV="LD_LIBRARY_PATH=${x86Glibc}/lib:${x86GccLib}/lib"
          '';
        };
      });
}
