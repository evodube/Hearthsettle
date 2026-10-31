#include "Settler.h"

#include "Settings.h"

namespace
{
	bool IsDynamic(const RE::hkpRigidBody* a_body)
	{
		using Motion = RE::hkpMotion::MotionType;
		switch (a_body->motion.type.get()) {
		case Motion::kDynamic:
		case Motion::kSphereInertia:
		case Motion::kBoxInertia:
		case Motion::kThinBoxInertia:
			return true;
		default:
			return false;  // keyframed, fixed, character: not ours to touch
		}
	}

	// Scales a velocity down to a_max if it is faster. Returns true if it changed.
	bool Cap(const RE::hkVector4& a_velocity, float a_max, RE::hkVector4& a_out)
	{
		const float sqr = a_velocity.SqrLength3();
		if (sqr <= a_max * a_max) {
			return false;
		}
		const float scale = a_max / std::sqrt(sqr);
		a_out = a_velocity * RE::hkVector4(scale);
		return true;
	}
}

void Settler::Install()
{
	if (auto events = RE::ScriptEventSourceHolder::GetSingleton()) {
		events->AddEventSink<RE::TESCellFullyLoadedEvent>(this);
	}
	if (auto ui = RE::UI::GetSingleton()) {
		ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
	}
	logger::info("Listening for cell loads");
}

bool Settler::IsClutter(RE::TESObjectREFR* a_ref) const
{
	if (a_ref->IsDisabled() || a_ref->IsMarkedForDeletion()) {
		return false;
	}
	const auto base = a_ref->GetBaseObject();
	if (!base) {
		return false;
	}

	const auto& s = Settings::Get();
	switch (base->GetFormType()) {
	case RE::FormType::Misc:          return s.misc;
	case RE::FormType::AlchemyItem:   return s.food;
	case RE::FormType::Ingredient:    return s.ingredients;
	case RE::FormType::Book:          return s.books;
	case RE::FormType::Weapon:        return s.weapons;
	case RE::FormType::Armor:         return s.armor;
	case RE::FormType::KeyMaster:     return s.keys;
	case RE::FormType::SoulGem:       return s.soulGems;
	case RE::FormType::Scroll:        return s.scrolls;
	case RE::FormType::Ammo:          return s.ammo;
	case RE::FormType::Light:         return s.lights;
	case RE::FormType::MovableStatic: return s.movableStatics;
	default:                          return false;
	}
}

void Settler::QueueCell(RE::TESObjectCELL* a_cell)
{
	if (!a_cell) {
		return;
	}

	std::vector<RE::ObjectRefHandle> found;
	a_cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
		if (a_ref && IsClutter(a_ref)) {
			found.push_back(a_ref->GetHandle());
		}
		return RE::BSContainer::ForEachResult::kContinue;
	});

	if (Settings::Get().debugLog) {
		logger::debug("Cell {:08X} '{}': {} clutter refs queued",
			a_cell->GetFormID(), a_cell->GetFormEditorID() ? a_cell->GetFormEditorID() : "", found.size());
	}

	std::scoped_lock l(_lock);
	_pending.insert(_pending.end(), found.begin(), found.end());
}

void Settler::QueueRef(RE::TESObjectREFR* a_ref)
{
	if (a_ref && IsClutter(a_ref)) {
		std::scoped_lock l(_lock);
		_pending.push_back(a_ref->GetHandle());
	}
}

RE::BSEventNotifyControl Settler::ProcessEvent(const RE::TESCellFullyLoadedEvent* a_event, RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*)
{
	if (a_event && Settings::Get().enabled) {
		QueueCell(a_event->cell);
	}
	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl Settler::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	// When the load screen closes, sweep every loaded cell. This catches loading a save
	// into the same cell, where no "cell loaded" event may fire, and restarts the window
	// for anything already queued, so the timer counts from when you can actually see.
	if (a_event && !a_event->opening && a_event->menuName == RE::LoadingMenu::MENU_NAME && Settings::Get().enabled) {
		if (auto tes = RE::TES::GetSingleton()) {
			tes->ForEachCell([this](RE::TESObjectCELL* a_cell) { QueueCell(a_cell); });
		}
	}
	return RE::BSEventNotifyControl::kContinue;
}

void Settler::GatherBodies(Entry& a_entry, RE::TESObjectREFR* a_ref)
{
	const auto root = a_ref->Get3D();
	if (!root) {
		return;  // 3D not loaded yet; try again next frame
	}

	const auto& s = Settings::Get();
	RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* a_collision) {
		const auto worldObject = a_collision ? a_collision->body.get() : nullptr;
		const auto body = worldObject ? worldObject->AsBhkRigidBody() : nullptr;
		const auto hk = body ? body->GetRigidBody() : nullptr;
		if (hk && IsDynamic(hk)) {
			auto& state = hk->motion.motionState;
			a_entry.bodies.push_back({ RE::NiPointer<RE::bhkRigidBody>(body),
				static_cast<float>(state.linearDamping),
				static_cast<float>(state.angularDamping) });
			state.linearDamping = s.settleLinearDamping;
			state.angularDamping = s.settleAngularDamping;
		}
		return RE::BSVisit::BSVisitControl::kContinue;
	});

	// No collision yet (Havok can attach a frame after the 3D). Retry until the window closes.
	a_entry.gathered = !a_entry.bodies.empty();
}

void Settler::Clamp(Entry& a_entry)
{
	const auto& s = Settings::Get();
	for (auto& b : a_entry.bodies) {
		const auto hk = b.body ? b.body->GetRigidBody() : nullptr;
		if (!hk || !hk->world) {
			continue;  // removed from the physics world
		}
		RE::hkVector4 capped;
		if (Cap(hk->motion.linearVelocity, s.maxLinearSpeed, capped)) {
			b.body->SetLinearVelocity(capped);
		}
		if (Cap(hk->motion.angularVelocity, s.maxAngularSpeed, capped)) {
			b.body->SetAngularVelocity(capped);
		}
	}
}

void Settler::Release(Entry& a_entry)
{
	for (auto& b : a_entry.bodies) {
		if (const auto hk = b.body ? b.body->GetRigidBody() : nullptr) {
			hk->motion.motionState.linearDamping = b.linearDamping;
			hk->motion.motionState.angularDamping = b.angularDamping;
		}
	}
	a_entry.bodies.clear();
}

void Settler::Tick(float a_delta)
{
	const auto& s = Settings::Get();

	std::vector<RE::ObjectRefHandle> pending;
	{
		std::scoped_lock l(_lock);
		pending.swap(_pending);
	}
	for (const auto& handle : pending) {
		if (!handle) {
			continue;
		}
		auto& entry = _entries[handle.native_handle()];
		entry.handle = handle;
		entry.remaining = s.settleSeconds;  // new, or restart the window
	}

	if (_entries.empty()) {
		return;
	}

	const auto ui = RE::UI::GetSingleton();
	const bool loading = ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
	// The first frame after a load screen can be very long; don't let it eat the window.
	const float dt = std::clamp(a_delta, 0.0f, 0.1f);

	const auto player = RE::PlayerCharacter::GetSingleton();
	const auto grabbed = player ? player->GetGrabbedRef() : RE::NiPointer<RE::TESObjectREFR>();

	for (auto it = _entries.begin(); it != _entries.end();) {
		auto& entry = it->second;
		const auto ref = entry.handle.get();

		if (!ref || ref->IsDisabled() || ref->IsMarkedForDeletion() || ref == grabbed) {
			Release(entry);  // gone, or the player picked it up: hand it straight back
			it = _entries.erase(it);
			continue;
		}

		if (!entry.gathered) {
			GatherBodies(entry, ref.get());
		}
		Clamp(entry);

		if (!loading) {
			entry.remaining -= dt;
		}
		if (entry.remaining <= 0.0f) {
			Release(entry);
			it = _entries.erase(it);
			continue;
		}
		++it;
	}
}
