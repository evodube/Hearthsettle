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

#include "Higgs.h"
#include "Settings.h"
#include "Settler.h"

namespace
{
	// PlayerCharacter::Update runs once per frame while the game is unpaused.
	// Vtable slot 0xAD on SE/AE and 0xAF on VR (per CommonLibSSE-NG's Actor::Update).
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
			func = vtbl.write_vfunc(REL::Relocate<std::size_t>(0xAD, 0xAD, 0xAF), thunk);
		}
	};

	// HIGGS (VR hand physics) grabs and pulls objects itself, so the game's own grab check
	// never sees them. Let go of anything a HIGGS hand grabs or pulls right away.
	void OnHiggsGrab(bool, RE::TESObjectREFR* a_ref) { Settler::Get().ReleaseRef(a_ref); }

	void ConnectHiggs()
	{
		if (!REL::Module::IsVR()) {
			return;
		}
		const auto higgs = Higgs::Fetch();
		if (!higgs) {
			logger::info("HIGGS not found; using the game's own grab detection");
			return;
		}
		higgs->AddGrabbedCallback(OnHiggsGrab);
		higgs->AddPulledCallback(OnHiggsGrab);
		Settler::Get().SetHiggs(higgs);
		logger::info("HIGGS build {} found: objects grabbed or pulled by your hands are let go immediately", higgs->GetBuildNumber());
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostPostLoad:
			ConnectHiggs();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			Settler::Get().Install();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);

	logger::info("Runtime: {} {}", REL::Module::IsVR() ? "Skyrim VR" : (REL::Module::IsAE() ? "Skyrim AE" : "Skyrim SE"),
		REL::Module::get().version().string());

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
