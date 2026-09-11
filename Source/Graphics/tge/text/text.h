/*
Use this class to create and show a text
*/

#pragma once
#include <tge/math/Color.h>
#include <tge/math/vector2.h>
#include <tge/render/RenderCommon.h>
#include <string>
#include <string_view>
#include <memory>

namespace Tga
{
	enum FontSize
	{
		FontSize_6 = 6,
		FontSize_8 = 8,
		FontSize_9 = 9,
		FontSize_10 = 10,
		FontSize_11 = 11,
		FontSize_12 = 12,
		FontSize_14 = 14,
		FontSize_18 = 18,
		FontSize_24 = 24,
		FontSize_30 = 30,
		FontSize_36 = 36,
		FontSize_48 = 48,
		FontSize_60 = 60,
		FontSize_72 = 72,
		FontSize_Count
	};

	class InternalTextAndFontData;
	struct Font
	{
		std::shared_ptr<const InternalTextAndFontData> myData;
	};

	class TextService;
	class SpriteShader;
	class Text
	{
		friend class TextService;
	public:
		Text(const Font& font);

		// If this is the first time creating the text, the text will be loaded in memory, dont do this runtime
		/*aPathAndName: ex. taxe/arial.ttf, */
		Text(const char* aPathAndName = "Text/arial.ttf", FontSize aFontSize = FontSize_14, unsigned char aBorderSize = 0);
		~Text();
		void Render();
		void Render(Tga::SpriteShader* aCustomShaderToRenderWith);
		float GetWidth();
		float GetHeight();

		void SetColor(const Color& aColor);
		Color GetColor() const;

		void SetText(std::string_view aText);
		std::string GetText() const;

		void SetPosition(const Vector2f& aPosition);
		Vector2f GetPosition() const;

		void SetScale(float aScale);
		float GetScale() const;

		void SetRotation(float aRotation) { myRotation = aRotation; }
		float GetRotation() const { return myRotation; }
	
	protected:
		TextService* myTextService;
		Font myFont;
		std::string myText;
		Vector2f myPosition;
		float myScale;
		float myRotation;
		Color myColor;
	};
}

