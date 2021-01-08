#include "STDInclude.hpp"

namespace Components
{
	XINPUT_STATE XInput::xiStates[XUSER_MAX_COUNT];
	XINPUT_STATE XInput::lastxiState = { 0 };
	int XInput::xiPlayerNum = -1;

	Game::dvar_t* XInput::input_viewSensitvity;
	Game::dvar_t* XInput::input_invertPitch;
	Game::dvar_t* XInput::gpad_button_lstick_deflect_max;
	Game::dvar_t* XInput::gpad_button_rstick_deflect_max;
	Game::dvar_t* XInput::gpad_button_deadzone;
	Game::dvar_t* XInput::gpad_stick_deadzone_min;
	Game::dvar_t* XInput::gpad_stick_deadzone_max;

	void XInput::PollXInputDevices()
	{
		XInput::xiPlayerNum = -1;

		for (int i = XUSER_MAX_COUNT - 1; i >= 0; i--)
		{
			if (XInputGetState(i, &xiStates[i]) == ERROR_SUCCESS)
				XInput::xiPlayerNum = i;
		}
	}

	__declspec(naked) void XInput::CL_FrameStub()
	{
		__asm
		{
			// poll the xinput devices on every client frame
			call XInput::PollXInputDevices

			// execute the code we patched over
			sub     esp, 0Ch
			push    ebx
			push    ebp
			push    esi

			// return back to original code
			push 0x486976
			retn
		}
	}

	float GraphGetValueFromFraction(const int knotCount, const float (*knots)[2], const float fraction)
	{
	  float result;
	  float adjustedFrac;
	  int knotIndex;

	  result = -1.0f;
	  for ( knotIndex = 1; knotIndex < knotCount; ++knotIndex )
	  {
		if ( (*knots)[2 * knotIndex] >= fraction )
		{
		  adjustedFrac = (fraction - (*knots)[2 * knotIndex - 2])
					   / ((*knots)[2 * knotIndex] - (*knots)[2 * knotIndex - 2]);
		  result = (((*knots)[2 * knotIndex + 1] - (*knots)[2 * knotIndex - 1]) * adjustedFrac)
				 + (*knots)[2 * knotIndex - 1];
		  break;
		}
	  }
	  return result;
	}

	float GraphFloat_GetValue(Game::GraphFloat *graph, float fraction)
	{
	  float result;

	  result = GraphGetValueFromFraction(graph->knotCount, graph->knots, fraction);
	  return (result * graph->scale);
	}

	void AimAssist_CalcAdjustedAxis(float inPitch, float inYaw, float *pitchAxis, float *yawAxis)
	{
	  float fraction;
	  float v4;
	  float deflection;
	  float absPitchAxis;
	  float graphScale;
	  float absYawAxis;

	  if ( XInput::aim_input_graph_enabled->current.enabled )
	  {
		deflection = sqrtf((float)(inPitch * inPitch) + (float)(inYaw * inYaw));
		if ( (float)(deflection - 1.0) < 0.0 )
		  v4 = deflection;
		else
		  v4 = 1.0f;
		if ( (float)(0.0 - deflection) < 0.0 )
		  fraction = v4;
		else
		  fraction = 0.0f;

		graphScale = GraphFloat_GetValue(&Game::aaInputGraph[XInput::aim_input_graph_index->current.integer], fraction);
		*pitchAxis = inPitch * graphScale;
		*yawAxis = inYaw * graphScale;
	  }
	  else
	  {
		*pitchAxis = inPitch;
		*yawAxis = inYaw;
	  }

	  if ( XInput::aim_scale_view_axis->current.enabled )
	  {
		absPitchAxis = abs(*pitchAxis);
		absYawAxis = abs(*yawAxis);

		if ( absPitchAxis <= absYawAxis )
		  *pitchAxis = (float)(1.0 - (float)(absYawAxis - absPitchAxis)) * *pitchAxis;
		else
		  *yawAxis = (float)(1.0 - (float)(absPitchAxis - absYawAxis)) * *yawAxis;
	  }
	}

	void AimAssist_ApplyTurnRates(float inPitch, float inYaw, float *pitch, float *yaw)
	{
		float adjustedPitchAxis;
		float adjustedYawAxis;
		float sensitivity;
		float pitchTurnRate;
		float yawTurnRate;
		float pitchDelta;
		float yawDelta;

		AimAssist_CalcAdjustedAxis(inPitch, inYaw, &adjustedPitchAxis, &adjustedYawAxis);

		sensitivity = XInput::aim_view_sensitivity_override->current.value;
		if (sensitivity <= 0.0)
			sensitivity = XInput::input_viewSensitvity->current.value;

		pitchTurnRate = AimAssist_LerpDvars(XInput::aim_turnrate_pitch, XInput::aim_turnrate_pitch_ads, adsLerp);
		pitchTurnRate = *Game::cgameFOVSensitivityScale * sensitivity * pitchTurnRate;
		yawTurnRate = AimAssist_LerpDvars(XInput::aim_turnrate_yaw, XInput::aim_turnrate_yaw_ads, adsLerp);
		yawTurnRate = *Game::cgameFOVSensitivityScale * sensitivity * pitchTurnRate;

		pitchDelta = abs(adjustedPitchAxis) * pitchTurnRate;
		yawDelta = abs(adjustedYawAxis) * yawTurnRate;

		if ( !XInput::aim_accel_turnrate_enabled->current.enabled )
		{
			aaGlob->pitchDelta = pitchDelta;
			aaGlob->yawDelta = yawDelta;
		}
		else
		{
			accel = aim_accel_turnrate_lerp->current.value * sensitivity;
			if ( pitchDelta <= aaGlob->pitchDelta )
			{
				aaGlob->pitchDelta = pitchDelta;
			}
			else
			{
				v2 = LinearTrack(pitchDelta, aaGlob->pitchDelta, accel, input->deltaTime);
				aaGlob->pitchDelta = v2;
			}
			if ( yawDelta <= aaGlob->yawDelta )
			{
				aaGlob->yawDelta = yawDelta;
			}
			else
			{
				v3 = LinearTrack(yawDelta, aaGlob->yawDelta, accel, input->deltaTime);
				aaGlob->yawDelta = v3;
			}
		}
		Game::cl_angles[1] += (float)((float)(pitchDelta * deltaTime) * pitchSign);
		Game::cl_angles[0] += (float)((float)(yawDelta * deltaTime) * yawSign);
	}

	void XInput::CL_GamepadMove(int, Game::usercmd_s* cmd)
	{
		if (XInput::xiPlayerNum != -1)
		{
			XINPUT_STATE* xiState = &xiStates[xiPlayerNum];

			cmd->rightmove = static_cast<BYTE>(xiState->Gamepad.sThumbLX / 256);
			cmd->forwardmove = static_cast<BYTE>(xiState->Gamepad.sThumbLY / 256);

			float normalizedRY = (xiState->Gamepad.sThumbRY / 65535.f);
			if (abs(normalizedRY) < XInput::gpad_stick_deadzone_min->current.value)
				normalizedRY = 0.0f;
			if (abs(normalizedRY) > 1 - XInput::gpad_stick_deadzone_max->current.value)
			{
				if (normalizedRY > 0)
					normalizedRY = 1.0f;
				else
					normalizedRY = -1.0f;
			}

			float normalizedRX = (xiState->Gamepad.sThumbRX / 65535.f);
			if (abs(normalizedRX) < XInput::gpad_stick_deadzone_min->current.value)
				normalizedRX = 0.0f;
			if (abs(normalizedRX) > 1 - XInput::gpad_stick_deadzone_max->current.value)
			{
				if (normalizedRX > 0)
					normalizedRX = 1.0f;
				else
					normalizedRX = -1.0f;
			}

			float smoothFactor = sqrtf(normalizedRY * normalizedRY + normalizedRX * normalizedRX);

			float pitchMultipler = 1.f;
			if (XInput::input_invertPitch->current.value)
				pitchMultipler = -1.f;

			float pitch = normalizedRY * smoothFactor * pitchMultipler;
			float yaw = normalizedRX * smoothFactor;

			

			float frame_msec = static_cast<float>(Utils::Hook::Get<unsigned int>(0xB2BB58));
			if (frame_msec == 0.f)
				frame_msec = 1.f;

			Game::cl_angles[0] -= normalizedRY * smoothFactor * pitchMultipler * *Game::cgameFOVSensitivityScale * XInput::input_viewSensitvity->current.value;
			Game::cl_angles[1] -= normalizedRX * smoothFactor * *Game::cgameFOVSensitivityScale * XInput::input_viewSensitvity->current.value;

			bool pressingLeftTrigger = xiState->Gamepad.bLeftTrigger / 255.f > 0.5;
			if (pressingLeftTrigger != XInput::lastxiState.Gamepad.bLeftTrigger / 255.f > 0.5)
			{
				if (pressingLeftTrigger)
					Command::Execute("+speed");
				else
					Command::Execute("-speed");
			}

			bool pressingRightTrigger = xiState->Gamepad.bRightTrigger / 255.f > 0.5;
			if (pressingRightTrigger != XInput::lastxiState.Gamepad.bRightTrigger / 255.f > 0.5)
			{
				if (pressingRightTrigger)
					Command::Execute("+attack");
				else
					Command::Execute("-attack");
			}

			bool pressingWeapChange = (xiState->Gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0;
			if (pressingWeapChange != ((XInput::lastxiState.Gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0))
			{
				if (pressingWeapChange)
					Command::Execute("weapnext");
			}

			bool pressingReload = (xiState->Gamepad.wButtons & XINPUT_GAMEPAD_X) != 0;
			if (pressingReload != ((XInput::lastxiState.Gamepad.wButtons & XINPUT_GAMEPAD_X) != 0))
			{
				if (pressingReload)
					Command::Execute("+usereload");
				else
					Command::Execute("-usereload");
			}

			bool pressingJump = (xiState->Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
			if (pressingJump != ((XInput::lastxiState.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0))
			{
				if (pressingJump)
					Command::Execute("+gostand");
				else
					Command::Execute("-gostand");
			}

			bool pressingKnife = (xiState->Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0;
			if (pressingKnife != ((XInput::lastxiState.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0))
			{
				if (pressingKnife)
					Command::Execute("+melee");
				else
					Command::Execute("-melee");
			}

			bool pressingSprint = (xiState->Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
			if (pressingSprint != ((XInput::lastxiState.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0))
			{
				if (pressingSprint)
					Command::Execute("+breath_sprint");
				else
					Command::Execute("-breath_sprint");
			}

			bool pressingStance = (xiState->Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
			if (pressingStance != ((XInput::lastxiState.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0))
			{
				if (pressingStance)
					Command::Execute("+stance");
				else
					Command::Execute("-stance");
			}

			bool pressingSmoke = (xiState->Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
			if (pressingSmoke != ((XInput::lastxiState.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0))
			{
				if (pressingSmoke)
					Command::Execute("+smoke");
				else
					Command::Execute("-smoke");
			}

			bool pressingFrag = (xiState->Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
			if (pressingFrag != ((XInput::lastxiState.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0))
			{
				if (pressingFrag)
					Command::Execute("+frag");
				else
					Command::Execute("-frag");
			}

			bool pressingScore = (xiState->Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0;
			if (pressingScore != ((XInput::lastxiState.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0))
			{
				if (pressingScore)
					Command::Execute("+scores");
				else
					Command::Execute("-scores");
			}

			bool pressingAlt = (xiState->Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
			if (pressingAlt != ((XInput::lastxiState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0))
			{
				if (pressingAlt)
					Command::Execute("+actionslot 2");
				else
					Command::Execute("-actionslot 2");
			}

			bool pressingKillstreak = (xiState->Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
			if (pressingKillstreak != ((XInput::lastxiState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0))
			{
				if (pressingKillstreak)
					Command::Execute("+actionslot 3");
				else
					Command::Execute("-actionslot 3");
			}

			bool pressingNight = (xiState->Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
			if (pressingNight != ((XInput::lastxiState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0))
			{
				if (pressingNight)
					Command::Execute("+actionslot 4");
				else
					Command::Execute("-actionslot 4");
			}

			bool pressingUp = (xiState->Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
			if (pressingUp != ((XInput::lastxiState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0))
			{
				if (pressingUp)
					Command::Execute("+actionslot 1");
				else
					Command::Execute("-actionslot 1");
			}

			bool pressingStart = (xiState->Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0;
			if (pressingStart != ((XInput::lastxiState.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0))
			{
				if (pressingStart)
					Command::Execute("togglemenu");
			}


			memcpy(&XInput::lastxiState, xiState, sizeof XINPUT_STATE);
		}
	}

	__declspec(naked) void XInput::CL_CreateCmdStub()
	{
		__asm
		{
			// do xinput!
			push esi
			push ebp
			call XInput::CL_GamepadMove
			add     esp, 8h

			// execute code we patched over
			add     esp, 4
			fld     st
			pop     ebx

			// return back
			push 0x5A6DBF
			retn
		}
	}

	__declspec(naked) void XInput::MSG_WriteDeltaUsercmdKeyStub()
	{
		__asm
		{
			// fix stack pointer
			add esp, 0Ch

			// put both forward move and rightmove values in the movement button
			mov   dl, byte ptr [edi+1Ah] // to_forwardMove
			mov   dh, byte ptr [edi+1Bh] // to_rightMove

			mov     [esp+30h], dx // to_buttons

			mov   dl, byte ptr [ebp+1Ah] // from_forwardMove
			mov   dh, byte ptr [ebp+1Bh] // from_rightMove

			mov     [esp+2Ch], dx // from_buttons
			
			// return back
			push 0x60E40E
			retn
		}
	}

	void XInput::ApplyMovement(Game::msg_t* msg, int key, Game::usercmd_s* from, Game::usercmd_s* to)
	{
		char forward;
		char right;

		if (Game::MSG_ReadBit(msg))
		{
			short movementBits = static_cast<short>(key ^ Game::MSG_ReadBits(msg, 16));

			forward = static_cast<char>(movementBits);
			right = static_cast<char>(movementBits >> 8);
		}
		else
		{
			forward = from->forwardmove;
			right = from->rightmove;
		}
		
		to->forwardmove = forward;
		to->rightmove = right;
	}

	__declspec(naked) void XInput::MSG_ReadDeltaUsercmdKeyStub()
	{
		__asm
		{
			push ebx // to
			push ebp // from
			push edi // key
			push esi // msg
			call XInput::ApplyMovement
			add     esp, 10h

			// return back
			push 0x4921BF
			ret
		}
	}

	__declspec(naked) void XInput::MSG_ReadDeltaUsercmdKeyStub2()
	{
		__asm
		{
			push ebx // to
			push ebp // from
			push edi // key
			push esi // msg
			call XInput::ApplyMovement
			add     esp, 10h

			// return back
			push 3
			push esi
			push 0x492085
			ret
		}
	}

	XInput::XInput()
	{
		// poll xinput devices every client frame
		Utils::Hook(0x486970, XInput::CL_FrameStub, HOOK_JUMP).install()->quick();

		// use the xinput state when creating a usercmd
		Utils::Hook(0x5A6DB9, XInput::CL_CreateCmdStub, HOOK_JUMP).install()->quick();

		// package the forward and right move components in the move buttons
		Utils::Hook(0x60E38D, XInput::MSG_WriteDeltaUsercmdKeyStub, HOOK_JUMP).install()->quick();

		// send two bytes for sending movement data
		Utils::Hook::Set<BYTE>(0x60E501, 16);
		Utils::Hook::Set<BYTE>(0x60E5CD, 16);

		// make sure to parse the movement data properally and apply it
		Utils::Hook(0x492127, XInput::MSG_ReadDeltaUsercmdKeyStub, HOOK_JUMP).install()->quick();
		Utils::Hook(0x492009, XInput::MSG_ReadDeltaUsercmdKeyStub2, HOOK_JUMP).install()->quick();

		XInput::input_viewSensitvity = Game::Dvar_RegisterFloat("input_viewSensitivity", 1.0f, 0.001f, 5.0f, Game::DVAR_FLAG_SAVED, "View Sensitivity");

		XInput::input_invertPitch = Game::Dvar_RegisterBool("input_invertPitch", 0, Game::DVAR_FLAG_SAVED, "Invert gamepad pitch");

		XInput::gpad_button_lstick_deflect_max = Game::Dvar_RegisterFloat(
											"gpad_button_lstick_deflect_max",
											1.0,
											0.0,
											1.0,
											0,
											"Game pad maximum pad stick pressed value");
		XInput::gpad_button_rstick_deflect_max = Game::Dvar_RegisterFloat(
											"gpad_button_rstick_deflect_max",
											1.0,
											0.0,
											1.0,
											0,
											"Game pad maximum pad stick pressed value");
		XInput::gpad_button_deadzone = Game::Dvar_RegisterFloat(
											"gpad_button_deadzone",
											0.13,
											0.0,
											1.0,
											0x80u,
											"Game pad button deadzone threshhold");
		XInput::gpad_stick_deadzone_min = Game::Dvar_RegisterFloat(
											"gpad_stick_deadzone_min",
											0.2,
											0.0,
											1.0,
											0x80u,
											"Game pad minimum stick deadzone");
		XInput::gpad_stick_deadzone_max = Game::Dvar_RegisterFloat(
											"gpad_stick_deadzone_max",
											0.0099999998,
											0.0,
											1.0,
											0x80u,
											"Game pad maximum stick deadzone");
	}
}
