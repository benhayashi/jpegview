#pragma once

#include "stdafx.h"
#include <stdint.h>

enum EHDRToneMapOperator {
	TMO_BT2390 = 0,    // ITU-R BT.2390 Hermite spline knee curve (broadcast/cinema standard)
	TMO_ACES = 1,      // Academy Color Encoding System photographic filmic curve
	TMO_Reinhard = 2,  // Extended Reinhard tone mapper with white point preservation
	TMO_GainMapBase = 3 // Direct SDR base image extraction (Ultra HDR / Apple HDR)
};

// High-performance HDR to SDR tone mapper
class CHDRToneMapper {
public:
	// Maps an FP16 or FP32 HDR pixel buffer down to standard 32bpp BGRA DIB pixels
	static bool ConvertHDRToSDR_DIB(
		int nWidth, int nHeight,
		const void* pHDRPixels, bool bIsFloat32,
		void* pOutDIB32,
		EHDRToneMapOperator eOperator = TMO_BT2390,
		float fSourceMaxNits = 1000.0f,
		float fTargetNits = 100.0f,
		float fExposure = 1.0f);

	// Evaluates and applies an Ultra HDR / Apple HDR gain map to reconstruct HDR luminance
	static bool ApplyGainMap(
		int nWidth, int nHeight,
		const uint8_t* pSDRBaseBGRA,
		const uint8_t* pGainMapGray, int nGainMapW, int nGainMapH,
		void* pOutHDRPixels, bool bOutputFloat32,
		float fGainMapMin = 0.0f, float fGainMapMax = 1.0f,
		float fGamma = 1.0f, float fTargetWeight = 1.0f);

	// Fast half-precision float to single-precision float conversion
	static inline float HalfToFloat(uint16_t h) {
		uint32_t w = ((uint32_t)h) << 16;
		uint32_t sign = w & 0x80000000;
		uint32_t nonsign = w & 0x7FFFFFFF;
		uint32_t renorm = nonsign >> 3;
		int32_t exp = (int32_t)(nonsign >> 23);

		if (exp == 0) {
			return 0.0f;
		}
		if (exp == 31) {
			// Inf or NaN
			uint32_t r = sign | 0x7F800000 | (nonsign & 0x007FFFFF);
			return *(float*)&r;
		}
		uint32_t out = sign | ((exp + (127 - 15)) << 23) | ((h & 0x03FF) << 13);
		return *(float*)&out;
	}

	// Fast single-precision float to half-precision float conversion
	static inline uint16_t FloatToHalf(float f) {
		uint32_t x = *(uint32_t*)&f;
		uint32_t sign = (x >> 16) & 0x8000;
		int32_t exp = ((x >> 23) & 0xFF) - 127 + 15;
		uint32_t mant = x & 0x007FFFFF;

		if (exp <= 0) {
			return (uint16_t)sign;
		}
		if (exp >= 31) {
			return (uint16_t)(sign | 0x7C00); // clamp to max finite / inf
		}
		return (uint16_t)(sign | (exp << 10) | (mant >> 13));
	}

private:
	// ITU-R BT.2390 EETF tone mapping curve
	static float BT2390_EETF(float inputNorm, float kneePoint);
	// ACES filmic tone reproduction curve
	static inline float ACESFilmic(float x) {
		float a = 2.51f;
		float b = 0.03f;
		float c = 2.43f;
		float d = 0.59f;
		float e = 0.14f;
		return (x * (a * x + b)) / (x * (c * x + d) + e);
	}
};
