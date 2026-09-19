#include "stdafx.h"
#include "age/rhi/Format.h"

namespace Ag::rhi
{
	DXGI_FORMAT ToDxgi(Format f)
	{
		switch (f)
		{
		case Format::Unknown:              return DXGI_FORMAT_UNKNOWN;
		case Format::R8_UNorm:             return DXGI_FORMAT_R8_UNORM;
		case Format::R8G8B8A8_UNorm:       return DXGI_FORMAT_R8G8B8A8_UNORM;
		case Format::R8G8B8A8_UNorm_sRGB:  return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case Format::B8G8R8A8_UNorm:       return DXGI_FORMAT_B8G8R8A8_UNORM;
		case Format::B8G8R8A8_UNorm_sRGB:  return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		case Format::R10G10B10A2_UNorm:    return DXGI_FORMAT_R10G10B10A2_UNORM;
		case Format::R11G11B10_Float:      return DXGI_FORMAT_R11G11B10_FLOAT;
		case Format::R16_Float:            return DXGI_FORMAT_R16_FLOAT;
		case Format::R16G16_Float:         return DXGI_FORMAT_R16G16_FLOAT;
		case Format::R16G16B16A16_Float:   return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case Format::R32_Float:            return DXGI_FORMAT_R32_FLOAT;
		case Format::R32G32_Float:         return DXGI_FORMAT_R32G32_FLOAT;
		case Format::R32G32B32_Float:      return DXGI_FORMAT_R32G32B32_FLOAT;
		case Format::R32G32B32A32_Float:   return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case Format::R16_UInt:             return DXGI_FORMAT_R16_UINT;
		case Format::R32_UInt:             return DXGI_FORMAT_R32_UINT;
		case Format::R32G32_UInt:          return DXGI_FORMAT_R32G32_UINT;
		case Format::R32G32B32A32_UInt:    return DXGI_FORMAT_R32G32B32A32_UINT;
		case Format::R16G16B16A16_SNorm:   return DXGI_FORMAT_R16G16B16A16_SNORM;
		case Format::R32_Typeless:         return DXGI_FORMAT_R32_TYPELESS;
		case Format::R8G8B8A8_Typeless:    return DXGI_FORMAT_R8G8B8A8_TYPELESS;
		case Format::D16_UNorm:            return DXGI_FORMAT_D16_UNORM;
		case Format::D24_UNorm_S8_UInt:    return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case Format::D32_Float:            return DXGI_FORMAT_D32_FLOAT;
		case Format::BC1_UNorm:            return DXGI_FORMAT_BC1_UNORM;
		case Format::BC1_UNorm_sRGB:       return DXGI_FORMAT_BC1_UNORM_SRGB;
		case Format::BC2_UNorm:            return DXGI_FORMAT_BC2_UNORM;
		case Format::BC3_UNorm:            return DXGI_FORMAT_BC3_UNORM;
		case Format::BC3_UNorm_sRGB:       return DXGI_FORMAT_BC3_UNORM_SRGB;
		case Format::BC4_UNorm:            return DXGI_FORMAT_BC4_UNORM;
		case Format::BC5_UNorm:            return DXGI_FORMAT_BC5_UNORM;
		case Format::BC6H_UF16:            return DXGI_FORMAT_BC6H_UF16;
		case Format::BC7_UNorm:            return DXGI_FORMAT_BC7_UNORM;
		case Format::BC7_UNorm_sRGB:       return DXGI_FORMAT_BC7_UNORM_SRGB;
		}
		return DXGI_FORMAT_UNKNOWN;
	}

	Format FromDxgi(DXGI_FORMAT f)
	{
		switch (f)
		{
		case DXGI_FORMAT_R8_UNORM:              return Format::R8_UNorm;
		case DXGI_FORMAT_R8G8B8A8_UNORM:        return Format::R8G8B8A8_UNorm;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:   return Format::R8G8B8A8_UNorm_sRGB;
		case DXGI_FORMAT_B8G8R8A8_UNORM:        return Format::B8G8R8A8_UNorm;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:   return Format::B8G8R8A8_UNorm_sRGB;
		case DXGI_FORMAT_R10G10B10A2_UNORM:     return Format::R10G10B10A2_UNorm;
		case DXGI_FORMAT_R11G11B10_FLOAT:       return Format::R11G11B10_Float;
		case DXGI_FORMAT_R16_FLOAT:             return Format::R16_Float;
		case DXGI_FORMAT_R16G16_FLOAT:          return Format::R16G16_Float;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:    return Format::R16G16B16A16_Float;
		case DXGI_FORMAT_R32_FLOAT:             return Format::R32_Float;
		case DXGI_FORMAT_R32G32_FLOAT:          return Format::R32G32_Float;
		case DXGI_FORMAT_R32G32B32_FLOAT:       return Format::R32G32B32_Float;
		case DXGI_FORMAT_R32G32B32A32_FLOAT:    return Format::R32G32B32A32_Float;
		case DXGI_FORMAT_R16_UINT:              return Format::R16_UInt;
		case DXGI_FORMAT_R32_UINT:              return Format::R32_UInt;
		case DXGI_FORMAT_R32G32_UINT:           return Format::R32G32_UInt;
		case DXGI_FORMAT_R32G32B32A32_UINT:     return Format::R32G32B32A32_UInt;
		case DXGI_FORMAT_R16G16B16A16_SNORM:    return Format::R16G16B16A16_SNorm;
		case DXGI_FORMAT_R32_TYPELESS:          return Format::R32_Typeless;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:     return Format::R8G8B8A8_Typeless;
		case DXGI_FORMAT_D16_UNORM:             return Format::D16_UNorm;
		case DXGI_FORMAT_D24_UNORM_S8_UINT:     return Format::D24_UNorm_S8_UInt;
		case DXGI_FORMAT_D32_FLOAT:             return Format::D32_Float;
		case DXGI_FORMAT_BC1_UNORM:             return Format::BC1_UNorm;
		case DXGI_FORMAT_BC1_UNORM_SRGB:        return Format::BC1_UNorm_sRGB;
		case DXGI_FORMAT_BC2_UNORM:             return Format::BC2_UNorm;
		case DXGI_FORMAT_BC3_UNORM:             return Format::BC3_UNorm;
		case DXGI_FORMAT_BC3_UNORM_SRGB:        return Format::BC3_UNorm_sRGB;
		case DXGI_FORMAT_BC4_UNORM:             return Format::BC4_UNorm;
		case DXGI_FORMAT_BC5_UNORM:             return Format::BC5_UNorm;
		case DXGI_FORMAT_BC6H_UF16:             return Format::BC6H_UF16;
		case DXGI_FORMAT_BC7_UNORM:             return Format::BC7_UNorm;
		case DXGI_FORMAT_BC7_UNORM_SRGB:        return Format::BC7_UNorm_sRGB;
		default:                               return Format::Unknown;
		}
	}

	bool IsDepth(Format f)
	{
		return f == Format::D16_UNorm || f == Format::D24_UNorm_S8_UInt || f == Format::D32_Float;
	}

	bool IsTypeless(Format f)
	{
		return f == Format::R32_Typeless || f == Format::R8G8B8A8_Typeless;
	}

	DXGI_FORMAT ToTypeless(Format f)
	{
		switch (f)
		{
		case Format::D16_UNorm:         return DXGI_FORMAT_R16_TYPELESS;
		case Format::D24_UNorm_S8_UInt: return DXGI_FORMAT_R24G8_TYPELESS;
		case Format::D32_Float:         return DXGI_FORMAT_R32_TYPELESS;
		default:                        return ToDxgi(f);
		}
	}

	DXGI_FORMAT ToDsvFormat(Format f)
	{
		switch (f)
		{
		case Format::D16_UNorm:         return DXGI_FORMAT_D16_UNORM;
		case Format::D24_UNorm_S8_UInt: return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case Format::D32_Float:         return DXGI_FORMAT_D32_FLOAT;
		default:                        return ToDxgi(f);
		}
	}

	DXGI_FORMAT ToDepthSrvFormat(Format f)
	{
		switch (f)
		{
		case Format::D16_UNorm:         return DXGI_FORMAT_R16_UNORM;
		case Format::D24_UNorm_S8_UInt: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		case Format::D32_Float:         return DXGI_FORMAT_R32_FLOAT;
		default:                        return ToDxgi(f);
		}
	}

	uint32_t BitsPerPixel(Format f)
	{
		switch (f)
		{
		case Format::R8_UNorm:            return 8;
		case Format::R16_Float: case Format::R16_UInt: case Format::D16_UNorm: return 16;
		case Format::R8G8B8A8_UNorm: case Format::R8G8B8A8_UNorm_sRGB:
		case Format::B8G8R8A8_UNorm: case Format::B8G8R8A8_UNorm_sRGB:
		case Format::R10G10B10A2_UNorm: case Format::R11G11B10_Float:
		case Format::R16G16_Float: case Format::R32_Float: case Format::R32_UInt:
		case Format::R32_Typeless: case Format::R8G8B8A8_Typeless:
		case Format::D24_UNorm_S8_UInt: case Format::D32_Float:
			return 32;
		case Format::R16G16B16A16_Float: case Format::R16G16B16A16_SNorm: case Format::R32G32_Float: case Format::R32G32_UInt:
			return 64;
		case Format::R32G32B32A32_Float: case Format::R32G32B32A32_UInt:
			return 128;
		case Format::R32G32B32_Float:
			return 96;
		default:
			return 32;
		}
	}
}
