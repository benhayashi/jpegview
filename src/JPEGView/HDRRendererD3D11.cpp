#include "stdafx.h"
#include "HDRRendererD3D11.h"
#include "SettingsProvider.h"
#include <d3d11.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

typedef HRESULT(WINAPI* PFN_D3D11_CREATE_DEVICE)(
	IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
	const D3D_FEATURE_LEVEL*, UINT, UINT,
	ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

typedef HRESULT(WINAPI* PFN_CREATE_DXGI_FACTORY1)(REFIID, void**);

// Minimal HLSL Vertex Shader (generates fullscreen triangle using SV_VertexID without vertex buffer)
static const char g_ShaderVertexCode[] = 
"struct VSOutput {\n"
"    float4 Pos : SV_POSITION;\n"
"    float2 Tex : TEXCOORD0;\n"
"};\n"
"VSOutput main(uint id : SV_VertexID) {\n"
"    VSOutput output;\n"
"    output.Tex = float2((id << 1) & 2, id & 2);\n"
"    output.Pos = float4(output.Tex * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);\n"
"    return output;\n"
"}\n";

// Constant buffer for viewport mapping and luminance scaling
struct ConstantBufferData {
	float srcX;
	float srcY;
	float srcW;
	float srcH;
	float dstX;
	float dstY;
	float dstW;
	float dstH;
	float luminanceScale; // multiplier to map image values to scRGB (1.0 = 80 nits)
	float padding[3];
};

// HLSL Pixel Shader for HDR image rendering
static const char g_ShaderPixelHDRCode[] =
"Texture2D g_Texture : register(t0);\n"
"SamplerState g_Sampler : register(s0);\n"
"cbuffer CB : register(b0) {\n"
"    float4 g_SrcRect;\n"
"    float4 g_DstRect;\n"
"    float g_LuminanceScale;\n"
"    float3 g_Padding;\n"
"};\n"
"float4 main(float4 pos : SV_POSITION, float2 tex : TEXCOORD0) : SV_TARGET {\n"
"    if (pos.x < g_DstRect.x || pos.x > g_DstRect.x + g_DstRect.z ||\n"
"        pos.y < g_DstRect.y || pos.y > g_DstRect.y + g_DstRect.w) {\n"
"        return float4(0.0, 0.0, 0.0, 1.0);\n"
"    }\n"
"    float2 uv = (pos.xy - g_DstRect.xy) / g_DstRect.zw;\n"
"    float2 sampleUV = g_SrcRect.xy + uv * g_SrcRect.zw;\n"
"    float4 col = g_Texture.Sample(g_Sampler, sampleUV);\n"
"    col.rgb *= g_LuminanceScale;\n"
"    return col;\n"
"}\n";

// HLSL Pixel Shader for GDI UI overlay blending
static const char g_ShaderPixelOverlayCode[] =
"Texture2D g_Overlay : register(t0);\n"
"SamplerState g_Sampler : register(s0);\n"
"float4 main(float4 pos : SV_POSITION, float2 tex : TEXCOORD0) : SV_TARGET {\n"
"    float4 col = g_Overlay.Sample(g_Sampler, tex);\n"
"    return col;\n"
"}\n";

CHDRRendererD3D11& CHDRRendererD3D11::Instance() {
	static CHDRRendererD3D11 s_instance;
	return s_instance;
}

CHDRRendererD3D11::CHDRRendererD3D11() {
	m_hWnd = NULL;
	m_bInitialized = false;
	m_bHDRAvailable = false;
	m_bHDREnabled = true;
	m_nClientWidth = 0;
	m_nClientHeight = 0;
	m_fMaxDisplayNits = 80.0f;
	m_fMinDisplayNits = 0.005f;
	m_fSDRWhiteNits = 200.0f;
	m_hD3D11 = NULL;
	m_hDXGI = NULL;
	m_pDevice = NULL;
	m_pContext = NULL;
	m_pSwapChain = NULL;
	m_pSwapChain4 = NULL;
	m_pRenderTargetView = NULL;
	m_pImageTexture = NULL;
	m_pImageSRV = NULL;
	m_pUploadedPixels = NULL;
	m_nImageWidth = 0;
	m_nImageHeight = 0;
	m_fImageLuminanceScale = 1.0f;
	m_pOverlayTexture = NULL;
	m_pOverlaySRV = NULL;
	m_nOverlayWidth = 0;
	m_nOverlayHeight = 0;
	m_pVertexShader = NULL;
	m_pPixelShaderHDR = NULL;
	m_pPixelShaderOverlay = NULL;
	m_pConstantBuffer = NULL;
	m_pSamplerLinear = NULL;
	m_pSamplerPoint = NULL;
	m_pBlendStateAlpha = NULL;
}

CHDRRendererD3D11::~CHDRRendererD3D11() {
	Shutdown();
}

bool CHDRRendererD3D11::QueryDisplayHDRCapabilities(HWND hWnd, float& outMaxLuminance, float& outMinLuminance, float& outSDRWhiteLevel) {
	outMaxLuminance = 80.0f;
	outMinLuminance = 0.005f;
	outSDRWhiteLevel = 200.0f;

	HMODULE hDXGI = ::LoadLibraryW(L"dxgi.dll");
	if (!hDXGI) return false;

	PFN_CREATE_DXGI_FACTORY1 pfnCreateFactory = (PFN_CREATE_DXGI_FACTORY1)::GetProcAddress(hDXGI, "CreateDXGIFactory1");
	if (!pfnCreateFactory) {
		::FreeLibrary(hDXGI);
		return false;
	}

	IDXGIFactory1* pFactory = NULL;
	if (FAILED(pfnCreateFactory(__uuidof(IDXGIFactory1), (void**)&pFactory))) {
		::FreeLibrary(hDXGI);
		return false;
	}

	HMONITOR hTargetMonitor = ::MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
	bool bFoundHDR = false;

	UINT i = 0;
	IDXGIAdapter* pAdapter = NULL;
	while (pFactory->EnumAdapters(i++, &pAdapter) != DXGI_ERROR_NOT_FOUND) {
		UINT j = 0;
		IDXGIOutput* pOutput = NULL;
		while (pAdapter->EnumOutputs(j++, &pOutput) != DXGI_ERROR_NOT_FOUND) {
			DXGI_OUTPUT_DESC desc;
			pOutput->GetDesc(&desc);
			if (desc.Monitor == hTargetMonitor) {
				IDXGIOutput6* pOutput6 = NULL;
				if (SUCCEEDED(pOutput->QueryInterface(__uuidof(IDXGIOutput6), (void**)&pOutput6))) {
					DXGI_OUTPUT_DESC1 desc1;
					if (SUCCEEDED(pOutput6->GetDesc1(&desc1))) {
						outMaxLuminance = desc1.MaxLuminance;
						outMinLuminance = desc1.MinLuminance;
						if (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 || desc1.BitsPerColor >= 10) {
							bFoundHDR = true;
						}
					}
					pOutput6->Release();
				}
			}
			pOutput->Release();
		}
		pAdapter->Release();
	}

	pFactory->Release();
	::FreeLibrary(hDXGI);

	m_fMaxDisplayNits = outMaxLuminance;
	m_fMinDisplayNits = outMinLuminance;
	m_fSDRWhiteNits = outSDRWhiteLevel;
	m_bHDRAvailable = bFoundHDR;

	return bFoundHDR;
}

bool CHDRRendererD3D11::Initialize(HWND hWnd) {
	if (m_bInitialized && m_hWnd == hWnd) {
		return true;
	}
	Shutdown();

	m_hWnd = hWnd;
	RECT clientRect;
	::GetClientRect(hWnd, &clientRect);
	int nWidth = max(1, (int)(clientRect.right - clientRect.left));
	int nHeight = max(1, (int)(clientRect.bottom - clientRect.top));

	QueryDisplayHDRCapabilities(hWnd, m_fMaxDisplayNits, m_fMinDisplayNits, m_fSDRWhiteNits);

	if (!CreateDeviceAndSwapChain(hWnd, nWidth, nHeight)) {
		Shutdown();
		return false;
	}

	if (!CreateShadersAndPipelineState()) {
		Shutdown();
		return false;
	}

	m_bInitialized = true;
	return true;
}

bool CHDRRendererD3D11::CreateDeviceAndSwapChain(HWND hWnd, int nWidth, int nHeight) {
	m_hD3D11 = ::LoadLibraryW(L"d3d11.dll");
	m_hDXGI = ::LoadLibraryW(L"dxgi.dll");
	if (!m_hD3D11 || !m_hDXGI) return false;

	PFN_D3D11_CREATE_DEVICE pfnCreateDevice = (PFN_D3D11_CREATE_DEVICE)::GetProcAddress(m_hD3D11, "D3D11CreateDevice");
	PFN_CREATE_DXGI_FACTORY1 pfnCreateFactory = (PFN_CREATE_DXGI_FACTORY1)::GetProcAddress(m_hDXGI, "CreateDXGIFactory1");
	if (!pfnCreateDevice || !pfnCreateFactory) return false;

	UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
	// createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0
	};
	D3D_FEATURE_LEVEL featureLevel;

	HRESULT hr = pfnCreateDevice(
		NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createFlags,
		featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
		&m_pDevice, &featureLevel, &m_pContext);
	if (FAILED(hr)) return false;

	IDXGIFactory2* pFactory2 = NULL;
	hr = pfnCreateFactory(__uuidof(IDXGIFactory2), (void**)&pFactory2);
	if (FAILED(hr)) return false;

	DXGI_SWAP_CHAIN_DESC1 swapDesc = { 0 };
	swapDesc.Width = nWidth;
	swapDesc.Height = nHeight;
	// Use FP16 scRGB for true HDR Advanced Color display
	swapDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	swapDesc.Stereo = FALSE;
	swapDesc.SampleDesc.Count = 1;
	swapDesc.SampleDesc.Quality = 0;
	swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapDesc.BufferCount = 2;
	swapDesc.Scaling = DXGI_SCALING_NONE;
	swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	swapDesc.Flags = 0;

	IDXGISwapChain1* pSwapChain1 = NULL;
	hr = pFactory2->CreateSwapChainForHwnd(m_pDevice, hWnd, &swapDesc, NULL, NULL, &pSwapChain1);
	pFactory2->Release();
	if (FAILED(hr)) return false;

	m_pSwapChain = pSwapChain1;

	// Set color space to scRGB linear (G10_NONE_P709) for HDR
	IDXGISwapChain3* pSwapChain3 = NULL;
	if (SUCCEEDED(pSwapChain1->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&pSwapChain3))) {
		DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
		pSwapChain3->SetColorSpace1(colorSpace);
		pSwapChain3->Release();
	}

	pSwapChain1->QueryInterface(__uuidof(IDXGISwapChain4), (void**)&m_pSwapChain4);

	// Create Render Target View
	ID3D11Texture2D* pBackBuffer = NULL;
	hr = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
	if (FAILED(hr)) return false;

	hr = m_pDevice->CreateRenderTargetView(pBackBuffer, NULL, &m_pRenderTargetView);
	pBackBuffer->Release();
	if (FAILED(hr)) return false;

	m_nClientWidth = nWidth;
	m_nClientHeight = nHeight;
	return true;
}

bool CHDRRendererD3D11::CreateShadersAndPipelineState() {
	if (!m_pDevice) return false;

	ID3DBlob* pBlobVS = NULL;
	ID3DBlob* pBlobError = NULL;
	HRESULT hr = D3DCompile(g_ShaderVertexCode, strlen(g_ShaderVertexCode), NULL, NULL, NULL,
		"main", "vs_4_0", 0, 0, &pBlobVS, &pBlobError);
	if (FAILED(hr)) {
		if (pBlobError) pBlobError->Release();
		return false;
	}
	m_pDevice->CreateVertexShader(pBlobVS->GetBufferPointer(), pBlobVS->GetBufferSize(), NULL, &m_pVertexShader);
	pBlobVS->Release();

	// Pixel Shader HDR
	ID3DBlob* pBlobPS = NULL;
	hr = D3DCompile(g_ShaderPixelHDRCode, strlen(g_ShaderPixelHDRCode), NULL, NULL, NULL,
		"main", "ps_4_0", 0, 0, &pBlobPS, &pBlobError);
	if (FAILED(hr)) {
		if (pBlobError) pBlobError->Release();
		return false;
	}
	m_pDevice->CreatePixelShader(pBlobPS->GetBufferPointer(), pBlobPS->GetBufferSize(), NULL, &m_pPixelShaderHDR);
	pBlobPS->Release();

	// Pixel Shader Overlay
	hr = D3DCompile(g_ShaderPixelOverlayCode, strlen(g_ShaderPixelOverlayCode), NULL, NULL, NULL,
		"main", "ps_4_0", 0, 0, &pBlobPS, &pBlobError);
	if (FAILED(hr)) {
		if (pBlobError) pBlobError->Release();
		return false;
	}
	m_pDevice->CreatePixelShader(pBlobPS->GetBufferPointer(), pBlobPS->GetBufferSize(), NULL, &m_pPixelShaderOverlay);
	pBlobPS->Release();

	// Constant buffer
	D3D11_BUFFER_DESC cbDesc = { 0 };
	cbDesc.ByteWidth = sizeof(ConstantBufferData);
	cbDesc.Usage = D3D11_USAGE_DYNAMIC;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	m_pDevice->CreateBuffer(&cbDesc, NULL, &m_pConstantBuffer);

	// Samplers
	D3D11_SAMPLER_DESC sampDesc = { 0 };
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	m_pDevice->CreateSamplerState(&sampDesc, &m_pSamplerLinear);

	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	m_pDevice->CreateSamplerState(&sampDesc, &m_pSamplerPoint);

	// Blend state for GDI alpha overlay
	D3D11_BLEND_DESC blendDesc = { 0 };
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	m_pDevice->CreateBlendState(&blendDesc, &m_pBlendStateAlpha);

	return true;
}

bool CHDRRendererD3D11::Resize(int nWidth, int nHeight) {
	if (!m_pSwapChain || nWidth <= 0 || nHeight <= 0) return false;
	if (m_nClientWidth == nWidth && m_nClientHeight == nHeight) return true;

	if (m_pRenderTargetView) {
		m_pRenderTargetView->Release();
		m_pRenderTargetView = NULL;
	}

	HRESULT hr = m_pSwapChain->ResizeBuffers(0, nWidth, nHeight, DXGI_FORMAT_UNKNOWN, 0);
	if (FAILED(hr)) return false;

	ID3D11Texture2D* pBackBuffer = NULL;
	hr = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
	if (FAILED(hr)) return false;

	hr = m_pDevice->CreateRenderTargetView(pBackBuffer, NULL, &m_pRenderTargetView);
	pBackBuffer->Release();
	if (FAILED(hr)) return false;

	m_nClientWidth = nWidth;
	m_nClientHeight = nHeight;
	return true;
}

void CHDRRendererD3D11::ReleaseImageTexture() {
	if (m_pImageSRV) {
		m_pImageSRV->Release();
		m_pImageSRV = NULL;
	}
	if (m_pImageTexture) {
		m_pImageTexture->Release();
		m_pImageTexture = NULL;
	}
	m_pUploadedPixels = NULL;
	m_nImageWidth = 0;
	m_nImageHeight = 0;
}

bool CHDRRendererD3D11::UploadHDRImage(int nWidth, int nHeight, const void* pPixels, bool bIsFloat32, float fWhiteLevelNits) {
	if (!m_pDevice || !pPixels || nWidth <= 0 || nHeight <= 0) return false;
	if (m_pImageSRV != NULL && m_pUploadedPixels == pPixels && m_nImageWidth == nWidth && m_nImageHeight == nHeight) {
		return true;
	}
	ReleaseImageTexture();

	DXGI_FORMAT format = bIsFloat32 ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
	int bpp = bIsFloat32 ? 16 : 8;

	D3D11_TEXTURE2D_DESC desc = { 0 };
	desc.Width = nWidth;
	desc.Height = nHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA subData = { 0 };
	subData.pSysMem = pPixels;
	subData.SysMemPitch = nWidth * bpp;

	HRESULT hr = m_pDevice->CreateTexture2D(&desc, &subData, &m_pImageTexture);
	if (FAILED(hr)) return false;

	hr = m_pDevice->CreateShaderResourceView(m_pImageTexture, NULL, &m_pImageSRV);
	if (FAILED(hr)) {
		ReleaseImageTexture();
		return false;
	}

	m_pUploadedPixels = pPixels;
	m_nImageWidth = nWidth;
	m_nImageHeight = nHeight;
	// In scRGB, 1.0 = 80 nits. If image peak luminance is specified in nits, compute scale.
	m_fImageLuminanceScale = (fWhiteLevelNits > 0.0f) ? (fWhiteLevelNits / 80.0f) : 1.0f;
	return true;
}

bool CHDRRendererD3D11::UpdateOverlayTexture(void* pDIBBits, int nWidth, int nHeight) {
	if (!m_pDevice || !pDIBBits || nWidth <= 0 || nHeight <= 0) return false;

	if (!m_pOverlayTexture || m_nOverlayWidth != nWidth || m_nOverlayHeight != nHeight) {
		if (m_pOverlaySRV) { m_pOverlaySRV->Release(); m_pOverlaySRV = NULL; }
		if (m_pOverlayTexture) { m_pOverlayTexture->Release(); m_pOverlayTexture = NULL; }

		D3D11_TEXTURE2D_DESC desc = { 0 };
		desc.Width = nWidth;
		desc.Height = nHeight;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		if (FAILED(m_pDevice->CreateTexture2D(&desc, NULL, &m_pOverlayTexture))) return false;
		if (FAILED(m_pDevice->CreateShaderResourceView(m_pOverlayTexture, NULL, &m_pOverlaySRV))) return false;

		m_nOverlayWidth = nWidth;
		m_nOverlayHeight = nHeight;
	}

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (SUCCEEDED(m_pContext->Map(m_pOverlayTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		uint8_t* pDst = (uint8_t*)mapped.pData;
		uint8_t* pSrc = (uint8_t*)pDIBBits;
		int rowBytes = nWidth * 4;
		for (int y = 0; y < nHeight; y++) {
			memcpy(pDst + y * mapped.RowPitch, pSrc + y * rowBytes, rowBytes);
		}
		m_pContext->Unmap(m_pOverlayTexture, 0);
		return true;
	}
	return false;
}

void CHDRRendererD3D11::Render(HWND hWnd, const RECT& targetRect, const RECT& sourceRect,
	void* pGDIOverlayDIB, int nOverlayWidth, int nOverlayHeight) {
	if (!m_bInitialized || !m_pContext || !m_pRenderTargetView || !m_pImageSRV) return;

	RECT clientRect;
	::GetClientRect(hWnd, &clientRect);
	int clW = clientRect.right - clientRect.left;
	int clH = clientRect.bottom - clientRect.top;
	Resize(clW, clH);

	D3D11_VIEWPORT vp = { 0 };
	vp.Width = (float)clW;
	vp.Height = (float)clH;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	m_pContext->RSSetViewports(1, &vp);

	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	m_pContext->ClearRenderTargetView(m_pRenderTargetView, clearColor);
	m_pContext->OMSetRenderTargets(1, &m_pRenderTargetView, NULL);

	// Update constant buffer for image mapping
	D3D11_MAPPED_SUBRESOURCE mappedCB;
	if (SUCCEEDED(m_pContext->Map(m_pConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedCB))) {
		ConstantBufferData* cb = (ConstantBufferData*)mappedCB.pData;
		cb->srcX = (float)sourceRect.left / max(1, m_nImageWidth);
		cb->srcY = (float)sourceRect.top / max(1, m_nImageHeight);
		cb->srcW = (float)(sourceRect.right - sourceRect.left) / max(1, m_nImageWidth);
		cb->srcH = (float)(sourceRect.bottom - sourceRect.top) / max(1, m_nImageHeight);

		cb->dstX = (float)targetRect.left;
		cb->dstY = (float)targetRect.top;
		cb->dstW = (float)(targetRect.right - targetRect.left);
		cb->dstH = (float)(targetRect.bottom - targetRect.top);

		cb->luminanceScale = m_bHDREnabled ? m_fImageLuminanceScale : 1.0f;
		m_pContext->Unmap(m_pConstantBuffer, 0);
	}

	// Draw HDR quad
	m_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_pContext->IASetInputLayout(NULL);
	m_pContext->VSSetShader(m_pVertexShader, NULL, 0);
	m_pContext->PSSetShader(m_pPixelShaderHDR, NULL, 0);
	m_pContext->PSSetConstantBuffers(0, 1, &m_pConstantBuffer);
	m_pContext->PSSetShaderResources(0, 1, &m_pImageSRV);
	m_pContext->PSSetSamplers(0, 1, &m_pSamplerLinear);
	m_pContext->OMSetBlendState(NULL, NULL, 0xFFFFFFFF);

	m_pContext->Draw(3, 0);

	// Blend GDI UI overlay if available
	if (pGDIOverlayDIB && nOverlayWidth > 0 && nOverlayHeight > 0) {
		if (UpdateOverlayTexture(pGDIOverlayDIB, nOverlayWidth, nOverlayHeight)) {
			m_pContext->PSSetShader(m_pPixelShaderOverlay, NULL, 0);
			m_pContext->PSSetShaderResources(0, 1, &m_pOverlaySRV);
			m_pContext->PSSetSamplers(0, 1, &m_pSamplerPoint);
			m_pContext->OMSetBlendState(m_pBlendStateAlpha, NULL, 0xFFFFFFFF);
			m_pContext->Draw(3, 0);
		}
	}

	m_pSwapChain->Present(1, 0);
}

void CHDRRendererD3D11::Shutdown() {
	ReleaseImageTexture();

	if (m_pOverlaySRV) { m_pOverlaySRV->Release(); m_pOverlaySRV = NULL; }
	if (m_pOverlayTexture) { m_pOverlayTexture->Release(); m_pOverlayTexture = NULL; }
	if (m_pBlendStateAlpha) { m_pBlendStateAlpha->Release(); m_pBlendStateAlpha = NULL; }
	if (m_pSamplerLinear) { m_pSamplerLinear->Release(); m_pSamplerLinear = NULL; }
	if (m_pSamplerPoint) { m_pSamplerPoint->Release(); m_pSamplerPoint = NULL; }
	if (m_pConstantBuffer) { m_pConstantBuffer->Release(); m_pConstantBuffer = NULL; }
	if (m_pPixelShaderHDR) { m_pPixelShaderHDR->Release(); m_pPixelShaderHDR = NULL; }
	if (m_pPixelShaderOverlay) { m_pPixelShaderOverlay->Release(); m_pPixelShaderOverlay = NULL; }
	if (m_pVertexShader) { m_pVertexShader->Release(); m_pVertexShader = NULL; }
	if (m_pRenderTargetView) { m_pRenderTargetView->Release(); m_pRenderTargetView = NULL; }
	if (m_pSwapChain4) { m_pSwapChain4->Release(); m_pSwapChain4 = NULL; }
	if (m_pSwapChain) { m_pSwapChain->Release(); m_pSwapChain = NULL; }
	if (m_pContext) { m_pContext->Release(); m_pContext = NULL; }
	if (m_pDevice) { m_pDevice->Release(); m_pDevice = NULL; }

	if (m_hD3D11) { ::FreeLibrary(m_hD3D11); m_hD3D11 = NULL; }
	if (m_hDXGI) { ::FreeLibrary(m_hDXGI); m_hDXGI = NULL; }

	m_bInitialized = false;
	m_hWnd = NULL;
}
