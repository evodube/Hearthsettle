#pragma once

// Holds freshly loaded loose clutter steady for a short window so Havok's
// load-time overlap correction can't launch it.
//
// For each tracked rigid body, while the window is open:
//   - linear/angular damping are raised, so anything that does move eases into place;
//   - velocities above a small cap are scaled down every frame.
// Sleeping bodies (zero velocity) are never touched, so nothing is woken up.
// When the window closes, objects that have been calm for a moment get their original
// damping back and behave exactly like vanilla. Objects that are still being pushed
// (usually ones placed inside a shelf or table) are "stuck": they keep being held until
// they calm down, the player picks them up, or their 3D unloads. Nothing here is saved.
class Settler :
	public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>,
	public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	static Settler& Get()
	{
		static Settler instance;
		return instance;
	}

	void Install();

	// Called once per frame from the PlayerCharacter::Update hook (main thread).
	void Tick(float a_delta);

	RE::BSEventNotifyControl ProcessEvent(const RE::TESCellFullyLoadedEvent* a_event, RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;

private:
	struct Body
	{
		RE::NiPointer<RE::bhkRigidBody> body;
		float                           linearDamping;
		float                           angularDamping;
		std::uint8_t                    maxLinear;   // original in-step limits (Havok hkUFloat8)
		std::uint8_t                    maxAngular;
	};

	struct Entry
	{
		RE::ObjectRefHandle handle;
		float               remaining{ 0.0f };
		bool                gathered{ false };
		bool                caught{ false };
		bool                stuck{ false };  // still being pushed when its window ended
		bool                pushed{ false }; // needed many corrections: use the tighter limits
		std::uint32_t       corrections{ 0 };
		float               calm{ 0.0f };    // seconds since the last speed correction
		float               held{ 0.0f };    // seconds held so far
		std::vector<Body>   bodies;
	};

	// Summary of one settle batch, written to the log (always, not just in debug) when it ends.
	struct Stats
	{
		std::size_t refs{ 0 };      // objects tracked
		std::size_t bodies{ 0 };    // rigid bodies held
		std::size_t caught{ 0 };    // objects that tried to move faster than the cap
		std::size_t clamps{ 0 };    // total speed corrections
		float       fastest{ 0.0f };
		std::string fastestName;
		std::uint32_t fastestID{ 0 };
		std::size_t stuck{ 0 };     // objects still being pushed at the end of their window
		std::vector<std::string> stuckNames;
	};

	Settler() = default;

	bool IsClutter(RE::TESObjectREFR* a_ref) const;
	void QueueCell(RE::TESObjectCELL* a_cell);
	void QueueRef(RE::TESObjectREFR* a_ref);
	void GatherBodies(Entry& a_entry, RE::TESObjectREFR* a_ref);
	bool Clamp(Entry& a_entry, RE::TESObjectREFR* a_ref);  // true if any speed was corrected
	bool HasLiveBody(const Entry& a_entry) const;
	void ApplyInStepLimits(Entry& a_entry) const;
	void SampleLimits(const RE::hkpRigidBody* a_body);
	void VerifyLimits();
	void LogStats();
	void Release(Entry& a_entry);
	static std::string Describe(RE::TESObjectREFR* a_ref);

	std::mutex                                 _lock;
	std::unordered_map<std::uint32_t, Entry>   _entries;  // keyed by ref handle
	std::vector<RE::ObjectRefHandle>           _pending;  // queued from events, merged on the next tick
	Stats                                      _stats;

	// In-step limit self-check: histogram of the game's own maxLinearVelocity bytes.
	enum class LimitState { kUnknown, kVerified, kRejected };
	LimitState                                 _limitState{ LimitState::kUnknown };
	std::array<std::uint32_t, 256>             _linHist{};
	std::uint32_t                              _samples{ 0 };
};
