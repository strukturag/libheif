import subprocess

from config import Config

APP_NAME = "heif-convert"
APP_PATH = "../examples/" + APP_NAME

config = Config()

helptext = """\
 {appname}  libheif version: {version}
-------------------------------------------
Usage: {appname} [options]  <input-image> [output-image]

The program determines the output file format from the output filename suffix.
These suffixes are recognized: jpg, jpeg, png, y4m. If no output filename is specified, 'jpg' is used.

Options:
  -h, --help                     show help
  -v, --version                  show version
  -q, --quality                  quality (for JPEG output)
  -o, --output FILENAME          write output to FILENAME (optional)
  -d, --decoder ID               use a specific decoder (see --list-decoders)
      --with-aux                 also write auxiliary images (e.g. depth images)
      --with-xmp                 write XMP metadata to file (output filename with .xmp suffix)
      --with-exif                write EXIF metadata to file (output filename with .exif suffix)
      --skip-exif-offset         skip EXIF metadata offset bytes
      --no-colons                replace ':' characters in auxiliary image filenames with '_'
      --list-decoders            list all available decoders (built-in and plugins)
      --quiet                    do not output status messages to console
  -C, --chroma-upsampling ALGO   Force chroma upsampling algorithm (nn = nearest-neighbor / bilinear)
      --png-compression-level #  Set to integer between 0 (fastest) and 9 (best). Use -1 for default.
""".format(appname = APP_NAME, version = config.heif_version())


def test_help_output():

    ret = subprocess.run([APP_PATH, "--help"], capture_output=True, text=True)
    assert ret.stderr == helptext
