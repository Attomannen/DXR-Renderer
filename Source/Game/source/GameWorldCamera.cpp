#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "GameWorldImpl.h"

// GameWorld: Scripted bench camera and the free-fly camera.

void GameWorld::Impl::SetScriptedCamera()
{
	// BENCH_FREEZE_FRAME=N holds the scripted camera where it was at frame N,
	// for judging temporal stability with a truly still view.
	const int freezeFrame = bench.freezeFrame;
	const int cameraFrame = freezeFrame > 0 ? std::min(frame, freezeFrame) : frame;
	const float u = benchFrames > 1 ? (float)cameraFrame / (float)(benchFrames - 1) : 0.f;

	if (camLoaded && camMode == CamMode::Fixed)
	{
		camera.GetTransform().SetRotation(camRot);
		camera.GetTransform().SetPosition(camPos);
		return;
	}
	if (camLoaded && camMode == CamMode::Spin)
	{
		// starts and ends at the saved yaw (so the screenshot frame matches),
		// sweeps +/- camSpinDeg through the run
		const float yawOff = camSpinDeg * std::sin(u * 6.28318530718f);
		camera.GetTransform().SetRotation(Vector3f{ camRot.x, camRot.y + yawOff, camRot.z });
		camera.GetTransform().SetPosition(camPos);
		return;
	}

	if (camLoaded && camMode == CamMode::Bob)
	{
		// Pitched at the sky, translating straight up and down: two full
		// bobs over the run, starting and ending at the saved height.
		const int moving = std::max(0, cameraFrame - camBobHoldFrames);
		const int span = std::max(1, benchFrames - 1 - camBobHoldFrames);
		const float ub = (float)moving / (float)span;
		const float yOff = camBobM * std::sin(ub * 6.28318530718f * 2.0f);
		camera.GetTransform().SetRotation(Vector3f{ camRot.x + camBobPitch, camRot.y, camRot.z });
		camera.GetTransform().SetPosition(Vector3f{ camPos.x, camPos.y + yOff, camPos.z });
		return;
	}
	const float ang = u * 6.28318530718f * 2.0f;   // two full orbits over the run

	// Orbit around the saved viewpoint if there is one (unless BENCH_CAM=room),
	// else a point ~1/4 up from the floor (not the bounds centre, which can sit high).
	const Vector3f look = (camLoaded && !orbitRoom) ? camPos
		: sceneCenter + Vector3f{ 0, -sceneExtents.y * 0.25f, 0 };
	// BENCH_ORBIT_HEIGHT pins the orbit height (fraction of scene half-height)
	// instead of letting it oscillate over the run, so camera height can be held
	// constant while something else is A/B tested against it.
	const float orbitY = camOrbitHeight >= 0.f ? camOrbitHeight : (0.10f + 0.08f * std::sin(ang * 0.5f));
	Vector3f pos = look + Vector3f{ std::cos(ang) * orbitRadius,
	                                sceneExtents.y * orbitY,
	                                std::sin(ang) * orbitRadius };

	Vector3f dir = look - pos;
	const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
	if (len > 1e-4f) dir = dir * (1.0f / len);

	const float yaw   = Rad2Deg(std::atan2(dir.x, dir.z));
	const float pitch = Rad2Deg(-std::asin(std::clamp(dir.y, -1.f, 1.f)));

	camera.GetTransform().SetRotation(Vector3f{ pitch, yaw, 0 });
	camera.GetTransform().SetPosition(pos);
}

void GameWorld::Impl::UpdateFreeFly(float dt)
{
	if (!input) return;
	input->Update();

	// Embedded in the editor viewport, WantCaptureMouse is always true -- the
	// viewport is itself an ImGui window -- which froze the camera outright.
	// There the host tells us instead whether the panel owns the mouse.
	const bool embedded = IsEmbedded();
#ifndef _RETAIL
	// Don't drive the camera while the tuning panel has the cursor / keyboard.
	// Once the cursor is trapped it is ours until the button comes up, whatever
	// the host reports about hovering.
	const bool hostInput = embeddedInputActive || mouseTrapped;
	const bool uiMouse = embedded ? !hostInput : ImGui::GetIO().WantCaptureMouse;
	const bool uiKeys  = embedded ? !hostInput : ImGui::GetIO().WantCaptureKeyboard;
#else
	const bool hostInput = embeddedInputActive || mouseTrapped;
	const bool uiMouse = embedded ? !hostInput : false;
	const bool uiKeys  = embedded ? !hostInput : false;
#endif
	if (uiMouse) dt = 0.f;   // freeze movement/look; still process F-key shortcuts below

	if (!uiMouse && input->IsKeyPressed(VK_RBUTTON) && !mouseTrapped)
	{
		input->HideMouse();
		// Embedded, confine the cursor to the panel rather than the window --
		// otherwise a look-around drags it out over the editor's own UI.
		if (embedded)
			input->CaptureMouse(embeddedOrigin.x, embeddedOrigin.y,
				embeddedOrigin.x + (int)embeddedSize.x, embeddedOrigin.y + (int)embeddedSize.y);
		else
			input->CaptureMouse();
		mouseTrapped = true;
	}
	if (input->IsKeyReleased(VK_RBUTTON) && mouseTrapped)
	{
		input->ShowMouse(); input->ReleaseMouse(); mouseTrapped = false;
	}

	// A camera component owns the view: the free-fly controls stay out of its way (the panel,
	// F-keys and Esc below still work).
	const bool freeFly = activeSceneCamera < 0;

	Matrix4x4f rot = Matrix4x4f::CreateFromRollPitchYaw(camRot);
	Vector3f fwd = rot.GetForward();
	Vector3f right = rot.GetRight();
	Vector3f move{ 0,0,0 };
	if (input->IsKeyHeld('W')) move = move + fwd;
	if (input->IsKeyHeld('S')) move = move - fwd;
	if (input->IsKeyHeld('D')) move = move + right;
	if (input->IsKeyHeld('A')) move = move - right;
	if (input->IsKeyHeld('E')) move.y += 1.f;
	if (input->IsKeyHeld('Q')) move.y -= 1.f;
	const float speed = flySpeed * (input->IsKeyHeld(VK_SHIFT) ? 4.f : .4f);
	if (freeFly) camPos = camPos + move * speed * dt;

	if (freeFly && mouseTrapped && !uiMouse)
	{
		const Vector2f md = input->GetMouseDelta();
		camRot.y += md.x * 0.15f;
		camRot.x += md.y * 0.15f;
		camRot.x = std::clamp(camRot.x, -89.f, 89.f);
	}
	if (input->IsKeyPressed(VK_OEM_3)) debugUiOpen = !debugUiOpen;   // ` / ~ toggles the panel
	// Not while embedded: F5 is the editor's Play/Stop toggle.
	if (!uiKeys && !embedded && input->IsKeyPressed(VK_F5)) SaveCamera();
	if (!uiKeys && !embedded && input->IsKeyPressed(VK_F9)) { if (LoadCamera()) INFO_PRINT("bench: camera reloaded"); }
	if (!uiKeys && input->IsKeyPressed(VK_F6))
		INFO_PRINT("bench: camera pos (%.1f, %.1f, %.1f)  rot (%.2f, %.2f, %.2f)",
			camPos.x, camPos.y, camPos.z, camRot.x, camRot.y, camRot.z);
	// Embedded, Escape would take the whole editor down with it.
	if (!embedded && input->IsKeyPressed(VK_ESCAPE)) PostQuitMessage(0);

	if (freeFly)
	{
		camera.GetTransform().SetRotation(camRot);
		camera.GetTransform().SetPosition(camPos);
	}
}

void GameWorld::Impl::SaveCamera()
{
	std::ofstream o(camFile);
	o << "{ \"pos\": [" << camPos.x << ", " << camPos.y << ", " << camPos.z
	  << "], \"rot\": [" << camRot.x << ", " << camRot.y << ", " << camRot.z << "] }\n";
	o.close();
	INFO_PRINT("bench: camera saved -> %s   pos (%.1f, %.1f, %.1f)  rot (%.2f, %.2f, %.2f)",
		camFile.c_str(), camPos.x, camPos.y, camPos.z, camRot.x, camRot.y, camRot.z);
}

bool GameWorld::Impl::LoadCamera()
{
	std::ifstream in(camFile);
	if (!in) in.open(fs::path(Settings::GameAssetRoot()) / camFile);
	if (!in) return false;
	std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	float p[3] = { 0,0,0 }, r[3] = { 0,0,0 };
	size_t pp = s.find("\"pos\"");
	size_t rp = s.find("\"rot\"");
	if (pp == std::string::npos || rp == std::string::npos) return false;
	if (std::sscanf(s.c_str() + s.find('[', pp), "[ %f , %f , %f", &p[0], &p[1], &p[2]) != 3) return false;
	if (std::sscanf(s.c_str() + s.find('[', rp), "[ %f , %f , %f", &r[0], &r[1], &r[2]) != 3) return false;
	camPos = { p[0], p[1], p[2] };
	camRot = { r[0], r[1], r[2] };
	camLoaded = true;
	return true;
}
