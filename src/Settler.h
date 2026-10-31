#pragma once

// Holds freshly loaded loose clutter steady for a short window so Havok's
// load-time overlap correction can't launch it.
//
// For each tracked rigid body, while the window is open:
//   - linear/angular damping are raised, so anything that does move eases into place;
//   - velocities above a small cap are scaled down every frame.
// Sleeping bodies (zero velocity) are never touched, so nothing is woken up.
// When the window closes the original damping is restored and the body is
// left exactly as vanilla would have it.
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
	};

	struct Entry
	{
		RE::ObjectRefHandle handle;
		float               remaining{ 0.0f };
		bool                gathered{ false };
		bool                caught{ false };
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
	};

	Settler() = default;

	bool IsClutter(RE::TESObjectREFR* a_ref) const;
	void QueueCell(RE::TESObjectCELL* a_cell);
	void QueueRef(RE::TESObjectREFR* a_ref);
	void GatherBodies(Entry& a_entry, RE::TESObjectREFR* a_ref);
	void Clamp(Entry& a_entry, RE::TESObjectREFR* a_ref);
	void LogStats();
	void Release(Entry& a_entry);

	std::mutex                                 _lock;
	std::unordered_map<std::uint32_t, Entry>   _entries;  // keyed by ref handle
	std::vector<RE::ObjectRefHandle>           _pending;  // queued from events, merged on the next tick
	Stats                                      _stats;
};
