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

#pragma once

// Minimal declaration of HIGGS's mod API (IHiggsInterface001), matching the layout of
// higgsinterface001.h in https://github.com/adamhynek/higgs (GPL-3.0). Only the entries
// up to GetGrabbedObject are declared; their order must match HIGGS exactly, because the
// interface is a table of virtual functions. Nothing after them is called.
namespace Higgs
{
	struct IHiggsInterface001
	{
		virtual unsigned int GetBuildNumber() = 0;

		using PulledCallback = void (*)(bool a_isLeft, RE::TESObjectREFR* a_pulledRefr);
		virtual void AddPulledCallback(PulledCallback a_callback) = 0;

		using GrabbedCallback = void (*)(bool a_isLeft, RE::TESObjectREFR* a_grabbedRefr);
		virtual void AddGrabbedCallback(GrabbedCallback a_callback) = 0;

		using DroppedCallback = void (*)(bool a_isLeft, RE::TESObjectREFR* a_droppedRefr);
		virtual void AddDroppedCallback(DroppedCallback a_callback) = 0;

		using StashedCallback = void (*)(bool a_isLeft, RE::TESForm* a_stashedForm);
		virtual void AddStashedCallback(StashedCallback a_callback) = 0;

		using ConsumedCallback = void (*)(bool a_isLeft, RE::TESForm* a_consumedForm);
		virtual void AddConsumedCallback(ConsumedCallback a_callback) = 0;

		using CollisionCallback = void (*)(bool a_isLeft, float a_mass, float a_separatingVelocity);
		virtual void AddCollisionCallback(CollisionCallback a_callback) = 0;

		virtual void GrabObject(RE::TESObjectREFR* a_object, bool a_isLeft) = 0;

		virtual RE::TESObjectREFR* GetGrabbedObject(bool a_isLeft) = 0;
	};

	// HIGGS hands its API out in reply to this SKSE message.
	struct InterfaceMessage
	{
		enum : std::uint32_t
		{
			kGetInterface = 0xF9279A57
		};
		void* (*GetApiFunction)(unsigned int a_revisionNumber) = nullptr;
	};

	// Call after SKSE's PostLoad message. Returns nullptr if HIGGS isn't installed.
	inline IHiggsInterface001* Fetch()
	{
		const auto messaging = SKSE::GetMessagingInterface();
		if (!messaging) {
			return nullptr;
		}
		InterfaceMessage message;
		messaging->Dispatch(InterfaceMessage::kGetInterface, &message, sizeof(InterfaceMessage*), "HIGGS");
		if (!message.GetApiFunction) {
			return nullptr;
		}
		return static_cast<IHiggsInterface001*>(message.GetApiFunction(1));
	}
}
