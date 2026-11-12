#pragma once

#include "stdafx.h"
#include <windows.h>

// Forward declarations to avoid hard dependency at header inclusion
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct IDXGISwapChain4;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11Buffer;
struct ID3D11SamplerState;
struct ID3D11BlendState;

// HDR Rendering Engine using Direct3D 11 and DXGI 1.5+ Advanced Color / scRGB
// Supports FP16 (DXGI_FORMAT_R16G16B16A16_FLOAT) swapchain with scRGB linear color space
// Dynamically delay-loaded via LoadLibrary to preserve instant startup when HDR is not in use.
class CHDRRendererD3D11 {
public:
	static CHDRRendererD3D11& Instance();

	// Initialize D3D11 and DXGI subsystem for the specified target window
	bool Initialize(HWND hWnd);
	void Shutdown();
	bool IsInitialized() const { return m_bInitialized; }

	// Query monitor HDR status and display capabilities (peak nits, paper white)
	bool QueryDisplayHDRCapabilities(HWND hWnd, float& outMaxLuminance, float& outMinLuminance, float& outSDRWhiteLevel);

	// HDR display state
	bool IsHDRAvailable() const { return m_bHDRAvailable; }
	bool IsHDREnabled() const { return m_bHDREnabled; }
	void SetHDREnabled(bool bEnable) { m_bHDREnabled = bEnable; }
	void ToggleHDR() { m_bHDREnabled = !m_bHDREnabled; }

	// Viewport resizing
	bool Resize(int nWidth, int nHeight);

	// Upload HDR image texture (supports half-float FP16 or float FP32 RGBA)
	bool UploadHDRImage(int nWidth, int nHeight, const void* pPixels, bool bIsFloat32, float fWhiteLevelNits = 80.0f);

	// Render the loaded HDR image to the target swapchain and blit GDI controls overlay
	void Render(HWND hWnd, const RECT& targetRect, const RECT& sourceRect,
		void* pGDIOverlayDIB = nullptr, int nOverlayWidth = 0, int nOverlayHeight = 0);

	// Clean up image texture resources
	void ReleaseImageTexture();

	// Returns display peak luminance in nits
	float GetMaxDisplayNits() const { return m_fMaxDisplayNits; }
	float GetSDRWhiteNits() const { return m_fSDRWhiteNits; }

private:
	CHDRRendererD3D11();
	~CHDRRendererD3D11();

	bool CreateDeviceAndSwapChain(HWND hWnd, int nWidth, int nHeight);
	bool CreateShadersAndPipelineState();
	bool UpdateOverlayTexture(void* pDIBBits, int nWidth, int nHeight);

	HWND m_hWnd;
	bool m_bInitialized;
	bool m_bHDRAvailable;
	bool m_bHDREnabled;

	int m_nClientWidth;
	int m_nClientHeight;
	float m_fMaxDisplayNits;
	float m_fMinDisplayNits;
	float m_fSDRWhiteNits;

	HMODULE m_hD3D11;
	HMODULE m_hDXGI;

	ID3D11Device* m_pDevice;
	ID3D11DeviceContext* m_pContext;
	IDXGISwapChain* m_pSwapChain;
	IDXGISwapChain4* m_pSwapChain4;
	ID3D11RenderTargetView* m_pRenderTargetView;

	// Image texture
	ID3D11Texture2D* m_pImageTexture;
	ID3D11ShaderResourceView* m_pImageSRV;
	const void* m_pUploadedPixels;
	int m_nImageWidth;
	int m_nImageHeight;
	float m_fImageLuminanceScale;

	// GDI overlay texture (for UI buttons, panels, text)
	ID3D11Texture2D* m_pOverlayTexture;
	ID3D11ShaderResourceView* m_pOverlaySRV;
	int m_nOverlayWidth;
	int m_nOverlayHeight;

	// Pipeline states
	ID3D11VertexShader* m_pVertexShader;
	ID3D11PixelShader* m_pPixelShaderHDR;
	ID3D11PixelShader* m_pPixelShaderOverlay;
	ID3D11Buffer* m_pConstantBuffer;
	ID3D11SamplerState* m_pSamplerLinear;
	ID3D11SamplerState* m_pSamplerPoint;
	ID3D11BlendState* m_pBlendStateAlpha;
};
