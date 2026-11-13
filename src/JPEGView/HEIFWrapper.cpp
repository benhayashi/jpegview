#include "stdafx.h"

#include "HEIFWrapper.h"
#include "MaxImageDef.h"
#include "ICCProfileTransform.h"

#include "HDRToneMapper.h"

void * HeifReader::ReadImage(int &width,
					   int &height,
					   int &nchannels,
					   int &frame_count,
					   void* &exif_chunk,
					   bool &outOfMemory,
					   int frame_index,
					   const void *buffer,
					   int sizebytes,
					   bool* pOutIsHDR,
					   void** pOutHDRPixels,
					   float* pOutMaxLuminance)
{
	outOfMemory = false;
	width = height = 0;
	nchannels = 4;
	if (pOutIsHDR) *pOutIsHDR = false;
	if (pOutHDRPixels) *pOutHDRPixels = nullptr;
	if (pOutMaxLuminance) *pOutMaxLuminance = 80.0f;

	unsigned char* pPixelData = NULL;
	exif_chunk = NULL;

	heif::Context context;
	context.read_from_memory_without_copy(buffer, sizebytes);
	frame_count = context.get_number_of_top_level_images();
	heif_item_id item_id = context.get_list_of_top_level_image_IDs().at(frame_index);
	heif::ImageHandle handle = context.get_image_handle(item_id);
	// height = handle.get_height();
	// width = handle.get_width();
	heif::Image image = handle.decode_image(heif_colorspace_RGB, heif_chroma_interleaved_RGBA);
	int stride;
	uint8_t* data = image.get_plane(heif_channel_interleaved, &stride);
	width = image.get_width(heif_channel_interleaved);
	height = image.get_height(heif_channel_interleaved);

	if (width > MAX_IMAGE_DIMENSION || height > MAX_IMAGE_DIMENSION)
		return NULL;
	if (abs((double)width * height) > MAX_IMAGE_PIXELS) {
		outOfMemory = true;
		return NULL;
	}
	if (width < 1 || height < 1 || width * nchannels > stride)
		return NULL;

	int size = width * nchannels * height;
	pPixelData = new(std::nothrow) unsigned char[size];
	if (pPixelData == NULL) {
		outOfMemory = true;
		return NULL;
	}
	std::vector<uint8_t> iccp = image.get_raw_color_profile();
	void* transform = ICCProfileTransform::CreateTransform(iccp.data(), iccp.size(), ICCProfileTransform::FORMAT_RGBA);
	size_t i, j;
	if (!ICCProfileTransform::DoTransform(transform, data, pPixelData, width, height, stride=stride)) {
		unsigned int* o = (unsigned int*)pPixelData;
		for (i = 0; i < height; i++) {
			unsigned int* p = (unsigned int*)(data + i * stride);
			for (j = 0; j < width; j++) {
				// RGBA -> BGRA conversion
				*o = _rotr(_byteswap_ulong(*p), 8);
				p++;
				o++;
			}
		}
	ICCProfileTransform::DeleteTransform(transform);

	// Detect HDR characteristics (10/12-bit depth or NCLX PQ/HLG)
	bool bIsHDR = false;
	float fMaxLuminance = 1000.0f;
	if (handle.get_luma_bits_per_pixel() > 8) {
		bIsHDR = true;
	}
	if (handle.get_raw_image_handle() != nullptr) {
		heif_color_profile_type profileType = heif_image_handle_get_color_profile_type(handle.get_raw_image_handle());
		if (profileType == heif_color_profile_type_nclx) {
			struct heif_color_profile_nclx* nclx = nullptr;
			if (heif_image_handle_get_nclx_color_profile(handle.get_raw_image_handle(), &nclx).code == heif_error_Ok && nclx != nullptr) {
				if (nclx->transfer_characteristics == 16 || nclx->transfer_characteristics == 18) {
					bIsHDR = true;
				}
				heif_nclx_color_profile_free(nclx);
			}
		}
	}

	if (bIsHDR && pOutHDRPixels != nullptr) {
		try {
			heif::Image hdrImage = handle.decode_image(heif_colorspace_RGB, heif_chroma_interleaved_RRGGBBAA_LE);
			int hdrStride = 0;
			uint8_t* hdrData = hdrImage.get_plane(heif_channel_interleaved, &hdrStride);
			int hdrW = hdrImage.get_width(heif_channel_interleaved);
			int hdrH = hdrImage.get_height(heif_channel_interleaved);
			if (hdrData && hdrW > 0 && hdrH > 0) {
				uint16_t* pFP16 = new(std::nothrow) uint16_t[hdrW * hdrH * 4];
				if (pFP16) {
					int bitDepth = handle.get_luma_bits_per_pixel();
					if (bitDepth < 10) bitDepth = 10;
					float maxVal = (float)((1 << bitDepth) - 1);
					for (int y = 0; y < hdrH; y++) {
						uint16_t* pRow = (uint16_t*)(hdrData + y * hdrStride);
						for (int x = 0; x < hdrW; x++) {
							float r = (float)pRow[x * 4 + 0] / maxVal;
							float g = (float)pRow[x * 4 + 1] / maxVal;
							float b = (float)pRow[x * 4 + 2] / maxVal;
							float a = (float)pRow[x * 4 + 3] / maxVal;
							pFP16[(y * hdrW + x) * 4 + 0] = CHDRToneMapper::FloatToHalf(r);
							pFP16[(y * hdrW + x) * 4 + 1] = CHDRToneMapper::FloatToHalf(g);
							pFP16[(y * hdrW + x) * 4 + 2] = CHDRToneMapper::FloatToHalf(b);
							pFP16[(y * hdrW + x) * 4 + 3] = CHDRToneMapper::FloatToHalf(a);
						}
					}
					*pOutHDRPixels = (void*)pFP16;
				}
			}
		} catch (...) {}
	}
	if (pOutIsHDR) *pOutIsHDR = bIsHDR;
	if (pOutMaxLuminance) *pOutMaxLuminance = fMaxLuminance;

	std::vector<heif_item_id> exif_blocks = handle.get_list_of_metadata_block_IDs("Exif");

	if (!exif_blocks.empty()) {
		std::vector<uint8_t> exif = handle.get_metadata(exif_blocks[0]);
		// 65538 magic number comes from investigations by qbnu
		// see https://github.com/sylikc/jpegview/pull/213#pullrequestreview-1494451359 for more details
		/*
		* These libraries all have their own ideas about where to start the Exif data from.
		* JPEG Exif blocks are in the format
		  FF E1 SS SS 45 78 69 66 00 00 [data]

		  The SS SS is a big-endian unsigned short representing the size of everything after FF E1.

		  libjxl gives 00 00 00 00 [data]
		  libheif gives 00 00 00 00 45 78 69 66 00 00 [data]
		  libavif, libwebp and libpng give [data], so they have different limits for size.

		  If you want I can change it to 65536 + an offset and add notes explaining why.
		*/
		if (exif.size() > 8 && exif.size() < 65538) {
			exif_chunk = malloc(exif.size());
			if (exif_chunk != NULL) {
				memcpy(exif_chunk, exif.data(), exif.size());
				*((unsigned short*)exif_chunk) = _byteswap_ushort(0xFFE1);
				*((unsigned short*)exif_chunk + 1) = _byteswap_ushort(exif.size() - 2);
			}
		}
	}

	return (void*)pPixelData;
}
