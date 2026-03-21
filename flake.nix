{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
  inputs.flake-utils.url = "github:numtide/flake-utils";
  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
        };

        pname = "meson-template";
        version = "0.0.0";
        meta = with pkgs.lib; {
          description = "meson flake template";
          homepage = "";
          maintainers = with maintainers; [ ];
          platforms = platforms.all;
        };

        src = ./.;

        nativeBuildInputs = with pkgs; [
          meson
          ninja
          pkg-config
        ];

        buildInputs = with pkgs; [
          grub2
        ];

        buildDir = "build";

        package = pkgs.stdenv.mkDerivation {
            inherit system pname version src nativeBuildInputs buildInputs;
            configurePhase = ''
              rm -rf ${buildDir}
              mkdir -p $out
              meson setup ${buildDir} ${src} --prefix $out
            '';
            buildPhase = ''
              meson compile -C ${buildDir}
            '';
            installPhase = ''
              meson install -C ${buildDir}
            '';
        };

      in
      {
        packages.${pname} = package;
        packages.default = package;

        devShells.default = pkgs.mkShell {
            inherit nativeBuildInputs buildInputs;
        };
      });
}
