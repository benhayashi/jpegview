#include "stdafx.h"
#include "HDRToneMapper.h"
#include <math.h>
#include <algorithm>

float CHDRToneMapper::BT2390_EETF(float inputNorm, float kneePoint) {
	if (inputNorm <= kneePoint) {
		return inputNorm;
	}
	// Hermite spline compression between kneePoint and 1.0
	float t = (inputNorm - kneePoint) / max(1e-5f, (1.0f - kneePoint));
	t = min(1.0f, max(0.0f, t));
	float hermite = (2.0f * t * t * t - 3.0f * t * t + 1.0f) * kneePoint +
		(t * t * t - 2.0f * t * t + t) * (1.0f - kneePoint) +
		(-2.0f * t * t * t + 3.0f * t * t);
	return hermite;
}

bool CHDRToneMapper::ConvertHDRToSDR_DIB(
	int nWidth, int nHeight,
	const void* pHDRPixels, bool bIsFloat32,
	void* pOutDIB32,
	EHDRToneMapOperator eOperator,
	float fSourceMaxNits,
	float fTargetNits,
	float fExposure) {

	if (!pHDRPixels || !pOutDIB32 || nWidth <= 0 || nHeight <= 0) return false;

	const float* pSrcFloat = bIsFloat32 ? (const float*)pHDRPixels : nullptr;
	const uint16_t* pSrcHalf = !bIsFloat32 ? (const uint16_t*)pHDRPixels : nullptr;
	uint32_t* pDst = (uint32_t*)pOutDIB32;

	float normFactor = (fSourceMaxNits > 0.0f) ? (1.0f / fSourceMaxNits) : (1.0f / 1000.0f);
	float knee = max(0.1f, min(0.9f, (fTargetNits / max(fTargetNits, fSourceMaxNits))));

	for (int y = 0; y < nHeight; y++) {
		int rowOffset = y * nWidth * 4;
		for (int x = 0; x < nWidth; x++) {
			float r, g, b, a;
			if (bIsFloat32) {
				r = pSrcFloat[rowOffset + x * 4 + 0];
				g = pSrcFloat[rowOffset + x * 4 + 1];
				b = pSrcFloat[rowOffset + x * 4 + 2];
				a = pSrcFloat[rowOffset + x * 4 + 3];
			} else {
				r = HalfToFloat(pSrcHalf[rowOffset + x * 4 + 0]);
				g = HalfToFloat(pSrcHalf[rowOffset + x * 4 + 1]);
				b = HalfToFloat(pSrcHalf[rowOffset + x * 4 + 2]);
				a = HalfToFloat(pSrcHalf[rowOffset + x * 4 + 3]);
			}

			// Apply exposure scaling
			r *= fExposure;
			g *= fExposure;
			b *= fExposure;

			float rOut, gOut, bOut;

			if (eOperator == TMO_ACES) {
				rOut = ACESFilmic(r);
				gOut = ACESFilmic(g);
				bOut = ACESFilmic(b);
			} else if (eOperator == TMO_Reinhard) {
				float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
				float lumOut = (lum * (1.0f + lum / 16.0f)) / (1.0f + lum);
				float scale = (lum > 1e-5f) ? (lumOut / lum) : 1.0f;
				rOut = r * scale;
				gOut = g * scale;
				bOut = b * scale;
			} else { // TMO_BT2390
				float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
				float lumNorm = lum * normFactor;
				float mappedLumNorm = BT2390_EETF(lumNorm, knee);
				float scale = (lum > 1e-5f) ? ((mappedLumNorm / normFactor) / lum) : 1.0f;
				rOut = r * scale;
				gOut = g * scale;
				bOut = b * scale;
			}

			// Apply standard sRGB gamma (approx 2.2) and clamp to 8-bit
			rOut = min(1.0f, max(0.0f, rOut));
			gOut = min(1.0f, max(0.0f, gOut));
			bOut = min(1.0f, max(0.0f, bOut));

			// Linear to sRGB transfer
			float rSRGB = (rOut <= 0.0031308f) ? (12.92f * rOut) : (1.055f * powf(rOut, 1.0f / 2.4f) - 0.055f);
			float gSRGB = (gOut <= 0.0031308f) ? (12.92f * gOut) : (1.055f * powf(gOut, 1.0f / 2.4f) - 0.055f);
			float bSRGB = (bOut <= 0.0031308f) ? (12.92f * bOut) : (1.055f * powf(bOut, 1.0f / 2.4f) - 0.055f);

			uint8_t u8R = (uint8_t)(min(255.0f, max(0.0f, rSRGB * 255.0f + 0.5f)));
			uint8_t u8G = (uint8_t)(min(255.0f, max(0.0f, gSRGB * 255.0f + 0.5f)));
			uint8_t u8B = (uint8_t)(min(255.0f, max(0.0f, bSRGB * 255.0f + 0.5f)));
			uint8_t u8A = (uint8_t)(min(255.0f, max(0.0f, a * 255.0f + 0.5f)));

			// Output BGRA for Windows DIB
			pDst[y * nWidth + x] = (u8A << 24) | (u8R << 16) | (u8G << 8) | u8B;
		}
	}
	return true;
}

bool CHDRToneMapper::ApplyGainMap(
	int nWidth, int nHeight,
	const uint8_t* pSDRBaseBGRA,
	const uint8_t* pGainMapGray, int nGainMapW, int nGainMapH,
	void* pOutHDRPixels, bool bOutputFloat32,
	float fGainMapMin, float fGainMapMax,
	float fGamma, float fTargetWeight) {

	if (!pSDRBaseBGRA || !pGainMapGray || !pOutHDRPixels || nWidth <= 0 || nHeight <= 0) return false;

	float* pDstFloat = bOutputFloat32 ? (float*)pOutHDRPixels : nullptr;
	uint16_t* pDstHalf = !bOutputFloat32 ? (uint16_t*)pOutHDRPixels : nullptr;

	float logMin = logf(max(1e-4f, fGainMapMin));
	float logMax = logf(max(1e-4f, fGainMapMax));

	for (int y = 0; y < nHeight; y++) {
		int gmY = (y * nGainMapH) / nHeight;
		for (int x = 0; x < nWidth; x++) {
			int gmX = (x * nGainMapW) / nWidth;

			uint32_t sdrPixel = ((const uint32_t*)pSDRBaseBGRA)[y * nWidth + x];
			uint8_t b = (sdrPixel) & 0xFF;
			uint8_t g = (sdrPixel >> 8) & 0xFF;
			uint8_t r = (sdrPixel >> 16) & 0xFF;
			uint8_t a = (sdrPixel >> 24) & 0xFF;

			// Convert SDR sRGB [0..255] to linear [0..1]
			float rLin = powf(r / 255.0f, 2.2f);
			float gLin = powf(g / 255.0f, 2.2f);
			float bLin = powf(b / 255.0f, 2.2f);

			// Sample gain map
			uint8_t gmVal = pGainMapGray[gmY * nGainMapW + gmX];
			float gmNorm = powf(gmVal / 255.0f, fGamma);

			// Interpolate gain factor in log space
			float gainLog = logMin + gmNorm * (logMax - logMin);
			float gainFactor = expf(gainLog * fTargetWeight);

			float rHDR = rLin * gainFactor;
			float gHDR = gLin * gainFactor;
			float bHDR = bLin * gainFactor;
			float aHDR = a / 255.0f;

			int outIndex = (y * nWidth + x) * 4;
			if (bOutputFloat32) {
				pDstFloat[outIndex + 0] = rHDR;
				pDstFloat[outIndex + 1] = gHDR;
				pDstFloat[outIndex + 2] = bHDR;
				pDstFloat[outIndex + 3] = aHDR;
			} else {
				pDstHalf[outIndex + 0] = FloatToHalf(rHDR);
				pDstHalf[outIndex + 1] = FloatToHalf(gHDR);
				pDstHalf[outIndex + 2] = FloatToHalf(bHDR);
				pDstHalf[outIndex + 3] = FloatToHalf(aHDR);
			}
		}
	}
	return true;
}
