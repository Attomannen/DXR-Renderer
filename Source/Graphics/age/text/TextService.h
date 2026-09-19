/*
This class handles all texts that will be rendered, don't use this to show text, only use the Text class
*/

#pragma once

#include <age/EngineDefines.h>
#include <age/math/color.h>
#include <age/text/fontfile.h>
#include <age/text/text.h>
#include <unordered_map>
#include <vector>

namespace Ag
{
	class Texture;
	class Text;
	class TextService
	{
	public:
		TextService();
		~TextService();

		void Init();

		Font GetOrLoad(std::string aFontPathAndName, FontSize aFontSize, unsigned char aBorderSize = 0);
		bool Draw(Ag::Text& aText, Ag::SpriteShader* aCustomShaderToRenderWith = nullptr);
		float GetSentenceWidth(Ag::Text& aText);
		float GetSentenceHeight(Ag::Text& aText);

	private:
		struct FT_LibraryRec_* myLibrary;

		std::unordered_map<std::string, std::weak_ptr<InternalTextAndFontData>> myFontData;
	};
}