#include "Texture.h"
#include "kt/Memory.h"
#include "stb_image.h"
#include "kt/Logging.h"

namespace sr
{

namespace Tex
{


void TextureData::CreateFromFile(char const* _file)
{
	Clear();
	static uint32_t const req_comp = 4;
	int x, y, comp;
	uint8_t* mem = stbi_load(_file, &x, &y, &comp, req_comp);
	if (!mem)
	{
		KT_LOG_ERROR("Failed to load texture: %s", _file);
	}

	m_width = x;
	m_height = y;
	m_data = mem;
	m_bitsPerPixel = req_comp * 8;
	m_rowStride = req_comp * m_width;
}

void TextureData::Clear()
{
	kt::Free(m_data);
}

void SampleClamp_Slow(TextureData const& _tex, float const _u, float const _v, float o_colour[4])
{
	uint32_t const clampU = uint32_t(kt::Clamp<int32_t>(_u * _tex.m_width, 0, _tex.m_width - 1));
	uint32_t const clampV = uint32_t(kt::Clamp<int32_t>(_v * _tex.m_height, 0, _tex.m_height - 1));

	uint32_t const offs = clampV * _tex.m_rowStride + clampU * (_tex.m_bitsPerPixel / 8);
	uint8_t* pix = _tex.m_data + offs;
	static const float recip255 = 1.0f / 255.0f;
	o_colour[0] = pix[0] * recip255;
	o_colour[1] = pix[1] * recip255;
	o_colour[2] = pix[2] * recip255;
	o_colour[3] = pix[3] * recip255;
}

void SampleWrap_Slow(TextureData const& _tex, float const _u, float const _v, float o_colour[4])
{
	float uWrap = fmodf(_u, 1.0f);
	float vWrap = fmodf(_v, 1.0f);

	uWrap = uWrap < 0.0f ? 1.0f + uWrap : uWrap;
	vWrap = vWrap < 0.0f ? 1.0f + vWrap : vWrap;

	uint32_t const clampU = uint32_t(kt::Clamp<int32_t>(uWrap * _tex.m_width, 0, _tex.m_width - 1));
	uint32_t const clampV = uint32_t(kt::Clamp<int32_t>(vWrap * _tex.m_height, 0, _tex.m_height - 1));

	uint32_t const offs = clampV * _tex.m_rowStride + clampU * (_tex.m_bitsPerPixel / 8);
	uint8_t* pix = _tex.m_data + offs;
	static const float recip255 = 1.0f / 255.0f;
	o_colour[0] = pix[0] * recip255;
	o_colour[1] = pix[1] * recip255;
	o_colour[2] = pix[2] * recip255;
	o_colour[3] = pix[3] * recip255;
}

}
}