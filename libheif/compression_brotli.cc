/*
 * HEIF codec.
 * Copyright (c) 2024 Brad Hards <bradh@frogmouth.net>
 *
 * This file is part of libheif.
 *
 * libheif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libheif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libheif.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "compression.h"

#if HAVE_BROTLI

const size_t BUF_SIZE = (1 << 18);
#include <brotli/decode.h>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

#include "error.h"

Error decompress_brotli(const std::vector<uint8_t> &compressed_input, std::vector<uint8_t> *output)
{
    BrotliDecoderResult result = BROTLI_DECODER_RESULT_ERROR;
    std::vector<uint8_t> buffer(BUF_SIZE, 0);
    size_t available_in = compressed_input.size();
    const std::uint8_t *next_in = reinterpret_cast<const std::uint8_t *>(compressed_input.data());
    size_t available_out = buffer.size();
    std::uint8_t *next_output = buffer.data();
    BrotliDecoderState *state = BrotliDecoderCreateInstance(0, 0, 0);

    while (true)
    {
        result = BrotliDecoderDecompressStream(state, &available_in, &next_in, &available_out, &next_output, 0);

        if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT)
        {
            output->insert(output->end(), buffer.data(), buffer.data() + std::distance(buffer.data(), next_output));
            available_out = buffer.size();
            next_output = buffer.data();
        }
        else if (result == BROTLI_DECODER_RESULT_SUCCESS)
        {
            output->insert(output->end(), buffer.data(), buffer.data() + std::distance(buffer.data(), next_output));
            break;
        }
        else if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT)
        {
            std::stringstream sstr;
            sstr << "Error performing brotli inflate - insufficient data.\n";
            return Error(heif_error_Invalid_input, heif_suberror_Decompression_invalid_data, sstr.str());
        }
        else if (result == BROTLI_DECODER_RESULT_ERROR)
        {
            const char* errorMessage = BrotliDecoderErrorString(BrotliDecoderGetErrorCode(state));
            std::stringstream sstr;
            sstr << "Error performing brotli inflate - " << errorMessage << "\n";
            return Error(heif_error_Invalid_input, heif_suberror_Decompression_invalid_data, sstr.str());
        }
        else
        {
            const char* errorMessage = BrotliDecoderErrorString(BrotliDecoderGetErrorCode(state));
            std::stringstream sstr;
            sstr << "Unknown error performing brotli inflate - " << errorMessage << "\n";
            return Error(heif_error_Invalid_input, heif_suberror_Decompression_invalid_data, sstr.str());
        }
    }
    BrotliDecoderDestroyInstance(state);
    return Error::Ok;
}

#endif
