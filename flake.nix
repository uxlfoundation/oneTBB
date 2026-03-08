{
  description = "oneAPI Threading Building Blocks (oneTBB) - C++ parallel programming library";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        # Build oneTBB with all examples
        oneTBB-with-examples = pkgs.stdenv.mkDerivation rec {
          pname = "onetbb-with-examples";
          version = "2021.14.0";

          src = ./.;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
          ];

          buildInputs = with pkgs; [
            hwloc
          ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
            # X11 dependencies for GUI examples on Linux
            libx11
            libxext
          ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DTBB_EXAMPLES=ON"
            "-DTBB_TEST=OFF"
            "-DTBB_STRICT=OFF"
            "-DTBB_ENABLE_IPO=ON"
            "-DBUILD_SHARED_LIBS=ON"
            "-DTBBMALLOC_BUILD=ON"
            "-DTBB_INSTALL=ON"
          ];

          # Set parallel build jobs
          enableParallelBuilding = true;

          # Post-install: ensure examples are included
          postInstall = ''
            # Examples are built in the build directory
            # Copy example binaries to a dedicated examples output directory
            if [ -d "$PWD" ]; then
              mkdir -p $out/share/oneTBB/examples

              # Find and copy all example executables
              find . -type f -executable \
                -not -path "*/CMakeFiles/*" \
                -not -path "*/test/*" \
                -not -name "*.so*" \
                -not -name "*.dylib" \
                -not -name "*.a" \
                -not -name "cmake" \
                -not -name "ctest" \
                -not -name "cpack" \
                | while read -r exe; do
                  if file "$exe" | grep -q "executable"; then
                    cp "$exe" $out/share/oneTBB/examples/ || true
                  fi
                done
            fi

            # Copy example source code for reference
            cp -r ${src}/examples $out/share/oneTBB/examples-src
          '';

          # Fix RPATH for example binaries to remove /build/ references
          preFixup = ''
            # Fix RPATH for all example executables before the automatic fixup phase
            for exe in $out/share/oneTBB/examples/*; do
              if [ -f "$exe" ] && [ -x "$exe" ]; then
                # Use patchelf to set a proper RPATH pointing to the TBB libraries in $out
                ${pkgs.patchelf}/bin/patchelf --set-rpath "$out/lib:${pkgs.lib.makeLibraryPath [ pkgs.stdenv.cc.cc ]}" "$exe" 2>/dev/null || true
              fi
            done
          '';

          meta = with pkgs.lib; {
            description = "oneAPI Threading Building Blocks - Flexible C++ library for parallel programming";
            longDescription = ''
              oneTBB is a flexible C++ library that simplifies the work of adding
              parallelism to complex applications. It enables you to specify logical
              parallelism instead of threads, targets threading for performance, and
              emphasizes scalable, data parallel programming.

              This derivation builds oneTBB with all examples included.
            '';
            homepage = "https://github.com/uxlfoundation/oneTBB";
            license = licenses.asl20;
            platforms = platforms.unix;
            maintainers = [ ];
          };
        };

        # Development shell with all build tools
        devShell = pkgs.mkShell {
          inputsFrom = [ oneTBB-with-examples ];

          buildInputs = with pkgs; [
            # Development tools
            gdb
            valgrind
            clang-tools

            # Optional tools mentioned in documentation
            gawk
            doxygen

            # For Python bindings (optional)
            python3
            swig
          ];

          shellHook = ''
            echo "oneTBB development environment"
            echo "=============================="
            echo ""
            echo "Available commands:"
            echo "  cmake -B build -DTBB_EXAMPLES=ON -DTBB_TEST=ON"
            echo "  cmake --build build -j\$(nproc)"
            echo "  cmake --build build --target run_examples"
            echo ""
            echo "Build with Nix:"
            echo "  nix build .#oneTBB-with-examples"
            echo ""
          '';
        };

      in
      {
        packages = {
          oneTBB-with-examples = oneTBB-with-examples;
          default = oneTBB-with-examples;
        };

        devShells.default = devShell;

        # Apps to run examples easily
        apps = {
          list-examples = {
            type = "app";
            program = toString (pkgs.writeShellScript "list-examples" ''
              echo "Available oneTBB examples:"
              ls -1 ${oneTBB-with-examples}/share/oneTBB/examples/
            '');
          };
        };
      }
    );
}
