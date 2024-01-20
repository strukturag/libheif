import pytest
import subprocess

from config import Config

config = Config()

CONFORMANCE_FILES = [
  "C001.heic",
  "C002.heic",
  "C003.heic",
  "C004.heic",
  "C005.heic",
  "C006.heic",
  "C007.heic",
  "C008.heic",
  "C009.heic",
  "C010.heic",
  "C011.heic",
  "C012.heic",
  "C013.heic",
  "C014.heic",
  "C015.heic",
  "C016.heic",
  "C017.heic",
  "C018.heic",
  "C019.heic",
  "C020.heic",
  pytest.param("C021.heic", marks=pytest.mark.xfail(reason="No AVC support yet")),
  "C022.heic",
  "C023.heic",
  "C024.heic",
  "C025.heic",
  pytest.param("C026.heic", marks=pytest.mark.xfail(reason="No supported brands - image sequence")),
  pytest.param("C027.heic", marks=pytest.mark.xfail(reason="No supported brands - image sequence")),
  pytest.param("C028.heic", marks=pytest.mark.xfail(reason="No supported brands")),
  pytest.param("C029.heic", marks=pytest.mark.xfail(reason="No supported brands")),
  pytest.param("C030.heic", marks=pytest.mark.xfail(reason="No supported brands")),
  pytest.param("C031.heic", marks=pytest.mark.xfail(reason="No supported brands")),
  pytest.param("C032.heic", marks=pytest.mark.xfail(reason="No supported brands")),
  "C034.heic",
  pytest.param("C036.heic", marks=pytest.mark.xfail(reason="No supported brands")),
  pytest.param("C037.heic", marks=pytest.mark.xfail(reason="No supported brands")),
  pytest.param("C038.heic", marks=pytest.mark.xfail(reason="No supported brands")),
  "C039.heic",
  "C040.heic",
  pytest.param("C041.heic", marks=pytest.mark.xfail(reason="No supported brands")),
  "C042.heic",
  "C043.heic",
  "C044.heic",
  "C045.heic",
  "C046.heic",
  "C047.heic",
  "C048.heic",
  "C049.heic",
  "C050.heic",
  "C051.heic",
  "C052.heic",
  "C053.heic",
  "MIAF001.heic",
  "MIAF002.heic",
  "MIAF003.heic",
  "MIAF004.heic",
  "MIAF005.heic",
  "MIAF006.heic",
  "MIAF007.heic",
  "multilayer001.heic",
  pytest.param("multilayer002.heic", marks=pytest.mark.xfail(reason="L-HEVC image")),
  "multilayer003.heic",
  pytest.param("multilayer004.heic", marks=pytest.mark.xfail(reason="No AVC support yet")),
  "multilayer005.heic"
]

helptext = """\
 heif-info  libheif version: {version}
------------------------------------
usage: heif-info [options] image.heic

options:
  -d, --dump-boxes     show a low-level dump of all MP4 file boxes
  -h, --help           show help
  -v, --version        show version
""".format(version=config.heif_version())


def test_help_output():
    ret = subprocess.run(["../examples/heif-info", "--help"], capture_output=True, text=True)
    assert ret.stderr == helptext

@pytest.mark.parametrize("filename", CONFORMANCE_FILES)
def test_info(filename):
    ret = subprocess.run(["../examples/heif-info", config.conformance_file_path(filename)], capture_output=True, text=True)
    with open(config.getExpectedFileInfo(filename)) as f:
      expected = f.read()
    assert ret.stdout == expected
    assert ret.stderr == ""
    

@pytest.mark.parametrize("filename", CONFORMANCE_FILES)
def test_dump(filename):
    ret = subprocess.run(["../examples/heif-info", "--dump-boxes", config.conformance_file_path(filename)], capture_output=True, text=True)
    with open(config.getExpectedFileDump(filename)) as f:
      expected = f.read()
    assert ret.stdout == expected
    assert ret.stderr == ""