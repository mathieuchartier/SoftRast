#include <string.h>

#include <kt/Memory.h>
#include <kt/Logging.h>

#include "Texture.h"
#include "stb_image.h"
#include "stb_image_resize.h"

namespace kt
{

template <>
void Serialize(ISerializer* _s, sr::Tex::TextureData& _tex)
{
	Serialize(_s, _tex.m_texels);
	Serialize(_s, _tex.m_widthLog2);
	Serialize(_s, _tex.m_heightLog2);
	Serialize(_s, _tex.m_bytesPerPixel);
	Serialize(_s, _tex.m_mipOffsets);
	Serialize(_s, _tex.m_numMips);
}

}

namespace sr
{

namespace Tex
{


void TextureData::CreateFromFile(char const* _file)
{
	Clear();
	static uint32_t const req_comp = 4;
	int x, y, comp;
	uint8_t* srcImageData = stbi_load(_file, &x, &y, &comp, req_comp);
	if (!srcImageData)
	{
		KT_LOG_ERROR("Failed to load texture: %s", _file);
	}

	KT_SCOPE_EXIT(stbi_image_free(srcImageData));

	KT_ASSERT(kt::IsPow2(x) && kt::IsPow2(y));

	m_widthLog2 = kt::FloorLog2(uint32_t(x));
	m_heightLog2 = kt::FloorLog2(uint32_t(y));

	KT_ASSERT(m_heightLog2 < Config::c_maxTexDimLog2);
	KT_ASSERT(m_widthLog2 < Config::c_maxTexDimLog2);
	
	m_bytesPerPixel = req_comp;

	// Calculate mips.
	
	uint32_t const fullMipChainLen = kt::FloorLog2(kt::Max(uint32_t(x), uint32_t(y))) + 1; // +1 for base tex.
	m_numMips = fullMipChainLen;
	
	struct MipInfo
	{
		uint32_t m_offs;
		uint32_t m_dims[2];
	};

	uint32_t curMipDataOffset = x * y * req_comp;

	MipInfo* mipInfos = (MipInfo*)KT_ALLOCA(sizeof(MipInfo) * (fullMipChainLen - 1));
	m_mipOffsets[0] = 0;

	for (uint32_t mipIdx = 0; mipIdx < fullMipChainLen - 1; ++mipIdx)
	{
		CalcMipDims2D(uint32_t(x), uint32_t(y), mipIdx + 1, mipInfos[mipIdx].m_dims);
		mipInfos[mipIdx].m_offs = curMipDataOffset;
		m_mipOffsets[mipIdx + 1] = curMipDataOffset;
		curMipDataOffset += mipInfos[mipIdx].m_dims[0] * mipInfos[mipIdx].m_dims[1] * req_comp;
	}

	m_texels.Resize(curMipDataOffset);
	uint8_t* texWritePointer = m_texels.Data();

	memcpy(texWritePointer, srcImageData, req_comp * x * y);

	for (uint32_t mipIdx = 0; mipIdx < fullMipChainLen - 1; ++mipIdx)
	{
		MipInfo const& mipInfo = mipInfos[mipIdx];
		stbir_resize_uint8(srcImageData, x, y, 0, texWritePointer + mipInfo.m_offs, mipInfo.m_dims[0], mipInfo.m_dims[1], 0, req_comp);
	}
}

void TextureData::Clear()
{
	m_texels.ClearAndFree();
}

void CalcMipDims2D(uint32_t _x, uint32_t _y, uint32_t _level, uint32_t o_dims[2])
{
	o_dims[0] = kt::Max<uint32_t>(1u, _x >> _level);
	o_dims[1] = kt::Max<uint32_t>(1u, _y >> _level);
}

void SampleClamp_Slow(TextureData const& _tex, uint32_t const _mipIdx, float const _u, float const _v, float o_colour[4])
{
	uint32_t const mipClamped = kt::Min(_mipIdx, _tex.m_numMips);

	uint32_t const width	= 1u << (kt::Min(_tex.m_widthLog2, mipClamped) - mipClamped);
	uint32_t const height	= 1u << (kt::Min(_tex.m_heightLog2, mipClamped) - mipClamped);

	uint32_t const pitch = width * _tex.m_bytesPerPixel;

	uint32_t const clampU = uint32_t(kt::Clamp<int32_t>(int32_t(_u * width), 0, width - 1));
	uint32_t const clampV = uint32_t(kt::Clamp<int32_t>(int32_t(_v * height), 0, height - 1));

	uint32_t const offs = clampV * pitch + clampU * _tex.m_bytesPerPixel;
	uint8_t const* pix = _tex.m_texels.Data() + (_tex.m_mipOffsets[mipClamped] + offs);
	static const float recip255 = 1.0f / 255.0f;
	o_colour[0] = pix[0] * recip255;
	o_colour[1] = pix[1] * recip255;
	o_colour[2] = pix[2] * recip255;
	o_colour[3] = pix[3] * recip255;
}

void SampleWrap_Slow(TextureData const& _tex, uint32_t const _mipIdx, float const _u, float const _v, float o_colour[4])
{
	uint32_t const mipClamped = kt::Min(_mipIdx, _tex.m_numMips);

	uint32_t const width = 1u << (_tex.m_widthLog2 - mipClamped);
	uint32_t const height = 1u << (_tex.m_heightLog2 - mipClamped);

	uint32_t const pitch = width * _tex.m_bytesPerPixel;

	float const uSign = _u < 0.0f ? -1.0f : 1.0f;
	float const vSign = _v < 0.0f ? -1.0f : 1.0f;

	float const absU = uSign * _u;
	float const absV = vSign * _v;

	float const fracU = absU - int32_t(absU);
	float const fracV = absV - int32_t(absV);

	float const uWrap = uSign < 0.0f ? (1.0f - fracU) : fracU;
	float const vWrap = vSign < 0.0f ? (1.0f - fracV) : fracV;

	uint32_t const clampU = uint32_t(kt::Clamp<int32_t>(int32_t(uWrap * width), 0, width - 1));
	uint32_t const clampV = uint32_t(kt::Clamp<int32_t>(int32_t(vWrap * height), 0, height - 1));

	uint32_t const offs = clampV * pitch + clampU * _tex.m_bytesPerPixel;
	uint8_t const* pix = _tex.m_texels.Data() + (_tex.m_mipOffsets[mipClamped] + offs);
	static const float recip255 = 1.0f / 255.0f;
	o_colour[0] = pix[0] * recip255;
	o_colour[1] = pix[1] * recip255;
	o_colour[2] = pix[2] * recip255;
	o_colour[3] = pix[3] * recip255;
}

__m256i CalcMipLevels(TextureData const& _tex, __m256 _dudx, __m256 _dudy, __m256 _dvdx, __m256 _dvdy)
{
	__m256 const height = _mm256_set1_ps(float(1u << _tex.m_heightLog2));
	__m256 const width = _mm256_set1_ps(float(1u << _tex.m_widthLog2));
	
	__m256 const dudx_tex = _mm256_mul_ps(_dudx, width);
	__m256 const dudy_tex = _mm256_mul_ps(_dudy, height);

	__m256 const dvdx_tex = _mm256_mul_ps(_dvdx, width);
	__m256 const dvdy_tex = _mm256_mul_ps(_dvdy, height);

	// inner product
	__m256 const du_dot2 = _mm256_fmadd_ps(dudx_tex, dudx_tex, _mm256_mul_ps(dudy_tex, dudy_tex));
	__m256 const dv_dot2 = _mm256_fmadd_ps(dvdx_tex, dvdx_tex, _mm256_mul_ps(dvdy_tex, dvdy_tex));

	// Todo: with proper log2 we can use identity log2(x^(1/2)) == 0.5 * log2(x) and remove sqrt.
	__m256 const maxCoord = _mm256_sqrt_ps(_mm256_max_ps(du_dot2, dv_dot2));

	return _mm256_min_epi32(_mm256_set1_epi32(_tex.m_numMips - 1), _mm256_max_epi32(_mm256_setzero_si256(), simdutil::ExtractExponent(maxCoord)));
}

void SampleWrap
(
	TextureData const& _tex,
	__m256 _u,
	__m256 _v,
	__m256 _dudx,
	__m256 _dudy,
	__m256 _dvdx,
	__m256 _dvdy,
	float o_colour[4 * 8]
)
{
	__m256i const mipFloor = CalcMipLevels(_tex, _dudx, _dudy, _dvdx, _dvdy);

	__m256i const one = _mm256_set1_epi32(1);

	__m256i const widthLog2 = _mm256_set1_epi32(_tex.m_widthLog2);
	__m256i const heightLog2 = _mm256_set1_epi32(_tex.m_heightLog2);

	__m256i const width = _mm256_sllv_epi32(one, _mm256_sub_epi32(widthLog2, _mm256_min_epi32(widthLog2, mipFloor)));
	__m256i const height = _mm256_sllv_epi32(one, _mm256_sub_epi32(heightLog2, _mm256_min_epi32(heightLog2, mipFloor)));

	__m256i const pitch = _mm256_mullo_epi32(width, _mm256_set1_epi32(_tex.m_bytesPerPixel)); // todo: always four?

	__m256 const signBit = SR_AVX_LOAD_CONST_FLOAT(simdutil::c_avxSignBit);

	__m256 const uSign = _mm256_and_ps(signBit, _u);
	__m256 const vSign = _mm256_and_ps(signBit, _v);

	__m256 const absU = _mm256_xor_ps(uSign, _u);
	__m256 const absV = _mm256_xor_ps(vSign, _v);

	// Todo: naive casting faster than floor (roundps) ?
	__m256 const fracU = _mm256_sub_ps(absU, _mm256_floor_ps(absU));
	__m256 const fracV = _mm256_sub_ps(absV, _mm256_floor_ps(absV));

	__m256 const uWrap = _mm256_blendv_ps(fracU, _mm256_sub_ps(_mm256_set1_ps(1.0f), fracU), uSign);
	__m256 const vWrap = _mm256_blendv_ps(fracV, _mm256_sub_ps(_mm256_set1_ps(1.0f), fracV), vSign);

	__m256 const widthF = _mm256_cvtepi32_ps(width);
	__m256i const widthMinusOne = _mm256_sub_epi32(width, one);

	__m256 const heightF = _mm256_cvtepi32_ps(height);
	__m256i const heightMinusOne = _mm256_sub_epi32(height, one);

	__m256i const clampU = _mm256_min_epi32(widthMinusOne, _mm256_max_epi32(
		_mm256_setzero_si256(), _mm256_cvtps_epi32(_mm256_mul_ps(widthF, uWrap))));

	__m256i const clampV = _mm256_min_epi32(heightMinusOne, _mm256_max_epi32(
		_mm256_setzero_si256(), _mm256_cvtps_epi32(_mm256_mul_ps(heightF, vWrap))));

	__m256i const offs = _mm256_add_epi32(_mm256_mullo_epi32(pitch, clampV), _mm256_mullo_epi32(clampU, _mm256_set1_epi32(_tex.m_bytesPerPixel)));

	KT_ALIGNAS(32) uint32_t offsArr[8];
	KT_ALIGNAS(32) uint32_t mips[8];

	_mm256_store_si256((__m256i*)offsArr, offs);
	_mm256_store_si256((__m256i*)mips, mipFloor);
	static const float recip255 = 1.0f / 255.0f;

	for (uint32_t i = 0; i < 8; ++i)
	{
		uint8_t const* pix = _tex.m_texels.Data() + (offsArr[i] + _tex.m_mipOffsets[mips[i]]); // todo: mip offset
		o_colour[0 + i * 4] = pix[0] * recip255;
		o_colour[1 + i * 4] = pix[1] * recip255;
		o_colour[2 + i * 4] = pix[2] * recip255;
		o_colour[3 + i * 4] = pix[3] * recip255;
	}
}

}
}