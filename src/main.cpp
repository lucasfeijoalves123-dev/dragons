#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	struct Element
	{
		std::string              name;
		std::uint8_t             r{ 255 }, g{ 110 }, b{ 20 };
		std::vector<std::string> keys;
		RE::TESEffectShader*     shader{ nullptr };
	};

	struct Config
	{
		float                scale{ 1.15f };
		bool                 glow{ true };
		float                alpha{ 0.35f };
		float                falloff{ 1.5f };
		std::vector<Element> elements;  // Frost, Shock, Fire (Fire = padrao)
	} cfg;

	std::string Lower(std::string s)
	{
		std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	std::string Trim(const std::string& s)
	{
		const auto a = s.find_first_not_of(" \t\r\n");
		if (a == std::string::npos) return {};
		const auto b = s.find_last_not_of(" \t\r\n");
		return s.substr(a, b - a + 1);
	}

	std::vector<std::string> Split(const std::string& s, char sep)
	{
		std::vector<std::string> out;
		std::stringstream        ss(s);
		std::string              item;
		while (std::getline(ss, item, sep)) {
			item = Trim(item);
			if (!item.empty()) out.push_back(item);
		}
		return out;
	}

	void LoadConfig()
	{
		using Section = std::map<std::string, std::string>;
		std::map<std::string, Section> ini;

		std::ifstream f("Data/SKSE/Plugins/DragoesGrandes.ini");
		if (!f) {
			spdlog::warn("DragoesGrandes.ini nao encontrado em Data/SKSE/Plugins; usando valores padrao");
		}
		std::string line, section;
		while (std::getline(f, line)) {
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '#') continue;
			if (line.front() == '[' && line.back() == ']') {
				section = Lower(line.substr(1, line.size() - 2));
				continue;
			}
			if (const auto eq = line.find('='); eq != std::string::npos) {
				ini[section][Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
			}
		}

		auto get = [&](const std::string& sec, const std::string& key, const std::string& def) -> std::string {
			if (const auto s = ini.find(sec); s != ini.end()) {
				if (const auto k = s->second.find(key); k != s->second.end() && !k->second.empty()) return k->second;
			}
			return def;
		};
		auto getf = [&](const std::string& sec, const std::string& key, float def) -> float {
			try {
				return std::stof(get(sec, key, std::to_string(def)));
			} catch (...) {
				return def;
			}
		};

		cfg.scale   = getf("general", "scalemultiplier", 1.15f);
		cfg.glow    = Lower(get("general", "enableglow", "true")) != "false";
		cfg.alpha   = std::clamp(getf("glow", "alpha", 0.35f), 0.0f, 1.0f);
		cfg.falloff = getf("glow", "edgefalloff", 1.5f);

		struct Def
		{
			const char* sec;
			const char* color;
			const char* names;
		};
		const Def defs[] = {
			{ "frost", "90,190,255", "frost,gelo,glacial,ice" },
			{ "shock", "150,110,255", "shock,storm,eletr,tempest,raio,thunder" },
			{ "fire", "255,110,20", "" },
		};
		cfg.elements.clear();
		for (const auto& d : defs) {
			Element e;
			e.name         = d.sec;
			const auto rgb = Split(get(d.sec, "color", d.color), ',');
			if (rgb.size() == 3) {
				try {
					e.r = static_cast<std::uint8_t>(std::clamp(std::stoi(rgb[0]), 0, 255));
					e.g = static_cast<std::uint8_t>(std::clamp(std::stoi(rgb[1]), 0, 255));
					e.b = static_cast<std::uint8_t>(std::clamp(std::stoi(rgb[2]), 0, 255));
				} catch (...) {}
			}
			for (auto& k : Split(get(d.sec, "names", d.names), ',')) e.keys.push_back(Lower(k));
			cfg.elements.push_back(std::move(e));
		}
		spdlog::info("Config: escala x{}, brilho {}, alpha {}, falloff {}", cfg.scale, cfg.glow ? "ligado" : "desligado", cfg.alpha, cfg.falloff);
	}

	bool IsDragonRace(RE::TESRace* a_race)
	{
		return a_race && a_race->HasKeywordString("ActorTypeDragon");
	}

	// ---------- 1) Tamanho: multiplica a altura de toda raca de dragao ----------
	void ScaleDragonRaces()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh) return;
		int n = 0;
		for (auto* race : dh->GetFormArray<RE::TESRace>()) {
			if (!IsDragonRace(race)) continue;
			for (auto& h : race->data.height) h *= cfg.scale;
			++n;
			spdlog::info("Raca de dragao escalada x{}: {:08X}", cfg.scale, race->GetFormID());
		}
		spdlog::info("{} raca(s) de dragao ajustada(s)", n);
	}

	// ---------- 2) Brilho: cria um Effect Shader por elemento ----------
	RE::TESEffectShader* MakeShader(const Element& e)
	{
		auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESEffectShader>();
		if (!factory) return nullptr;
		auto* s = factory->Create();
		if (!s) return nullptr;

		using F = RE::EffectShaderData::Flags;
		auto& d = s->data;
		d       = RE::EffectShaderData{};

		// valores D3D: SRCALPHA=5, ONE=2, BLENDOP_ADD=1 (mistura aditiva = brilho)
		d.membraneShaderSourceBlendMode = static_cast<RE::D3DBLEND>(5);
		d.membraneShaderDestBlendMode   = static_cast<RE::D3DBLEND>(2);
		d.membraneShaderBlendOperation  = static_cast<RE::D3DBLENDOP>(1);
		d.membraneShaderZTestFunction   = RE::D3DCMPFUNC::kLessEqual;

		// preenchimento desligado (alpha 0): so queremos a borda brilhando
		d.fillTextureEffectColorKey1            = RE::Color(e.r, e.g, e.b, 0);
		d.fillTextureEffectColorKey2            = RE::Color(e.r, e.g, e.b, 0);
		d.fillTextureEffectColorKey3            = RE::Color(e.r, e.g, e.b, 0);
		d.fillTextureEffectFullAlphaRatio       = 0.0f;
		d.fillTextureEffectPersistentAlphaRatio = 0.0f;

		// borda (rim glow) na cor do elemento, com pulso leve
		d.edgeEffectColor                = RE::Color(e.r, e.g, e.b, 255);
		d.edgeEffectFallOff              = cfg.falloff;
		d.edgeEffectAlphaFadeInTime      = 1.0f;
		d.edgeEffectFullAlphaTime        = 0.0f;
		d.edgeEffectAlphaFadeOutTime     = 1.0f;
		d.edgeEffectFullAlphaRatio       = cfg.alpha;
		d.edgeEffectPersistentAlphaRatio = cfg.alpha;
		d.edgeEffectAlphaPulseAmplitude  = 0.15f;
		d.edgeEffectAlphaPulseFrequency  = 0.4f;
		d.colorScale                     = 1.0f;

		d.flags = F::kDisableParticleShader;
		return s;
	}

	void BuildShaders()
	{
		for (auto& e : cfg.elements) {
			e.shader = MakeShader(e);
			spdlog::info("Shader '{}' {}", e.name, e.shader ? "criado" : "FALHOU");
		}
	}

	Element* PickElement(RE::Actor* a)
	{
		const char* raw  = a->GetDisplayFullName();
		const auto  name = Lower(raw ? raw : "");
		for (auto& e : cfg.elements) {
			if (e.name == "fire") continue;
			for (const auto& k : e.keys) {
				if (name.find(k) != std::string::npos) return &e;
			}
		}
		for (auto& e : cfg.elements) {
			if (e.name == "fire") return &e;
		}
		return nullptr;
	}

	void ApplyGlow(RE::FormID id)
	{
		auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
		if (!actor || actor->IsDead() || !actor->Is3DLoaded()) return;
		if (!IsDragonRace(actor->GetRace())) return;

		auto* el = PickElement(actor);
		if (!el || !el->shader) return;

		actor->ApplyEffectShader(el->shader, -1.0f);  // -1 = permanente (ate descarregar)
		spdlog::info("Brilho '{}' em {} ({:08X})", el->name, actor->GetDisplayFullName(), id);
	}

	class LoadSink : public RE::BSTEventSink<RE::TESObjectLoadedEvent>
	{
	public:
		static LoadSink* Get()
		{
			static LoadSink s;
			return &s;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_ev, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
		{
			if (a_ev && a_ev->loaded) {
				const auto id = a_ev->formID;
				// adia para o proximo frame: o 3D ainda esta terminando de carregar
				SKSE::GetTaskInterface()->AddTask([id] { ApplyGlow(id); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	void SetupLog()
	{
		auto path = SKSE::log::log_directory();
		if (!path) return;
		*path /= "DragoesGrandes.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log  = std::make_shared<spdlog::logger>("global", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg->type != SKSE::MessagingInterface::kDataLoaded) return;
		LoadConfig();
		ScaleDragonRaces();
		if (cfg.glow) {
			BuildShaders();
			RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(LoadSink::Get());
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SetupLog();
	spdlog::info("DragoesGrandes 1.0.0 carregado");
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
