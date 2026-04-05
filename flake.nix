{
  description = "Job Management System";
  inputs.flake-utils.url = "github:numtide/flake-utils";
  
  outputs = { self, nixpkgs, flake-utils }: {
    overlays.default = final: prev: {
      jms = final.callPackage ({stdenv, pkgs, ...}:
        stdenv.mkDerivation {
          src = ./.;
          pname = "jms";
          version = "unstable";
          buildInputs = with pkgs; [
            gnumake
            gcc
            unzip
            bear
          ];

          installPhase = ''
          mkdir -p $out/bin/
          cp jms_coord $out/bin/jms_coord
          '';
        }
      ) {};
    };
  } // # We now add the package defined in the above overlay for all architectures
  (flake-utils.lib.eachDefaultSystem (system:
    let
      pkgs = import nixpkgs {
        system = system;
        overlays = [ self.overlays.default ];
      };
    in
      {
        # Create a new package
        packages = {
          jms = pkgs.jms;
          default = self.packages.${system}.jms;
        };
      }
  ));
}
