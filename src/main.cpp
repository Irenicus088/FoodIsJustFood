// Food Is Just Food - food feeds you, it doesn't make you a better warrior or mage.
// When the game's data is loaded, takes off every food the effects that raise combat or character stats (health,
// magicka and stamina and their regeneration, resistances, carry weight, speed, skills...). Leaves everything else:
// hunger and warmth, drunkenness, diseases and poisonings, every harmful effect and every scripted one (CACO's
// portions and needs, eating animations...). No plugin of its own: the change lives in memory only.
#include "PCH.h"

namespace
{
	using AV = RE::ActorValue;
	using Arch = RE::EffectSetting::Archetype;

	constexpr auto kJson = "Data/SKSE/Plugins/FoodIsJustFood.json";

	struct Rules
	{
		std::unordered_set<RE::FormID>  keepFoods;
		std::unordered_set<RE::FormID>  keepEffects;
		std::unordered_set<std::string> skipPlugins;  // lower case
	};

	std::string Lower(std::string a_s)
	{
		std::ranges::transform(a_s, a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return a_s;
	}

	void SetupLog()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			return;
		}
		*path /= "FoodIsJustFood.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S] [%l] %v");
	}

	// "Plugin.esp|0x123456" -> the form's runtime ID (0 when the plugin isn't loaded or the text is wrong)
	RE::FormID Resolve(const std::string& a_text)
	{
		const auto bar = a_text.find('|');
		if (bar == std::string::npos) {
			spdlog::warn("Exceptions: '{}' is not Plugin.esp|0x123456", a_text);
			return 0;
		}
		try {
			const auto local = static_cast<RE::FormID>(std::stoul(a_text.substr(bar + 1), nullptr, 16)) & 0xFFFFFF;
			auto* form = RE::TESDataHandler::GetSingleton()->LookupForm(local, a_text.substr(0, bar));
			if (!form) {
				spdlog::info("Exceptions: {} not found (plugin not loaded?)", a_text);
			}
			return form ? form->GetFormID() : 0;
		} catch (...) {
			spdlog::warn("Exceptions: bad ID in '{}'", a_text);
			return 0;
		}
	}

	Rules LoadRules()
	{
		Rules r;
		std::ifstream in(kJson);
		if (!in) {
			spdlog::info("No {} - no exceptions", kJson);
			return r;
		}
		try {
			const auto j = nlohmann::json::parse(in, nullptr, true, true);  // (// notes allowed)
			auto list = [&](const char* a_key) {
				std::vector<std::string> out;
				if (j.contains(a_key) && j[a_key].is_array()) {
					for (const auto& v : j[a_key]) {
						if (v.is_string()) {
							out.push_back(v.get<std::string>());
						}
					}
				}
				return out;
			};
			for (const auto& s : list("keepFoods")) {
				if (const auto id = Resolve(s)) {
					r.keepFoods.insert(id);
				}
			}
			for (const auto& s : list("keepEffects")) {
				if (const auto id = Resolve(s)) {
					r.keepEffects.insert(id);
				}
			}
			for (const auto& s : list("skipPlugins")) {
				r.skipPlugins.insert(Lower(s));
			}
		} catch (const std::exception& e) {
			spdlog::error("{} could not be read ({}) - no exceptions", kJson, e.what());
			return {};
		}
		spdlog::info("Exceptions: {} food(s), {} effect(s), {} plugin(s)", r.keepFoods.size(), r.keepEffects.size(), r.skipPlugins.size());
		return r;
	}

	// a combat or character stat: what a meal shouldn't raise
	bool IsStat(AV a_av)
	{
		const auto v = static_cast<int>(a_av);
		return (v >= 6 && v <= 23)        // skills
		    || (v >= 24 && v <= 30)       // health, magicka, stamina, their regeneration, speed
		    || (v >= 32 && v <= 35)       // carry weight, critical chance, melee and unarmed damage
		    || v == 38                    // shout recovery
		    || (v >= 39 && v <= 45)       // armor and resistances (poison, fire, shock, frost, magic, disease)
		    || (v >= 55 && v <= 57)       // night eye, detect life, water breathing
		    || v == 62 || v == 78         // jumping, bow speed
		    || v == 85 || v == 86         // weapon speed, shout recovery
		    || v == 92                    // movement noise
		    || (v >= 96 && v <= 132)      // skill modifiers, skill experience, left weapon speed
		    || (v >= 134 && v <= 152)     // combat health regeneration, skill power modifiers
		    || (v >= 154 && v <= 157);    // attack damage, health / magicka / stamina regeneration rates
	}

	bool Strippable(const RE::Effect* a_e, const Rules& a_r)
	{
		const auto* m = a_e ? a_e->baseEffect : nullptr;
		if (!m || a_r.keepEffects.contains(m->GetFormID()) || m->IsDetrimental() || m->IsHostile()) {
			return false;
		}
		switch (m->GetArchetype()) {
		case Arch::kValueModifier:
		case Arch::kPeakValueModifier:
			return IsStat(m->data.primaryAV);
		case Arch::kDualValueModifier:
			return IsStat(m->data.primaryAV) && (m->data.secondaryAV == AV::kNone || IsStat(m->data.secondaryAV));
		default:
			return false;  // scripts and everything else stay
		}
	}

	std::string Describe(const RE::Effect* a_e)
	{
		return fmt::format("{} {}{}", a_e->baseEffect->GetName(), a_e->effectItem.magnitude,
			a_e->effectItem.duration ? fmt::format(" for {}s", a_e->effectItem.duration) : std::string{});
	}

	std::string PluginOf(const RE::TESForm* a_f, std::int32_t a_idx)
	{
		const auto* file = a_f->GetFile(a_idx);
		return file ? std::string(file->GetFilename()) : "(made in game)"s;
	}

	void Strip()
	{
		const auto rules = LoadRules();
		auto*      dh = RE::TESDataHandler::GetSingleton();
		if (!dh) {
			return;
		}
		struct Count
		{
			int foods = 0;
			int effects = 0;
		};
		std::map<std::string, Count> perPlugin;  // by the plugin whose version of the food wins
		int foods = 0, effects = 0, kept = 0, zeroed = 0;
		for (auto* f : dh->GetFormArray<RE::AlchemyItem>()) {
			if (!f || !f->IsFood() || f->effects.empty()) {
				continue;
			}
			const auto first = PluginOf(f, 0);
			const auto last = PluginOf(f, -1);
			if (rules.keepFoods.contains(f->GetFormID()) || rules.skipPlugins.contains(Lower(first)) || rules.skipPlugins.contains(Lower(last))) {
				++kept;
				continue;
			}
			std::vector<RE::Effect*> keep;
			std::vector<std::string> gone;
			for (auto* e : f->effects) {
				if (Strippable(e, rules)) {
					gone.push_back(Describe(e));
				} else {
					keep.push_back(e);
				}
			}
			if (gone.empty()) {
				continue;
			}
			const auto value = f->GetGoldValue();  // (the price stays what it was)
			bool       zero = false;
			if (keep.empty()) {
				// the game's item card reads a food's strongest effect without checking, and an item with none
				// crashes the inventory: keep the first one, doing nothing
				auto* e = f->effects[0];
				e->effectItem.magnitude = 0.0f;
				keep.push_back(e);
				zero = true;
				++zeroed;
			}
			f->effects.clear();
			for (auto* e : keep) {
				f->effects.push_back(e);
			}
			if (!f->data.flags.all(RE::AlchemyItem::AlchemyFlag::kCostOverride)) {
				f->data.costOverride = value;
				f->data.flags.set(RE::AlchemyItem::AlchemyFlag::kCostOverride);
			}
			++foods;
			effects += static_cast<int>(gone.size());
			auto& c = perPlugin[last];
			++c.foods;
			c.effects += static_cast<int>(gone.size());
			std::string list;
			for (const auto& g : gone) {
				list += list.empty() ? g : ", " + g;
			}
			spdlog::info("[{}] {} ({:08X}): took off {}{}", last, f->GetName(), f->GetFormID(), list,
				zero ? " (it had nothing else: the first one stays at 0)" : "");
		}
		spdlog::info("---- by plugin");
		for (const auto& [p, c] : perPlugin) {
			spdlog::info("{}: {} food(s), {} effect(s) taken off", p, c.foods, c.effects);
		}
		spdlog::info("---- {} food(s) changed, {} effect(s) taken off, {} left at 0, {} left alone by the exceptions", foods, effects, zeroed, kept);
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
			Strip();
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SetupLog();
	spdlog::info("Food Is Just Food {} loaded", SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."sv));
	if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
		spdlog::error("Could not register with the SKSE messaging interface");
		return false;
	}
	return true;
}
