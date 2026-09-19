#pragma once
#include <age/math/matrix4x4.h>
#include <age/shaders/shader.h>

#define MAX_SHADER_DATA 8
#define MAX_SHADER_TEXTURES 2

using Microsoft::WRL::ComPtr;

namespace Ag
{
	class GraphicsEngine;
	class Engine;
	class Texture;
	class TextureResource;
	struct SpriteSharedData;

	enum ShaderDataID
	{
		ShaderDataID_1,
		ShaderDataID_2,
		ShaderDataID_3,
		ShaderDataID_4,
		ShaderDataID_5,
		ShaderDataID_6,
		ShaderDataID_7,
		ShaderDataID_8,
	};
	enum ShaderTextureSlot
	{
		ShaderTextureSlot_1 = 6, // Below 6 is the standard texture slots, keep above it! (: register( t6 ); in the shader)
		ShaderTextureSlot_2 = 7,
		ShaderTextureSlot_3 = 8,
		ShaderTextureSlot_4 = 9,
	};
	enum class ShaderDataBufferIndex
	{
		Index_1 = 5, // Below 5 is the standard buffer slots, keep above it! (: register(b5) in shader)
		Index_2 = 6,
		Index_3 = 7,
		Index_4 = 8,
	};

	class SpriteShader : private Shader
	{
		friend class SpriteDrawer;
	public:
		SpriteShader();
		bool Init(const char* aVertex = "shaders/instanced_sprite_shader_VS", const char* aPixel = "shaders/model_shader_vertex_color_textured_PS");


		void SetDataBufferIndex(ShaderDataBufferIndex aBufferRegisterIndex);
		// Add a float4 to a shader, this must be in the shade file matching 1:1! The Id is the index of the float4 in the shader (top down starting from 0)
		void SetShaderdataFloat4(Ag::Vector4f someData, ShaderDataID aID);

		// Set a texture to the shader, the index: 0, 1, 2, 3 are reserved. Keep the index above this and register it with the same id in the shader
		void SetTextureAtRegister(Ag::TextureResource* aTexture, ShaderTextureSlot aRegisterIndex);

		bool PrepareRender(const SpriteSharedData& aSharedData) const;

	protected:
		bool CreateInputLayout(const std::string& aVS) override;

	private:
		Texture* myDefaultTextures[ShaderMap::MAP_MAX];

		Ag::Vector4f myCustomData[MAX_SHADER_DATA];
		int myCurrentDataIndex;
		unsigned char myCurrentTextureCount;

		unsigned char myBufferIndex;

		struct BoundTexture
		{
			BoundTexture(Ag::TextureResource* aTexture = nullptr, unsigned char aIndex = 0)
			{
				texture = aTexture;
				index = aIndex;
			}
			Ag::TextureResource* texture = nullptr;
			unsigned char index = 0;
		};
		BoundTexture myBoundTextures[MAX_SHADER_TEXTURES];
	};
}