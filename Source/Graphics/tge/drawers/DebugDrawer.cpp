#include "stdafx.h"
#ifndef _RETAIL
#include <tge/drawers/DebugDrawer.h>
#include <tge/drawers/DebugPerformancegraph.h>
#include <tge/drawers/LineDrawer.h>
#include <tge/application.h>
#include <tge/primitives/LinePrimitive.h>
#include <tge/text/Text.h>
#include <tge/log/Log.h>
#include <tge/graphics/camera.h>
#include <tge/graphics/DX11.h>
#include <tge/graphics/GraphicsEngine.h>
#include <tge/graphics/GraphicsStateStack.h>
#include <tge/sprite/sprite.h>
#include <tge/texture/texture.h>
#include <tge/texture/TextureManager.h>
#include <tge/drawers/SpriteDrawer.h>
#include <tge/util/FixedStream.h>
#include <chrono>
#include <string>

constexpr float LOG_DISPLAY_DURATION = 4.0f;

#include "tge/graphics/AmbientLight.h"
#include "tge/graphics/DirectionalLight.h"

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN 
#endif
#if !defined(NOMINMAX)
#define NOMINMAX 
#endif
#include <windows.h>

#include <stdio.h>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

using namespace Tga;

DebugDrawer::DebugDrawer(bool aIsEnabled)
{
	myIsEnabled = aIsEnabled;
	myTickIndex = 0;
	myTickSum = 0;
	myRealFPS = 0;
	myRealFPSAvarage = 0;
	myLastErrorsCount = 0;
}


DebugDrawer::~DebugDrawer(void)
{}

void DebugDrawer::Init()
{
	myLineBuffer = std::make_unique<std::array<LinePrimitive, MAX_LINES>>();

	myFPS = std::make_unique<Tga::Text>("Text/arial.ttf", FontSize_24);
	myFPS->SetText("--");
	myFPS->SetColor({ 1, 1, 1, 1.0f });

	myMemUsage = std::make_unique<Tga::Text>("Text/arial.ttf");
	myMemUsage->SetText("--");
	myMemUsage->SetColor({ 1, 1, 1, 1.0f });

	myDrawCallText = std::make_unique<Tga::Text>("Text/arial.ttf");
	myDrawCallText->SetText("--");
	myDrawCallText->SetColor({ 1, 1, 1, 1.0f });

	myCPUText = std::make_unique<Tga::Text>("Text/arial.ttf");
	myCPUText->SetText("--");
	myCPUText->SetColor({ 1, 1, 1, 1.0f });

	myLogText = std::make_unique<Tga::Text>("Text/arial.ttf");
	myLogText->SetText("--");
	myLogText->SetColor({ 1, 1, 1, 1.0f });

	myErrorTexture = GraphicsEngine::GetInstance()->GetTextureManager().GetTexture("sprites/error.dds");

	myShowErrorTimer = 0.0f;
	myPerformanceGraph = std::make_unique<PerformanceGraph>(this);
	Tga::Color bgColor(0, 1, 0, 0.4f);
	Tga::Color lineColro(1, 1, 1, 1.0f);
	myPerformanceGraph->Init(bgColor, lineColro, "FPS spike detector");
}


ULONGLONG FixCPUTimings(const FILETIME &a, const FILETIME &b)
{
	LARGE_INTEGER la, lb;
	la.LowPart = a.dwLowDateTime;
	la.HighPart = a.dwHighDateTime;
	lb.LowPart = b.dwLowDateTime;
	lb.HighPart = b.dwHighDateTime;

	return la.QuadPart - lb.QuadPart;
}

float GetCPUUsage(FILETIME *prevSysKernel, FILETIME *prevSysUser,
	FILETIME *prevProcKernel, FILETIME *prevProcUser,
	bool firstRun = false)
{
	FILETIME sysIdle, sysKernel, sysUser;
	FILETIME procCreation, procExit, procKernel, procUser;

	if (!GetSystemTimes(&sysIdle, &sysKernel, &sysUser) ||
		!GetProcessTimes(GetCurrentProcess(), &procCreation, &procExit, &procKernel, &procUser))
	{
		// can't get time info so return
		return -1.;
	}

	// check for first call
	if (firstRun)
	{
		// save time info before return
		prevSysKernel->dwLowDateTime = sysKernel.dwLowDateTime;
		prevSysKernel->dwHighDateTime = sysKernel.dwHighDateTime;

		prevSysUser->dwLowDateTime = sysUser.dwLowDateTime;
		prevSysUser->dwHighDateTime = sysUser.dwHighDateTime;

		prevProcKernel->dwLowDateTime = procKernel.dwLowDateTime;
		prevProcKernel->dwHighDateTime = procKernel.dwHighDateTime;

		prevProcUser->dwLowDateTime = procUser.dwLowDateTime;
		prevProcUser->dwHighDateTime = procUser.dwHighDateTime;

		return -1.;
	}

	ULONGLONG sysKernelDiff = FixCPUTimings(sysKernel, *prevSysKernel);
	ULONGLONG sysUserDiff = FixCPUTimings(sysUser, *prevSysUser);

	ULONGLONG procKernelDiff = FixCPUTimings(procKernel, *prevProcKernel);
	ULONGLONG procUserDiff = FixCPUTimings(procUser, *prevProcUser);

	ULONGLONG sysTotal = sysKernelDiff + sysUserDiff;
	ULONGLONG procTotal = procKernelDiff + procUserDiff;

	return (float)((100.0 * procTotal) / sysTotal);
}


const int CONVERSION_VALUE = 1024;
void DebugDrawer::Update(float aTimeDelta)
{
	if (!myIsEnabled)
	{
		return;
	}

	Vector2ui intResolution = Tga::Application::GetInstance()->GetRenderSize();
	Vector2f resolution = { (float)intResolution.x, (float)intResolution.y };
	const float scale = 1.f;

	myFPS->SetPosition(Vector2f(0, 1.0f-0.035f) * resolution);
	myFPS->SetScale(scale);

	myMemUsage->SetPosition(Vector2f(0.0f, 1.0f - 0.06f) * resolution);
	myMemUsage->SetScale(scale);

	myDrawCallText->SetPosition(Vector2f(0.0f, 1.0f - 0.09f) * resolution);
	myDrawCallText->SetScale(scale);

	myCPUText->SetPosition(Vector2f(0.0f, 1.0f - 0.12f) * resolution);
	myCPUText->SetScale(scale);



	
	myRealFPS = static_cast<unsigned short>(1.0f / aTimeDelta);

	static float timeInter = 0;
	static int iterations = 0;
	iterations++;
	timeInter += aTimeDelta;
	myRealFPSAvarage += myRealFPS;
	static int avarageFPS = 0;
	if (timeInter >= 0.3f)
	{
		timeInter = 0;
		avarageFPS = myRealFPSAvarage / iterations;
		myRealFPSAvarage = 0;
		iterations = 0;
	}

	myPerformanceGraph->FeedValue(myRealFPS);

	FixedStream<64> fpsStream;
	fpsStream.WriteFormatted("FPS: %d", avarageFPS);
	myFPS->SetText(fpsStream.GetStringView());

	PROCESS_MEMORY_COUNTERS memCounter;
	BOOL result = GetProcessMemoryInfo(GetCurrentProcess(),
		&memCounter,
		sizeof(memCounter));

	if (!result)
	{
		return;
	}

	SIZE_T memUsed = (memCounter.WorkingSetSize) / 1024;
	SIZE_T memUsedMb = (memCounter.WorkingSetSize) / 1024 / 1024;

	FixedStream<64> memStream;
	memStream.WriteFormatted("Sys Mem: %lluKb (%lluMb)", static_cast<unsigned long long>(memUsed), static_cast<unsigned long long>(memUsedMb));
	myMemUsage->SetText(memStream.GetStringView());

	static FILETIME prevSysKernel, prevSysUser;
	static FILETIME prevProcKernel, prevProcUser;
	float usage = 0.0;

	// first call
	static bool firstTime = true;
	usage = GetCPUUsage(&prevSysKernel, &prevSysUser, &prevProcKernel, &prevProcUser, firstTime);
	firstTime = false;

	FixedStream<64> cpuStream;
	cpuStream.WriteFormatted("CPU: %d%%", static_cast<int>(usage));
	myCPUText->SetText(cpuStream.GetStringView());

	if (myShowErrorTimer > 0.0f)
	{
		if (!myErrorTexture->myIsFailedTexture)
		{
			SpriteSharedData sharedData = {};
			sharedData.texture = myErrorTexture;

			Sprite2DInstanceData instanceData = {};
			instanceData.position = Vector2f(0.9f, 0.0f) * resolution;
			instanceData.size = myErrorTexture->myImageSize;
			instanceData.pivot = Vector2f(0.5f, 0.0f);

			float randomShake = (((rand() % 100) / 100.0f) - 0.5f) * 0.06f;
			instanceData.rotation = randomShake;
			instanceData.color = Tga::Color(1, 1, 1, std::min(myShowErrorTimer, 1.0f));

			Tga::GraphicsEngine::GetInstance()->GetSpriteDrawer().Draw(sharedData, instanceData);

		}
	}


	myShowErrorTimer -=  aTimeDelta;
	myShowErrorTimer = std::max(myShowErrorTimer, 0.0f);

	unsigned int errors = Log::GetErrorsReported();
	if (errors > myLastErrorsCount)
	{
		myShowErrorTimer = LOG_DISPLAY_DURATION;
	}
	myLastErrorsCount = errors;

	
}


double DebugDrawer::CalcAverageTick(int newtick)
{
	myTickSum -= myTickList[myTickIndex];  /* subtract value falling off */
	myTickSum += newtick;              /* add new value */
	myTickList[myTickIndex] = newtick;   /* save new value so it can be subtracted later */
	if (++myTickIndex == MAXSAMPLES)    /* inc buffer index */
		myTickIndex = 0;

	/* return average */
	return((double)myTickSum / MAXSAMPLES);
}

void DebugDrawer::Render()
{
	if (!myIsEnabled)
	{
		return;
	}

	DX11::BackBuffer->SetAsActiveTarget();

	GraphicsStateStack& graphicsStateStack = Tga::GraphicsEngine::GetInstance()->GetGraphicsStateStack();

	graphicsStateStack.Push();

	graphicsStateStack.SetAmbientLight(AmbientLight{ Color(1.f,1.f,1.f) });
	graphicsStateStack.SetDirectionalLight(DirectionalLight{ {} });
	graphicsStateStack.SetDefaultCamera();

	myPerformanceGraph->Render();

	if (Application::GetInstance()->IsDebugFeatureOn(DebugFeature::Fps))
	{
			myFPS->Render();
		}
	if (Application::GetInstance()->IsDebugFeatureOn(DebugFeature::Mem))
		{
			myMemUsage->Render();
		}

	if (Application::GetInstance()->IsDebugFeatureOn(DebugFeature::Cpu))
		{
			myCPUText->Render();
		}

	if (Application::GetInstance()->IsDebugFeatureOn(DebugFeature::Log))
	{
		const LogEntry* latestEntries[5] = { nullptr };
		Log::GetLatestLogEntries(latestEntries, 5);

		auto currentTime = std::chrono::steady_clock::now();
		Vector2ui intResolution = Tga::Application::GetInstance()->GetRenderSize();
		Vector2f resolution = { (float)intResolution.x, (float)intResolution.y };
		Vector2f basePosition = Vector2f(0.0f, 1.0f - 0.15f) * resolution;
		const float scale = 1.0f;
		float lineSpacing = 24.0f; 

		const LogEntry* visibleEntries[5] = { nullptr };
		size_t visibleCount = 0;
		for (size_t i = 0; i < 5; ++i)
		{
			const LogEntry* entry = latestEntries[i];
			if (!entry)
			{
				continue;
	}

			float age = std::chrono::duration<float>(currentTime - entry->lastLogTime).count();
			if (age <= LOG_DISPLAY_DURATION)
	{
				visibleEntries[visibleCount++] = entry;
	}
		}

		for (size_t j = 0; j < visibleCount; ++j)
	{
			const LogEntry* entry = visibleEntries[visibleCount - 1 - j];
			std::string msg = entry->message;
			if (entry->count > 1)
		{
				msg += " (" + std::to_string(entry->count) + ")";
			}
			myLogText->SetText(msg);
			myLogText->SetScale(scale);

			Vector2f pos = basePosition - Vector2f(0.0f, j * lineSpacing);
			myLogText->SetPosition(pos);

			Color color;
			if (entry->type == LogType::Error)
			{
				color = Color(1.0f, 0.3f, 0.3f, 1.0f);
			}
			else if (entry->type == LogType::Tip)
			{
				color = Color(1.0f, 1.0f, 0.4f, 1.0f);
			}
			else
			{
				color = Color(1.0f, 1.0f, 1.0f, 1.0f);
		}
			myLogText->SetColor(color);

			myLogText->Render();
		}
	}

	if (Application::GetInstance()->IsDebugFeatureOn(DebugFeature::Drawcalls))
	{
		FixedStream<64> drawCallsStream;
		int objCount = DX11::GetPreviousDrawCallCount();
		drawCallsStream.WriteFormatted("DrawCalls: %d", objCount);
		myDrawCallText->SetText(drawCallsStream.GetStringView());
		myDrawCallText->SetColor({ 1, 1, 1, 1 });
		myDrawCallText->Render();
	} 

	graphicsStateStack.Pop();
}

void Tga::DebugDrawer::DrawLine(Vector2f aFrom, Vector2f aTo, Color aColor)
{
	if (myNumberOfRenderedLines > MAX_LINES)
	{
		return;
	}
	(*myLineBuffer)[myNumberOfRenderedLines].fromPosition = Vector3f(aFrom, 0.f);
	(*myLineBuffer)[myNumberOfRenderedLines].toPosition = Vector3f(aTo, 0.f);
	(*myLineBuffer)[myNumberOfRenderedLines].color = aColor.AsVec4();
	myNumberOfRenderedLines++;

	DrawPendingDebugLines();
}



void DebugDrawer::DrawCircle(Vector2f aPos, float aRadius, Color aColor)
{
	const short circleResolution = 32;

	struct MultiLine
	{
		void Zero()
		{
			ZeroMemory(colors, sizeof(Color) * 256);
			ZeroMemory(fromPositions, sizeof(Vector2f) * 256);
			ZeroMemory(toPositions, sizeof(Vector2f) * 256);
		}
		Color colors[256];
		Vector3f fromPositions[256];
		Vector3f toPositions[256];
	};

	MultiLine computedLineBuffer;
	computedLineBuffer.Zero();

	Vector3f to;
	bool first = false;
	int currentCount = 0;
	for (int i = 0; i < circleResolution+1; i++)
	{
		float angle = 2.0f * 3.14f * static_cast<float>(i) / static_cast<float>(circleResolution);
		
		if (!first)
		{
			first = true;
			to = Vector3f( aRadius*cos(angle) + aPos.x, aRadius*sin(angle) + aPos.y, 0.f);
			continue;
		}

		computedLineBuffer.colors[currentCount] = aColor;
		computedLineBuffer.fromPositions[currentCount] = to;
		computedLineBuffer.toPositions[currentCount] = Vector3f(aRadius*cos(angle) + aPos.x, aRadius*sin(angle) + aPos.y, 0.f);

		to = computedLineBuffer.toPositions[currentCount];
		currentCount++;		
	}

	LineMultiPrimitive multiLine;

	multiLine.colors = computedLineBuffer.colors;
	multiLine.fromPositions = computedLineBuffer.fromPositions;
	multiLine.toPositions = computedLineBuffer.toPositions;
	multiLine.count = currentCount;

	LineDrawer& lineDrawer = Tga::GraphicsEngine::GetInstance()->GetLineDrawer();

	lineDrawer.Draw(multiLine);
}

void DebugDrawer::DrawArrow(Vector2f aFrom, Vector2f aTo, Color aColor, float aArrowHeadSize)
{
	if (myNumberOfRenderedLines+3 > MAX_LINES)
	{
		return;
	}

	Vector2f direction = aTo - aFrom;
	direction = direction.Normalize();

	direction *= aArrowHeadSize;

	Vector2f theNormal = direction.Normal();

	(*myLineBuffer)[myNumberOfRenderedLines].fromPosition = Vector3f(aFrom, 0.f);
	(*myLineBuffer)[myNumberOfRenderedLines].toPosition = Vector3f(aTo, 0.f);
	(*myLineBuffer)[myNumberOfRenderedLines].color = aColor.AsVec4();
	myNumberOfRenderedLines++;

	(*myLineBuffer)[myNumberOfRenderedLines].fromPosition = Vector3f(aTo, 0.f);
	(*myLineBuffer)[myNumberOfRenderedLines].toPosition = Vector3f(aTo - direction + theNormal, 0.f);
	(*myLineBuffer)[myNumberOfRenderedLines].color = aColor.AsVec4();
	myNumberOfRenderedLines++;

	(*myLineBuffer)[myNumberOfRenderedLines].fromPosition = Vector3f(aTo, 0.f);
	(*myLineBuffer)[myNumberOfRenderedLines].toPosition = Vector3f(aTo - direction - theNormal, 0.f);
	(*myLineBuffer)[myNumberOfRenderedLines].color = aColor.AsVec4();
	myNumberOfRenderedLines++;

	DrawPendingDebugLines();
}

void DebugDrawer::DrawPendingDebugLines()
{
	LineDrawer& lineDrawer = Tga::GraphicsEngine::GetInstance()->GetLineDrawer();
	for (int i = 0; i < myNumberOfRenderedLines; i++)
	{
		lineDrawer.Draw((*myLineBuffer)[i]);
	}
	myNumberOfRenderedLines = 0;
}
#endif _RETAIL