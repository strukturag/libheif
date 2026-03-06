#!/bin/bash
#
# HEIF codec.
# Copyright (c) 2025 Dirk Farin <dirk.farin@gmail.com>
#
# This file is part of libheif.
#
# libheif is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# libheif is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with libheif.  If not, see <http://www.gnu.org/licenses/>.
#

if [ "$#" -ne 1 ]; then
    echo "Usage: pass name of first (PNG) image (with alpha) as only argument"
    exit 5
fi

input_images=$1

for enc in "--hevc -e x265" \
	   "--hevc -e kvazaar" \
	   "--avc -e x264" \
	   "--vvc -e vvenc" \
	   "--vvc -e uvg266" \
	   "--avif -e aom" \
	   "--avif -e svt" \
	   "--avif -e rav1e" \
	   "--jpeg -e jpeg" \
	   "--jpeg2000 -e openjpeg" \
	   "--htj2k -e openjph" \
	   "--uncompressed -e uncompressed" ;
do
    format=${enc#--}        # remove leading --
    format="${format%% *}"  # stop at whitespace
    codec="${enc##* }"      # extract last word

    if [[ $format == "avif" ]] ; then
	suffix="avif"
    elif [[ $format == "hevc" ]] ; then
	suffix="heics"
    else
	suffix="heif"
    fi
    
    for alpha in "alpha" "noalpha" ; do
	if [[ $alpha == "alpha" ]] ; then
	    alpha_arg=""
	else
	    alpha_arg="--no-alpha"
	fi

	for loop in "loop" "once" ; do
	    if [[ $loop == "once" ]] ; then
		loop_arg=""
	    else
		loop_arg="--repetitions infinite"
	    fi
	    
	    echo "----- format: ${format}, encoder: ${codec}, ${alpha}, ${loop} -----"

	    if [[ $format == "hevc" || $format == "avc" || $format == "vvc" || $format == "avif" ]] ; then
		for pred in "i" "p" "b" ; do
		    ./examples/heif-enc -S $input_images -o ${alpha}-${format}-${pred}-${codec}-${loop}.$suffix $enc $alpha_arg $loop_arg --gop-structure $pred
		done
	    else
		./examples/heif-enc -S $input_images -o ${alpha}-${format}-${codec}-${loop}.$suffix $enc $alpha_arg $loop_arg
	    fi
	done
	
    done
    
done
