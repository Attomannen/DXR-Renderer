#include "stdafx.h"

#include <age/text/text.h>
#include <age/application.h>
#include <age/text/TextService.h>
#include <age/settings/settings.h>
#include <age/log/Log.h>

#include "age/graphics/GraphicsEngine.h"
using namespace Ag;

Text::Text(const Font& font)
	: myTextService(
		&Ag::GraphicsEngine::GetInstance()->GetTextService()
	)
{
	myColor.Set(1, 1, 1, 1);
	myScale = 1.0f;
	myFont = font;
	myRotation = 0.0f;
}

Text::Text(const char* aPathAndName, FontSize aFontSize, unsigned char aBorderSize)
: myTextService(
	&Ag::GraphicsEngine::GetInstance()->GetTextService()
)
{
	myColor.Set(1, 1, 1, 1);
	myScale = 1.0f;
	myFont = myTextService->GetOrLoad(aPathAndName, aFontSize, aBorderSize);
	myRotation = 0.0f;
}

Text::~Text() {}

void Ag::Text::Render()
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

void Ag::Text::Render(Ag::SpriteShader* aCustomShaderToRenderWith)
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

float Ag::Text::GetWidth()
{
	if (!myTextService)
	{
		return 0.0f;
	}

	return myTextService->GetSentenceWidth(*this);
}

float Ag::Text::GetHeight()
{
	if (!myTextService)
	{
		return 0.0f;
	}

	return myTextService->GetSentenceHeight(*this);
}

void Ag::Text::SetColor(const Color& aColor)
{
	myColor = aColor;
}

Ag::Color Ag::Text::GetColor() const
{
	return myColor;
}

void Ag::Text::SetText(std::string_view aText)
{
	myText = aText;
}

std::string Ag::Text::GetText() const
{
	return myText;
}

void Ag::Text::SetPosition(const Vector2f& aPosition)
{
	myPosition = aPosition;
}

Vector2f Ag::Text::GetPosition() const
{
	return myPosition;
}

void Ag::Text::SetScale(float aScale)
{
	myScale = aScale;
}

float Ag::Text::GetScale() const
{
	return myScale;
}