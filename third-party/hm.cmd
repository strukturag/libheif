: # This script downloads the HEVC reference software HM, which libheif can use as an HEVC
: # encoder for the bit depths that x265 does not support (9, 11 and 13 to 15 bits) and for
: # the coding tools of the range extensions.
: # The HM encoder plugin of libheif is EXPERIMENTAL.

: # If you want to enable the HM encoder, set the CMake variable WITH_HM to ON.
: # libheif looks for HM in "third-party/HM". For a different location, set HM_ROOT.

: # HM is not built here. libheif compiles the HM sources itself, since it has to put them into
: # a C++ namespace: the plugin can contain a second version of HM, see "hm-scc.cmd".
: # ENABLE_HM_VARIANT_LATEST (default: ON) selects whether this version of HM is part of the plugin.

: # git must be in your PATH.

git clone -b HM-18.0 --depth 1 https://vcgit.hhi.fraunhofer.de/jvet/HM.git

echo ""
echo "----- NOTE ----"
echo "Configure libheif with -DWITH_HM=ON to enable the HM encoder."
