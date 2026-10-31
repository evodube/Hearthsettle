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

	// Havok stores per-body speed limits as hkUFloat8: a tiny unsigned float with a
	// 5-bit exponent and 3-bit mantissa, value = (1 + m/8) * 2^(e - 8). Skyrim's usual
	// rigid-body template (max linear 104.4, max angular 31.57) is stored as bytes 117
	// and 104. This decoding is checked against the game's own data before use.
	float DecodeUF8(std::uint8_t a_v)
	{
		const int e = a_v >> 3;
		const int m = a_v & 7;
		return (1.0f + m / 8.0f) * std::ldexp(1.0f, e - 8);
	}

	std::uint8_t EncodeUF8(float a_value)
	{
		if (a_value <= DecodeUF8(0)) {
			return 0;
		}
		int   exp = 0;
		float frac = std::frexp(a_value, &exp);  // a_value = frac * 2^exp, frac in [0.5, 1)
		// rewrite as (1 + m/8) * 2^(exp - 1)
		int e = exp - 1 + 8;
		int m = static_cast<int>(std::lround((frac * 2.0f - 1.0f) * 8.0f));
		if (m == 8) {
			m = 0;
			++e;
		}
		if (e > 31) {
			return 255;
		}
		e = std::clamp(e, 0, 31);
		return static_cast<std::uint8_t>((e << 3) | m);
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
			SampleLimits(hk);
			a_entry.bodies.push_back({ RE::NiPointer<RE::bhkRigidBody>(body),
				static_cast<float>(state.linearDamping),
				static_cast<float>(state.angularDamping),
				state.maxLinearVelocity.value,
				state.maxAngularVelocity.value });
			state.linearDamping = s.settleLinearDamping;
			state.angularDamping = s.settleAngularDamping;
		}
		return RE::BSVisit::BSVisitControl::kContinue;
	});

	// No collision yet (Havok can attach a frame after the 3D). Retry until the window closes.
	a_entry.gathered = !a_entry.bodies.empty();
	if (a_entry.gathered) {
		ApplyInStepLimits(a_entry);
	}
}

void Settler::SampleLimits(const RE::hkpRigidBody* a_body)
{
	if (_limitState == LimitState::kUnknown) {
		++_linHist[a_body->motion.motionState.maxLinearVelocity.value];
		++_angHist[a_body->motion.motionState.maxAngularVelocity.value];
		++_samples;
	}
}

void Settler::VerifyLimits()
{
	if (_limitState != LimitState::kUnknown || _samples < 50) {
		return;
	}
	// Skyrim meshes almost all use the same rigid-body template: max linear velocity 104.4
	// and max angular velocity 31.57. If the most common bytes decode to those, the
	// decoding matches this game and is safe to write back. Both must agree.
	const auto modeOf = [](const auto& a_hist) {
		return static_cast<std::uint8_t>(std::ranges::max_element(a_hist) - a_hist.begin());
	};
	const auto within = [](float a_value, float a_target) { return std::abs(a_value - a_target) <= a_target * 0.08f; };
	const float lin = DecodeUF8(modeOf(_linHist));
	const float ang = DecodeUF8(modeOf(_angHist));
	const bool  ok = within(lin, 104.4f) && within(ang, 31.57f);

	const auto summarize = [](const std::array<std::uint32_t, 256>& a_hist) {
		auto order = std::array<std::uint16_t, 256>{};
		for (std::uint16_t i = 0; i < 256; ++i) {
			order[i] = i;
		}
		std::ranges::sort(order, [&](auto a, auto b) { return a_hist[a] > a_hist[b]; });
		std::string out;
		for (int k = 0; k < 4 && a_hist[order[k]] > 0; ++k) {
			out += std::format("{}{}x{} (~{:.1f})", out.empty() ? "" : ", ", a_hist[order[k]], order[k], DecodeUF8(static_cast<std::uint8_t>(order[k])));
		}
		return out;
	};
	const std::string top = std::format("linear {}; angular {}", summarize(_linHist), summarize(_angHist));

	_limitState = ok ? LimitState::kVerified : LimitState::kRejected;
	if (ok) {
		logger::info("In-step speed limit: ON (game values decode as expected; {} bodies sampled: {})", _samples, top);
	} else {
		logger::info("In-step speed limit: OFF, game values didn't decode as expected ({} bodies sampled: {}). Please send this line.", _samples, top);
	}
}

void Settler::ApplyInStepLimits(Entry& a_entry) const
{
	const auto& s = Settings::Get();
	if (!s.inStepLimit || _limitState != LimitState::kVerified) {
		return;
	}
	const auto lin = EncodeUF8(a_entry.pushed ? s.stuckMaxLinearSpeed : s.maxLinearSpeed);
	const auto ang = EncodeUF8(a_entry.pushed ? s.stuckMaxAngularSpeed : s.maxAngularSpeed);
	for (auto& b : a_entry.bodies) {
		if (const auto hk = b.body ? b.body->GetRigidBody() : nullptr) {
			// Never raise a limit above what the mesh already had.
			hk->motion.motionState.maxLinearVelocity.value = (std::min)(lin, b.maxLinear);
			hk->motion.motionState.maxAngularVelocity.value = (std::min)(ang, b.maxAngular);
		}
	}
}

bool Settler::Clamp(Entry& a_entry, RE::TESObjectREFR* a_ref)
{
	const auto& s = Settings::Get();
	bool        any = false;
	for (auto& b : a_entry.bodies) {
		const auto hk = b.body ? b.body->GetRigidBody() : nullptr;
		if (!hk || !hk->world) {
			continue;  // removed from the physics world
		}
		const float speed = std::sqrt(hk->motion.linearVelocity.SqrLength3());
		bool        changed = false;

		const float maxLin = a_entry.pushed ? s.stuckMaxLinearSpeed : s.maxLinearSpeed;
		const float maxAng = a_entry.pushed ? s.stuckMaxAngularSpeed : s.maxAngularSpeed;

		RE::hkVector4 capped;
		if (Cap(hk->motion.linearVelocity, maxLin, capped)) {
			b.body->SetLinearVelocity(capped);
			changed = true;
		}
		if (Cap(hk->motion.angularVelocity, maxAng, capped)) {
			b.body->SetAngularVelocity(capped);
			changed = true;
		}

		if (changed) {
			any = true;
			++a_entry.corrections;
			++_stats.clamps;
			if (!a_entry.caught) {
				a_entry.caught = true;
				++_stats.caught;
			}
			if (speed > _stats.fastest) {
				_stats.fastest = speed;
				const auto name = a_ref->GetName();
				_stats.fastestName = name ? name : "";
				_stats.fastestID = a_ref->GetFormID();
			}
			if (s.debugLog) {
				logger::debug("Caught {} at {:.2f} m/s", Describe(a_ref), speed);
			}
		}
	}
	// Corrected again and again: it's being pushed by something it's stuck in.
	if (any && !a_entry.pushed && a_entry.corrections >= 10) {
		a_entry.pushed = true;
		ApplyInStepLimits(a_entry);
		if (s.debugLog) {
			logger::debug("Pushed: {} gets the tighter limits", Describe(a_ref));
		}
	}
	return any;
}

bool Settler::HasLiveBody(const Entry& a_entry) const
{
	for (const auto& b : a_entry.bodies) {
		const auto hk = b.body ? b.body->GetRigidBody() : nullptr;
		if (hk && hk->world) {
			return true;
		}
	}
	return false;
}

std::string Settler::Describe(RE::TESObjectREFR* a_ref)
{
	const auto name = a_ref ? a_ref->GetName() : nullptr;
	return std::format("{:08X} '{}'", a_ref ? a_ref->GetFormID() : 0u, name ? name : "");
}

void Settler::LogStats()
{
	if (_stats.refs == 0) {
		return;
	}
	if (_stats.caught == 0) {
		logger::info("Settled {} objects ({} bodies): nothing tried to move fast", _stats.refs, _stats.bodies);
	} else {
		logger::info("Settled {} objects ({} bodies): caught {} moving too fast ({} corrections); fastest {:.2f} m/s, {:08X} '{}'",
			_stats.refs, _stats.bodies, _stats.caught, _stats.clamps, _stats.fastest, _stats.fastestID, _stats.fastestName);
	}
	if (_stats.stuck > 0) {
		std::string names;
		for (const auto& n : _stats.stuckNames) {
			names += (names.empty() ? "" : ", ") + n;
		}
		if (_stats.stuck > _stats.stuckNames.size()) {
			names += std::format(" and {} more", _stats.stuck - _stats.stuckNames.size());
		}
		logger::info("  {} stuck (still being pushed), holding until they calm down: {}", _stats.stuck, names);
	}
	spdlog::default_logger()->flush();
	_stats = {};
}

void Settler::Release(Entry& a_entry)
{
	for (auto& b : a_entry.bodies) {
		if (const auto hk = b.body ? b.body->GetRigidBody() : nullptr) {
			hk->motion.motionState.linearDamping = b.linearDamping;
			hk->motion.motionState.angularDamping = b.angularDamping;
			hk->motion.motionState.maxLinearVelocity.value = b.maxLinear;
			hk->motion.motionState.maxAngularVelocity.value = b.maxAngular;
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
		auto [it, inserted] = _entries.try_emplace(handle.native_handle());
		auto& entry = it->second;
		if (inserted) {
			++_stats.refs;
		} else if (entry.stuck || (entry.gathered && !HasLiveBody(entry))) {
			// Held from an earlier visit, or its 3D was rebuilt: hand back the old bodies
			// and start fresh. (Seen twice in the same load is just a window restart.)
			Release(entry);
			entry = Entry{};
			++_stats.refs;
		}
		entry.handle = handle;
		entry.remaining = s.settleSeconds;
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
			if (entry.gathered) {
				_stats.bodies += entry.bodies.size();
			}
		} else if (!ref->Get3D() || !HasLiveBody(entry)) {
			// Its 3D or physics unloaded (left the cell, fast travel): nothing left to hold.
			if (entry.stuck && s.debugLog) {
				logger::debug("Dropped stuck {}: unloaded after {:.1f}s", Describe(ref.get()), entry.held);
			}
			Release(entry);
			it = _entries.erase(it);
			continue;
		}

		if (Clamp(entry, ref.get())) {
			entry.calm = 0.0f;
		}

		if (!loading) {
			entry.remaining -= dt;
			entry.calm += dt;
			entry.held += dt;
		}

		if (entry.remaining <= 0.0f) {
			const bool calm = entry.calm >= s.calmSeconds;
			if (!entry.gathered || calm || !s.holdStuck) {
				if (entry.stuck) {
					logger::info("Released stuck {} after {:.1f}s, it calmed down", Describe(ref.get()), entry.held);
				}
				Release(entry);
				it = _entries.erase(it);
				continue;
			}
			if (!entry.stuck) {
				entry.stuck = true;
				++_stats.stuck;
				if (_stats.stuckNames.size() < 5) {
					_stats.stuckNames.push_back(Describe(ref.get()));
				}
			}
		}
		++it;
	}

	VerifyLimits();

	// Write the summary once only stuck objects (or nothing) are left in this batch.
	if (_stats.refs > 0) {
		const bool onlyStuck = std::ranges::all_of(_entries, [](const auto& a_kv) { return a_kv.second.stuck; });
		if (onlyStuck) {
			LogStats();
		}
	}
}
