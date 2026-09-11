#include "stdafx.h"

#include <tge/text/text.h>
#include <tge/application.h>
#include <tge/text/TextService.h>
#include <tge/settings/settings.h>
#include <tge/log/Log.h>

#include "tge/graphics/GraphicsEngine.h"
using namespace Tga;

Text::Text(const Font& font)
	: myTextService(
		&Tga::GraphicsEngine::GetInstance()->GetTextService()
	)
{
	myColor.Set(1, 1, 1, 1);
	myScale = 1.0f;
	myFont = font;
	myRotation = 0.0f;
}

Text::Text(const char* aPathAndName, FontSize aFontSize, unsigned char aBorderSize)
: myTextService(
	&Tga::GraphicsEngine::GetInstance()->GetTextService()
)
{
	myColor.Set(1, 1, 1, 1);
	myScale = 1.0f;
	myFont = myTextService->GetOrLoad(aPathAndName, aFontSize, aBorderSize);
	myRotation = 0.0f;
}

Text::~Text() {}

void Tga::Text::Render()
{
	if (!myTextService)
	{
		return;
	}
	if (!myTextService->Draw(*this))
	{
		ERROR_PRINT("%s", "Text rendering error! Trying to render a text where the resource has been deleted! Did you clear the memory for this font? OR: Did you set the correct working directory?");
	}
}

void Tga::Text::Render(Tga::SpriteShader* aCustomShaderToRenderWith)
{
	if (!myTextService)
	{
		return;
	}
	if (!myTextService->Draw(*this, aCustomShaderToRenderWith))
	{
		ERROR_PRINT("%s", "Text rendering error! Trying to render a text where the resource has been deleted! Did you clear the memory for this font? OR: Did you set the correct working directory?");
	}
}

float Tga::Text::GetWidth()
{
	if (!myTextService)
	{
		return 0.0f;
	}

	return myTextService->GetSentenceWidth(*this);
}

float Tga::Text::GetHeight()
{
	if (!myTextService)
	{
		return 0.0f;
	}

	return myTextService->GetSentenceHeight(*this);
}

void Tga::Text::SetColor(const Color& aColor)
{
	myColor = aColor;
}

Tga::Color Tga::Text::GetColor() const
{
	return myColor;
}

void Tga::Text::SetText(std::string_view aText)
{
	myText = aText;
}

std::string Tga::Text::GetText() const
{
	return myText;
}

void Tga::Text::SetPosition(const Vector2f& aPosition)
{
	myPosition = aPosition;
}

Vector2f Tga::Text::GetPosition() const
{
	return myPosition;
}

void Tga::Text::SetScale(float aScale)
{
	myScale = aScale;
}

float Tga::Text::GetScale() const
{
	return myScale;
}