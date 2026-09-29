: # This script downloads the version of the HEVC reference software HM that contains the
: # screen content coding (SCC) tools: palette mode, intra block copy and the adaptive
: # colour transform. They never became part of the main line of HM, so the latest version
: # with these tools is HM-16.21+SCM-8.8.
: # The HM encoder plugin of libheif is EXPERIMENTAL.

: # If you want the SCC tools in the HM encoder, set the CMake variables WITH_HM and
: # ENABLE_HM_VARIANT_SCC to ON. libheif looks for this version of HM in "third-party/HM-SCC".
: # For a different location, set HM_SCC_ROOT.

: # This version can be used together with the latest version of HM ("hm.cmd"), or without it.
: # With both, the plugin uses this version only for images that are encoded with SCC tools.

: # HM is not built here. libheif compiles the HM sources itself.

: # git must be in your PATH.

git clone -b HM-16.21+SCM-8.8 --depth 1 https://vcgit.hhi.fraunhofer.de/jvet/HM.git HM-SCC

echo ""
echo "----- NOTE ----"
echo "Configure libheif with -DWITH_HM=ON -DENABLE_HM_VARIANT_SCC=ON to enable the HM encoder with SCC tools."
