/*
 * HEIF codec.
 * Copyright (c) 2023 Dirk Farin <dirk.farin@gmail.com>
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

#ifndef LIBHEIF_COLORCONVERSION_REC2020_REC2100_H
#define LIBHEIF_COLORCONVERSION_REC2020_REC2100_H
#include <math.h>

// Below are PQ and HLG transfer functions as defined in Rec. 2100-3
// F_D = 10000 * Y = EOTF[E']
inline float PQ_EOTF(float E) {
	constexpr float c3 = 2392.0f / 128.0f;
	constexpr float c2 = 2413.0f / 128.0f;
	constexpr float c1 = c3 - c2 + 1.0f;
	constexpr float m2_rcp = 32.0f / 2523.0f;
	constexpr float m1_rcp = 8192.0f / 1305.0f;
	float EM = powf(E, m2_rcp);
	float Y = powf(fmaxf(EM - c1, 0.0f) / (c2 - c3 * EM), m1_rcp);
	return Y;
}

// E' = EOTF-1[FD] = EOTF-1[Y * 10000]
inline float PQ_inv_EOTF(float Y) {
	constexpr float c3 = 2392.0f / 128.0f;
	constexpr float c2 = 2413.0f / 128.0f;
	constexpr float c1 = c3 - c2 + 1.0f;
	constexpr float m2 = 2523.0f / 32.0f;
	constexpr float m1 = 1305.0f / 8192.0f;
	float YM = powf(Y, m1);
	return powf((c1 + c2 * YM) / (c3 * YM + 1.0f), m2);
}

inline float HLG_OETF(float E) {
	if (E <= 0.0f) {
		return 0.0f;
	}else if (E <= 1.0f / 12.0f) {
		return sqrtf(E * 3.0f);
	}
	else if (E > 1.0f) {
		return 1.0f;
	}
	else {
		constexpr float a = 0.17883277f;
		constexpr float b = 1.0f - 4.0f * a;
		constexpr float c = 0.55991073f; // 0.5f - a * logf(4.0f * a);
		return a * logf(12.0 * E - b) + c;
	}
}

inline float HLG_inv_OETF(float x) {
	if (x < 0.0f) {
		return 0.0f;
	}
	else if (x <= 0.5f) {
		return x * x / 3.0f;
	}
	else if (x <= 1.0f) {
		constexpr float a = 0.17883277f;
		constexpr float b = 1.0f - 4.0f * a;
		constexpr float c = 0.55991073f; // 0.5f - a * logf(4.0f * a);
		return (expf((x - c) / a) + b) / 12.0f;
	}
	else
		return 1.0f;
}

// Below are sRGB transfer characteristics
inline float sRGB_EOTF(float x) {
	if (x <= 0.04045f)
		return x / 12.92f;
	else
		return powf((x + 0.055f) / 1.055f, 2.4f);
}

inline float sRGB_inv_EOTF(float x) {
	if (x <= 0.0031308f)
		return x * 12.92f;
	else
		return 1.055f * powf(x, 1.0f / 2.4f) - 0.055f;
}

#endif