// Hearthsettle - stops loose clutter flying when a cell loads
// Copyright (C) 2026 evodube
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "Settings.h"
#include "Settler.h"

namespace
{
	// PlayerCharacter::Update runs once per frame while the game is unpaused (SE/AE vtable slot 0xAD).
	struct PlayerUpdate
	{
		static void thunk(RE::PlayerCharacter* a_player, float a_delta)
		{
			func(a_player, a_delta);
			Settler::Get().Tick(a_delta);
		}
		static inline REL::Relocation<decltype(thunk)> func;

		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
			func = vtbl.write_vfunc(0xAD, thunk);
		}
	};

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
			Settler::Get().Install();
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);

	if (REL::Module::IsVR()) {
		logger::error("Hearthsettle does not support Skyrim VR");
		return false;
	}

	auto& settings = Settings::Get();
	settings.Load("Data/SKSE/Plugins/Hearthsettle.ini");
	if (settings.debugLog) {
		spdlog::set_level(spdlog::level::debug);
		spdlog::flush_on(spdlog::level::debug);
	}

	if (!settings.enabled) {
		logger::info("Disabled in INI");
		return true;
	}

	logger::info("Settle {:.1f}s, max speed {:.2f} m/s / {:.2f} rad/s, damping {:.1f} / {:.1f}",
		settings.settleSeconds, settings.maxLinearSpeed, settings.maxAngularSpeed,
		settings.settleLinearDamping, settings.settleAngularDamping);

	PlayerUpdate::Install();
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
