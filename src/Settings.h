#pragma once

// Reads Data/SKSE/Plugins/Hearthsettle.ini. A missing file or key keeps its default.
struct Settings
{
	static Settings& Get()
	{
		static Settings instance;
		return instance;
	}

	// [General]
	bool  enabled{ true };
	bool  debugLog{ false };
	float settleSeconds{ 2.0f };  // how long a freshly loaded object is held steady (game time, after the load screen)

	// [Physics] Havok units: 1 unit ~ 1 metre (~70 Skyrim units)
	float maxLinearSpeed{ 1.0f };   // m/s; anything faster is slowed to this while settling
	float maxAngularSpeed{ 3.0f };  // rad/s
	float settleLinearDamping{ 8.0f };
	float settleAngularDamping{ 8.0f };

	// [Objects] which base form types count as loose clutter
	bool misc{ true };         // plates, cups, cutlery, tools, buckets...
	bool food{ true };         // food, drinks and potions (ALCH)
	bool ingredients{ true };  // INGR
	bool books{ true };
	bool weapons{ true };
	bool armor{ true };
	bool keys{ true };
	bool soulGems{ true };
	bool scrolls{ true };
	bool ammo{ true };
	bool lights{ true };  // carryable lights (lanterns, torches)
	bool movableStatics{ false };

	void Load(const std::filesystem::path& a_path)
	{
		std::ifstream file(a_path);
		if (!file) {
			logger::info("No INI at {}, using defaults", a_path.string());
			return;
		}

		std::string line;
		while (std::getline(file, line)) {
			if (auto c = line.find_first_of(";#"); c != std::string::npos) {
				line.erase(c);
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;  // blank or [section]; section names are cosmetic, keys are unique
			}
			const auto key = Trim(line.substr(0, eq));
			const auto value = Trim(line.substr(eq + 1));
			Apply(key, value);
		}

		settleSeconds = std::clamp(settleSeconds, 0.1f, 30.0f);
		maxLinearSpeed = std::max(maxLinearSpeed, 0.0f);
		maxAngularSpeed = std::max(maxAngularSpeed, 0.0f);
		settleLinearDamping = std::clamp(settleLinearDamping, 0.0f, 100.0f);
		settleAngularDamping = std::clamp(settleAngularDamping, 0.0f, 100.0f);
	}

private:
	static std::string Trim(std::string a_s)
	{
		const auto ws = " \t\r\n";
		const auto b = a_s.find_first_not_of(ws);
		if (b == std::string::npos) {
			return {};
		}
		const auto e = a_s.find_last_not_of(ws);
		return a_s.substr(b, e - b + 1);
	}

	static bool ToBool(const std::string& a_v)
	{
		return a_v == "1" || a_v == "true" || a_v == "True" || a_v == "TRUE";
	}

	static float ToFloat(const std::string& a_v, float a_fallback)
	{
		try {
			return std::stof(a_v);
		} catch (...) {
			return a_fallback;
		}
	}

	void Apply(const std::string& a_key, const std::string& a_value)
	{
		struct BoolKey  { const char* name; bool Settings::*field; };
		struct FloatKey { const char* name; float Settings::*field; };

		static constexpr BoolKey bools[] = {
			{ "bEnabled", &Settings::enabled },
			{ "bDebugLog", &Settings::debugLog },
			{ "bMisc", &Settings::misc },
			{ "bFood", &Settings::food },
			{ "bIngredients", &Settings::ingredients },
			{ "bBooks", &Settings::books },
			{ "bWeapons", &Settings::weapons },
			{ "bArmor", &Settings::armor },
			{ "bKeys", &Settings::keys },
			{ "bSoulGems", &Settings::soulGems },
			{ "bScrolls", &Settings::scrolls },
			{ "bAmmo", &Settings::ammo },
			{ "bLights", &Settings::lights },
			{ "bMovableStatics", &Settings::movableStatics },
		};
		static constexpr FloatKey floats[] = {
			{ "fSettleSeconds", &Settings::settleSeconds },
			{ "fMaxLinearSpeed", &Settings::maxLinearSpeed },
			{ "fMaxAngularSpeed", &Settings::maxAngularSpeed },
			{ "fSettleLinearDamping", &Settings::settleLinearDamping },
			{ "fSettleAngularDamping", &Settings::settleAngularDamping },
		};

		for (const auto& k : bools) {
			if (a_key == k.name) {
				this->*k.field = ToBool(a_value);
				return;
			}
		}
		for (const auto& k : floats) {
			if (a_key == k.name) {
				this->*k.field = ToFloat(a_value, this->*k.field);
				return;
			}
		}
		logger::warn("Unknown INI key '{}'", a_key);
	}
};
