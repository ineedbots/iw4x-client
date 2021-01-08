#pragma once

namespace Components
{
	class XInput : public Component
	{
	public:
		XInput();
		static Game::dvar_t* input_viewSensitvity;
		static Game::dvar_t* input_invertPitch;
		static Game::dvar_t* gpad_button_lstick_deflect_max;
		static Game::dvar_t* gpad_button_rstick_deflect_max;
		static Game::dvar_t* gpad_button_deadzone;
		static Game::dvar_t* gpad_stick_deadzone_min;
		static Game::dvar_t* gpad_stick_deadzone_max;

	private:
		static XINPUT_STATE xiStates[XUSER_MAX_COUNT];
		static int xiPlayerNum;
		static XINPUT_STATE lastxiState;

		static void CL_FrameStub();
		static void PollXInputDevices();

		static void CL_CreateCmdStub();
		static void CL_GamepadMove(int, Game::usercmd_s*);

		static void MSG_WriteDeltaUsercmdKeyStub();

		static void ApplyMovement(Game::msg_t* msg, int key, Game::usercmd_s* from, Game::usercmd_s* to);

		static void MSG_ReadDeltaUsercmdKeyStub();
		static void MSG_ReadDeltaUsercmdKeyStub2();
	};
}
