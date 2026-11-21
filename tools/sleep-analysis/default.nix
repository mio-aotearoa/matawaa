{
  lib,
  fetchFromGitHub,
  python3,
  gfortran,
  writeShellApplication,
  runCommand,
}:
let
  skdh = python3.pkgs.buildPythonPackage {
    pname = "scikit-digital-health";
    version = "0.17.18";
    pyproject = true;
    src = fetchFromGitHub {
      owner = "pfizer-opensource";
      repo = "scikit-digital-health";
      rev = "5c65ba36c2cb1993d56a4788e1a8019d9c45b8f2";
      hash = "sha256-4jItDZelUrd4t9io7m71IXSoe4bA2ayP1u9wa9iLatU=";
    };
    nativeBuildInputs = [ gfortran ];
    build-system = with python3.pkgs; [
      meson-python
      numpy
    ];
    dependencies = with python3.pkgs; [
      numpy
      scipy
      pandas
      lightgbm
      pywavelets
      scikit-learn
      h5py
      matplotlib
      packaging
      pyyaml
      avro
    ];
    nativeCheckInputs = with python3.pkgs; [ pytestCheckHook ];
    pytestFlags = [ "tests/sleep" ];
    pythonImportsCheck = [ "skdh.sleep" ];
    meta = {
      description = "Upstream research algorithms for wearable inertial data";
      homepage = "https://github.com/pfizer-opensource/scikit-digital-health";
      license = lib.licenses.mit;
    };
  };
  python = python3.withPackages (_: [ skdh ]);
in
{
  inherit skdh;
  analysisTests =
    runCommand "matawaa-sleep-analysis-tests"
      {
        nativeBuildInputs = [ python ];
        MATAWAA_ANALYSIS_MODULE = ./analyze.py;
      }
      ''
            export MPLCONFIGDIR="$TMPDIR/matplotlib"
        python ${../../tests/test_sleep_analysis.py} -v
            touch "$out"
      '';
  analyzeSleep = writeShellApplication {
    name = "matawaa-analyze-sleep";
    runtimeInputs = [ python ];
    text = ''exec python ${./analyze.py} "$@"'';
  };
}
