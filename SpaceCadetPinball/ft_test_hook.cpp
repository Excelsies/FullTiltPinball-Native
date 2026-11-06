#include "pch.h"
#include "ft_test_hook.h"

#ifdef FT_TEST_HOOK
// Native test hook. Everything here is compiled only with FT_TEST_HOOK;
// the hook is inactive unless FT_TEST_SCRIPT or FT_TEST_FIFO is set. It drives inputs and observes; it never
// changes Space Cadet behaviour on its own. Errors are fatal: hook.log gets the reason, exit code 3 (script)
// or 4 (script ended without quit); no silent skip.
#include <cerrno>
#include <cstdlib>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

#include "control.h"
#include "ft_rand.h"
#include "options.h"
#include "pb.h"
#include "render.h"
#include "table_rules.h"
#include "TBall.h"
#include "TLight.h"
#include "TPinballTable.h"
#include "TPlunger.h"
#include "TTextBox.h"
#include "winmain.h"

namespace
{
	struct Cmd
	{
		long Frame;
		std::string Name;
		std::vector<std::string> Args;
	};

	struct Hold
	{
		bool On = false;
		vector3 Pos{}, Dir{};
		float Speed = 0;
		int Mask = 0;
	};

	bool Enabled = false;
	bool Holding = false;
	// init startup_wait 1: the original's 2000 ms start-up wait, in script ticks, before replay_level.
	bool StartupRequested = false, StartupHold = false;
	long StartupTicks = 0;
	bool WindowWait = false, WindowMet = true;
	int WindowW = 0, WindowH = 0;
	std::chrono::steady_clock::time_point WindowDeadline;
	bool QuitPending = false;
	int ExpectExit = 0;
	long Tick = 0;
	double GameTime = 0;
	float FixedDt = 0;
	long DumpPeriod = 0, DumpTicks = 0;
	std::string OutDir;
	FILE *DumpsF = nullptr, *TimelineF = nullptr, *SoundsF = nullptr, *TextF = nullptr, *LogF = nullptr;
	std::vector<Cmd> Script;
	size_t NextCmd = 0;
	long LastFrame = 0;
	bool HasQuit = false;
	bool ScriptLoaded = false;
	struct WatchKey
	{
		std::string Key;
		bool Optional;
	};
	std::vector<WatchKey> Watch;
	std::deque<char> TypeQueue;
	std::vector<Cmd> EndOfFrame; // dump / shot of the current tick
	std::vector<std::string> PendingShots;
	std::map<std::string, std::string> LastTimeline;
	std::vector<Hold> Holds;
	int FifoFd = -1;
	std::string FifoBuf;
	int TextCodeNow = -1;
	std::vector<int> TextIds;
	std::set<std::string> NotedOnce;

	void Flush()
	{
		FILE* files[] = {DumpsF, TimelineF, SoundsF, TextF, LogF};
		for (auto f : files)
			if (f)
				fflush(f);
		fflush(stdout);
	}

	void Note(const std::string& msg)
	{
		if (LogF)
			fprintf(LogF, "%s\n", msg.c_str());
		printf("ft_test_hook: %s\n", msg.c_str());
	}

	void NoteOnce(const std::string& msg)
	{
		if (NotedOnce.insert(msg).second)
			Note(msg);
	}

	[[noreturn]] void Fail(int code, const std::string& msg)
	{
		Note("FATAL (exit " + std::to_string(code) + "): tick " + std::to_string(Tick) + ": " + msg);
		Flush();
		std::_Exit(code);
	}

	std::vector<std::string> Split(const std::string& line)
	{
		std::vector<std::string> out;
		std::istringstream in(line);
		std::string w;
		while (in >> w)
			out.push_back(w);
		return out;
	}

	long ToLong(const std::string& s, const char* what)
	{
		char* end = nullptr;
		long v = strtol(s.c_str(), &end, 0);
		if (s.empty() || *end)
			Fail(3, std::string("bad integer for ") + what + ": '" + s + "'");
		return v;
	}

	double ToDouble(const std::string& s, const char* what)
	{
		char* end = nullptr;
		double v = strtod(s.c_str(), &end);
		if (s.empty() || *end)
			Fail(3, std::string("bad number for ") + what + ": '" + s + "'");
		return v;
	}

	std::string Json(const std::string& s)
	{
		std::string o = "\"";
		for (unsigned char c : s)
		{
			if (c == '"' || c == '\\')
			{
				o += '\\';
				o += static_cast<char>(c);
			}
			else if (c < 0x20)
			{
				char b[8];
				snprintf(b, sizeof b, "\\u%04x", c);
				o += b;
			}
			else
				o += static_cast<char>(c);
		}
		return o + "\"";
	}

	std::string Num(double v)
	{
		char b[40];
		if (v == std::floor(v) && std::fabs(v) < 1e15)
			snprintf(b, sizeof b, "%lld", static_cast<long long>(v));
		else
			snprintf(b, sizeof b, "%.9g", static_cast<double>(static_cast<float>(v)));
		return b;
	}

	double ScriptTime()
	{
		return FixedDt > 0 ? Tick * static_cast<double>(FixedDt) / 1000.0 : GameTime / 1000.0;
	}

	std::string Stamp()
	{
		char b[100];
		snprintf(b, sizeof b, "\"frame\": %ld, \"t\": %.9g", Tick, ScriptTime());
		return b;
	}

	FILE* OpenOut(const char* name)
	{
		auto f = fopen((OutDir + "/" + name).c_str(), "w");
		if (!f)
			Fail(3, std::string("cannot open ") + OutDir + "/" + name);
		return f;
	}

	// ---- components and fields ----------------------------------------------------------------
	TPinballComponent* FindComp(const std::string& name, bool mustExist)
	{
		// TPinballTable::find_component shows a message box for a missing name; search silently.
		auto table = pb::MainTable;
		if (table)
			for (auto c : table->ComponentList)
				if (c && c->GroupName && name == c->GroupName)
					return c;
		if (mustExist)
			Fail(3, "no component named '" + name + "'");
		return nullptr;
	}

	bool CompField(TPinballComponent* c, const std::string& f, double& out)
	{
		if (c->HookGet(f, out))
			return true;
		if (f == "active") { out = c->ActiveFlag; return true; }
		if (f == "message_field") { out = c->MessageField; return true; }
		if (auto l = dynamic_cast<TLight*>(c))
		{
			if (f == "light_on") { out = l->LightOnFlag; return true; }
			if (f == "flasher_on") { out = l->FlasherOnFlag; return true; }
			if (f == "override") { out = l->TemporaryOverrideFlag; return true; }
			if (f == "bitmap") { out = l->PreviousBitmap; return true; } // UNVERIFIED while an override shows another sprite
		}
		if (auto p = dynamic_cast<TPlunger*>(c))
		{
			if (f == "max_pullback") { out = p->MaxPullback; return true; }
			if (f == "pullback_started") { out = p->PullbackStartedFlag; return true; }
		}
		return false;
	}

	// ---- keys ---------------------------------------------------------------------------------
	typedef std::vector<std::pair<std::string, std::string>> KV;

	void StandardKeys(KV& kv)
	{
		auto t = pb::MainTable;
		auto add = [&kv](const char* k, double v) { kv.emplace_back(k, Num(v)); };
		if (t)
		{
			add("player_count", t->PlayerCount);
			add("current_player", t->CurrentPlayer);
			add("multiplier", t->ScoreMultiplier);
			add("jackpot", t->JackpotScore);
			add("ball_count", t->BallCount);
			add("extra_balls", t->ExtraBalls);
			add("multiball", t->MultiballCount);
			add("tilt_lock", t->TiltLockFlag);
			add("super_jackpot", t->SuperJackpotScore);
		}
		if (control::Rules)
		{
			std::vector<std::pair<const char*, int>> rk;
			control::Rules->DumpState(rk);
			for (auto& p : rk)
				add(p.first, p.second);
		}
		add("n_frame", static_cast<double>(Tick));
		add("n_cheat", pb::cheat_mode);
		add("n_rand_draws", static_cast<double>(ft_rand::DrawCount()));
		add("n_rand_last", ft_rand::LastValue());
		if (t)
		{
			add("n_score", t->CurScoreE9 * 1000000000.0 + t->CurScore);
			int active = 0, placed = 0;
			for (auto b : t->BallList)
				if (b->ActiveFlag)
				{
					active++;
					if (b->HasGroupFlag)
						placed++;
				}
			add("n_active_balls", active);
			add("n_placed_balls", placed);
			for (size_t i = 0; i < t->BallList.size(); i++)
			{
				auto b = t->BallList[i];
				std::string p = "n_ball" + std::to_string(i) + "_";
				add((p + "x").c_str(), b->Position.X);
				add((p + "y").c_str(), b->Position.Y);
				add((p + "z").c_str(), b->Position.Z);
				add((p + "dx").c_str(), b->Direction.X);
				add((p + "dy").c_str(), b->Direction.Y);
				add((p + "vx").c_str(), b->Direction.X * b->Speed);
				add((p + "vy").c_str(), b->Direction.Y * b->Speed);
				add((p + "speed").c_str(), b->Speed);
				add((p + "active").c_str(), b->ActiveFlag);
				add((p + "mask").c_str(), b->CollisionMask);
				add((p + "placed").c_str(), b->HasGroupFlag);
			}
		}
	}

	// Value text of one part-field key; false when the part or field does not resolve.
	bool PartKey(const std::string& key, std::string& out)
	{
		auto dot = key.find('.');
		if (dot == std::string::npos)
			return false;
		auto c = FindComp(key.substr(0, dot), false);
		double v;
		if (!c || !CompField(c, key.substr(dot + 1), v))
			return false;
		out = Num(v);
		return true;
	}

	void AddWatch(const std::string& spec)
	{
		bool opt = !spec.empty() && spec[0] == '?';
		std::string key = opt ? spec.substr(1) : spec;
		if (key.find('.') == std::string::npos)
		{
			// a rule / standard key: validated when rules exist (it may legitimately be absent before)
			Watch.push_back({key, true});
			return;
		}
		if (key.compare(0, 2, "*.") == 0)
		{
			auto field = key.substr(2);
			int n = 0;
			for (auto c : pb::MainTable->ComponentList)
			{
				double v;
				if (c && c->GroupName && CompField(c, field, v))
				{
					Watch.push_back({std::string(c->GroupName) + "." + field, true});
					n++;
				}
			}
			if (!n && !opt)
				Fail(3, "init watch " + key + ": no component has that field");
			return;
		}
		std::string v;
		if (!PartKey(key, v) && !opt)
			Fail(3, "init watch " + key + ": unknown part or field");
		Watch.push_back({key, opt});
	}

	void WatchKeys(KV& kv, const std::vector<WatchKey>& keys)
	{
		for (auto& w : keys)
		{
			std::string v;
			if (w.Key.find('.') != std::string::npos)
			{
				if (PartKey(w.Key, v))
					kv.emplace_back(w.Key, v);
				else
				{
					NoteOnce("watch key '" + w.Key + "' does not resolve; written as null");
					kv.emplace_back(w.Key, "null");
				}
			}
		}
	}

	std::string Line(const char* label, const KV& kv)
	{
		std::string s = "{";
		if (label)
			s += "\"label\": " + Json(label) + ", ";
		char b[100];
		snprintf(b, sizeof b, "\"frame\": %ld, \"t\": %.9g, \"gt\": %.9g", Tick, ScriptTime(), GameTime);
		s += b;
		for (auto& p : kv)
			s += ", " + Json(p.first) + ": " + p.second;
		return s + "}";
	}

	void WriteDump(const std::string& label, const std::vector<std::string>& extra)
	{
		KV kv;
		StandardKeys(kv);
		WatchKeys(kv, Watch);
		for (auto& spec : extra)
		{
			bool opt = !spec.empty() && spec[0] == '?';
			std::string key = opt ? spec.substr(1) : spec;
			std::string v;
			if (PartKey(key, v))
				kv.emplace_back(key, v);
			else if (opt)
			{
				NoteOnce("dump key '" + key + "' does not resolve; written as null");
				kv.emplace_back(key, "null");
			}
			else
				Fail(3, "dump key '" + key + "' does not resolve");
		}
		fprintf(DumpsF, "%s\n", Line(label.c_str(), kv).c_str());
	}

	void WriteTimeline()
	{
		KV all, changed;
		StandardKeys(all);
		WatchKeys(all, Watch);
		for (auto& p : all)
		{
			if (p.first == "n_frame")
				continue;
			auto it = LastTimeline.find(p.first);
			if (it == LastTimeline.end() || it->second != p.second)
			{
				changed.push_back(p);
				LastTimeline[p.first] = p.second;
			}
		}
		if (!changed.empty())
			fprintf(TimelineF, "%s\n", Line(nullptr, changed).c_str());
	}

	void Capture(const std::string& name)
	{
		int w, h;
		if (SDL_GetRendererOutputSize(winmain::Renderer, &w, &h) != 0)
			Fail(3, std::string("shot: ") + SDL_GetError());
		std::vector<uint32_t> px(static_cast<size_t>(w) * h);
		if (SDL_RenderReadPixels(winmain::Renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, px.data(), w * 4) != 0)
			Fail(3, std::string("shot: SDL_RenderReadPixels: ") + SDL_GetError());
		auto surf = SDL_CreateRGBSurfaceWithFormatFrom(px.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_ARGB8888);
		if (!surf)
			Fail(3, std::string("shot: ") + SDL_GetError());
		std::string path = OutDir + "/frames/" + name + ".bmp";
		if (SDL_SaveBMP(surf, path.c_str()) != 0)
			Fail(3, "shot: cannot write " + path);
		SDL_FreeSurface(surf);
	}

	// ---- commands -----------------------------------------------------------------------------
	int KeySym(const std::string& n)
	{
		if (n == "left_flipper") return SDLK_z;
		if (n == "right_flipper") return SDLK_SLASH;
		if (n == "plunger") return SDLK_SPACE;
		if (n == "bump_left") return SDLK_x;
		if (n == "bump_right") return SDLK_PERIOD;
		if (n == "bump_bottom") return SDLK_UP;
		if (n == "new_game") return SDLK_F2;
		if (n == "pause") return SDLK_F3;
		Fail(3, "unknown key name '" + n + "'");
	}

	void NeedArgs(const Cmd& c, size_t lo, size_t hi)
	{
		if (c.Args.size() < lo || c.Args.size() > hi)
			Fail(3, "command '" + c.Name + "': wrong number of arguments");
	}

	bool OnOff(const std::string& s)
	{
		if (s == "on") return true;
		if (s == "off") return false;
		Fail(3, "expected on/off, got '" + s + "'");
	}

	void DoPoke(const Cmd& c)
	{
		NeedArgs(c, 3, 3);
		auto comp = FindComp(c.Args[0], true);
		auto& f = c.Args[1];
		double v = ToDouble(c.Args[2], "poke value");
		if (f == "message_field")
			comp->MessageField = static_cast<int>(v);
		else if (f == "active")
			comp->ActiveFlag = static_cast<char>(v);
		else if (f == "light_on")
		{
			auto l = dynamic_cast<TLight*>(comp);
			if (!l)
				Fail(3, "poke light_on: '" + c.Args[0] + "' is not a light");
			l->LightOnFlag = v != 0;
			// TLight's own turn-on/off case, minus timers, sounds and control::handler
			if (!l->FlasherOnFlag && !l->ToggledOffFlag && !l->ToggledOnFlag)
				l->SetSpriteBmp(l->BmpArr[l->LightOnFlag ? 1 : 0]);
		}
		else if (!comp->HookSet(f, v))
			Fail(3, "poke: '" + c.Args[0] + "' has no settable field '" + f + "'");
	}

	void DoPlace(const Cmd& c)
	{
		NeedArgs(c, 4, 8);
		if (c.Args.size() != 4 && c.Args.size() != 7 && c.Args.size() != 8)
			Fail(3, "place: expects 4, 7 or 8 arguments (index x y z [dx dy speed [mask]])");
		auto t = pb::MainTable;
		size_t i = static_cast<size_t>(ToLong(c.Args[0], "ball index"));
		vector2 p{static_cast<float>(ToDouble(c.Args[1], "x")), static_cast<float>(ToDouble(c.Args[2], "y"))};
		if (i == t->BallList.size())
			t->AddBall(p);
		if (i >= t->BallList.size())
			Fail(3, "place: ball index " + c.Args[0] + " beyond the next free one");
		auto b = t->BallList[i];
		b->ActiveFlag = 1;
		b->Position = {p.X, p.Y, static_cast<float>(ToDouble(c.Args[3], "z"))};
		b->PrevPosition = b->Position;
		b->Direction = {0, 0, 0};
		b->Speed = 0;
		if (c.Args.size() >= 7)
		{
			b->Direction = {static_cast<float>(ToDouble(c.Args[4], "dx")),
			                static_cast<float>(ToDouble(c.Args[5], "dy")), 0};
			b->Speed = static_cast<float>(ToDouble(c.Args[6], "speed"));
		}
		if (c.Args.size() == 8)
		{
			const char* ms = c.Args[7].c_str();
			char* end = nullptr;
			errno = 0;
			long m = strtol(ms, &end, 16);
			if (end == ms || *end != '\0' || errno != 0)
				Fail(3, "place: bad hex mask '" + c.Args[7] + "'");
			b->CollisionMask = static_cast<int>(m);
		}
		if (Holds.size() <= i)
			Holds.resize(i + 1);
		Holds[i].Pos = b->Position;
		Holds[i].Dir = b->Direction;
		Holds[i].Speed = b->Speed;
		Holds[i].Mask = b->CollisionMask;
	}

	void DoType(const std::string& s)
	{
		for (size_t i = 0; i < s.size(); i++)
		{
			if (s[i] == '\\' && i + 1 < s.size())
			{
				i++;
				if (s[i] == 's') TypeQueue.push_back(' ');
				else if (s[i] == 't') TypeQueue.push_back('\t');
				else if (s[i] == '\\') TypeQueue.push_back('\\');
				else Fail(3, "type: unknown escape");
			}
			else
				TypeQueue.push_back(s[i]);
		}
	}

	void Exec(const Cmd& c)
	{
		auto& n = c.Name;
		auto table = pb::MainTable;
		if (n == "seed")
		{
			NeedArgs(c, 1, 1);
			ft_rand::Seed(static_cast<uint32_t>(ToLong(c.Args[0], "seed")));
		}
		else if (n == "key")
		{
			NeedArgs(c, 2, 2);
			bool down = c.Args[0] == "down";
			if (!down && c.Args[0] != "up")
				Fail(3, "key: expected down/up");
			if (c.Args[1] == "demo")
			{
				if (down)
					pb::toggle_demo();
			}
			else if (down)
				pb::InputDown({InputTypes::Keyboard, KeySym(c.Args[1])});
			else
				pb::InputUp({InputTypes::Keyboard, KeySym(c.Args[1])});
		}
		else if (n == "type")
		{
			NeedArgs(c, 1, 1);
			DoType(c.Args[0]);
		}
		else if (n == "event")
		{
			NeedArgs(c, 2, 2);
			auto code = static_cast<MessageCode>(ToLong(c.Args[1], "code"));
			if (c.Args[0] == "table")
				control::table_control_handler(code);
			else
				control::handler(code, FindComp(c.Args[0], true));
		}
		else if (n == "msg")
		{
			NeedArgs(c, 3, 3);
			auto code = static_cast<MessageCode>(ToLong(c.Args[1], "code"));
			float v = static_cast<float>(ToDouble(c.Args[2], "value"));
			if (c.Args[0] == "table")
				table->Message(code, v);
			else
				FindComp(c.Args[0], true)->Message(code, v);
		}
		else if (n == "text")
		{
			NeedArgs(c, 1, 1);
			if (!control::TextEvent(static_cast<int>(ToLong(c.Args[0], "text code"))))
				Fail(3, "text: control::Rules is null (no FT text sites on this table)");
		}
		else if (n == "poke")
			DoPoke(c);
		else if (n == "place")
			DoPlace(c);
		else if (n == "hold")
		{
			NeedArgs(c, 2, 2);
			size_t i = static_cast<size_t>(ToLong(c.Args[0], "ball index"));
			if (i >= Holds.size() || i >= table->BallList.size())
				Fail(3, "hold: ball " + c.Args[0] + " was never placed");
			Holds[i].On = OnOff(c.Args[1]);
		}
		else if (n == "dump")
			EndOfFrame.push_back(c);
		else if (n == "shot")
		{
			NeedArgs(c, 1, 1);
			PendingShots.push_back(c.Args[0]); // captured in AfterRender, before the present
		}
		else if (n == "quit")
			QuitPending = true;
		else
			Fail(3, "unknown command '" + n + "'");
	}

	Cmd ParseLine(const std::string& raw, long frame, bool hasFrame)
	{
		auto w = Split(raw);
		Cmd c;
		c.Frame = frame;
		size_t at = 0;
		if (hasFrame)
		{
			c.Frame = ToLong(w[0], "frame");
			at = 1;
		}
		if (w.size() <= at)
			Fail(3, "empty command: '" + raw + "'");
		c.Name = w[at];
		c.Args.assign(w.begin() + at + 1, w.end());
		return c;
	}

	const std::string& InitArg(const Cmd& c)
	{
		if (c.Args.size() != 2)
			Fail(3, "init " + c.Args[0] + " needs exactly one value");
		return c.Args[1];
	}

	void ParseInit(const Cmd& c)
	{
		if (c.Args.empty())
			Fail(3, "init without a key");
		auto& k = c.Args[0];
		if (k == "fixed_dt")
			FixedDt = static_cast<float>(ToDouble(InitArg(c), "fixed_dt"));
		else if (k == "seed")
			ft_rand::Seed(static_cast<uint32_t>(ToLong(InitArg(c), "seed")));
		else if (k == "window")
		{
			if (c.Args.size() != 3)
				Fail(3, "init window <w> <h>");
			WindowW = static_cast<int>(ToLong(c.Args[1], "w"));
			WindowH = static_cast<int>(ToLong(c.Args[2], "h"));
			WindowWait = true;
			WindowMet = false;
			WindowDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
		}
		else if (k == "dump_period")
			DumpPeriod = ToLong(InitArg(c), "dump_period");
		else if (k == "watch")
			for (size_t i = 1; i < c.Args.size(); i++)
				AddWatch(c.Args[i]);
		else if (k == "startup_wait")
		{
			auto v = ToLong(InitArg(c), "startup_wait");
			if (v != 0 && v != 1)
				Fail(3, "init startup_wait must be 0 or 1");
			if (v == 1 && !pb::IsFullTiltTable())
				Fail(3, "init startup_wait 1 is only valid on a Full Tilt table (Space Cadet has no start-up wait)");
			StartupRequested = v == 1;
		}
		else if (k == "expect_exit")
			ExpectExit = static_cast<int>(ToLong(InitArg(c), "expect_exit"));
		else
			Fail(3, "unknown init key '" + k + "' (init option is read by hookrun.py, not the binary)");
	}

	void LoadScript(const char* path)
	{
		std::ifstream in(path);
		if (!in)
			Fail(3, std::string("cannot read FT_TEST_SCRIPT ") + path);
		std::string line;
		std::vector<Cmd> inits;
		long prev = 0;
		while (std::getline(in, line))
		{
			auto hash = line.find('#');
			if (hash != std::string::npos)
				line.erase(hash);
			if (Split(line).empty())
				continue;
			if (Split(line)[0] == "init")
			{
				inits.push_back(ParseLine(line, 0, false));
				continue;
			}
			auto c = ParseLine(line, 0, true);
			if (c.Frame < prev)
				Fail(3, "frames not ascending at: " + line);
			prev = c.Frame;
			Script.push_back(c);
			if (c.Name == "quit")
				HasQuit = true;
		}
		LastFrame = prev;
		for (auto& i : inits)
		{
			ParseInit(i);
		}
		if (StartupRequested)
		{
			double ratio = FixedDt > 0 ? 2000.0 / static_cast<double>(FixedDt) : 0;
			StartupTicks = std::lround(ratio);
			if (StartupTicks < 1 || std::fabs(ratio - StartupTicks) > 1e-3)
				Fail(3, "init startup_wait 1 needs fixed_dt set and dividing 2000 ms");
			StartupHold = true;
		}
		if (DumpPeriod > 0)
		{
			double ratio = FixedDt > 0 ? DumpPeriod / static_cast<double>(FixedDt) : 0;
			DumpTicks = std::lround(ratio);
			if (DumpTicks < 1 || std::fabs(ratio - DumpTicks) > 1e-3)
				Fail(3, "init dump_period must be a multiple of fixed_dt");
		}
	}

	void ReadFifo()
	{
		if (FifoFd < 0)
			return;
		char buf[512];
		ssize_t n;
		while ((n = read(FifoFd, buf, sizeof buf)) > 0)
			FifoBuf.append(buf, static_cast<size_t>(n));
		size_t nl;
		while ((nl = FifoBuf.find('\n')) != std::string::npos)
		{
			auto line = FifoBuf.substr(0, nl);
			FifoBuf.erase(0, nl + 1);
			if (!Split(line).empty())
				Exec(ParseLine(line, Tick, false));
		}
	}
}

void ft_test_hook::Init()
{
	auto script = getenv("FT_TEST_SCRIPT");
	auto fifo = getenv("FT_TEST_FIFO");
	if (!script && !fifo)
		return;
	auto out = getenv("FT_TEST_OUT");
	if (!out || !*out)
	{
		fprintf(stderr, "ft_test_hook: FT_TEST_OUT is required with a script\n");
		std::_Exit(3);
	}
	OutDir = out;
	mkdir((OutDir).c_str(), 0777);
	mkdir((OutDir + "/frames").c_str(), 0777);
	LogF = OpenOut("hook.log");
	DumpsF = OpenOut("dumps.jsonl");
	TimelineF = OpenOut("timeline.jsonl");
	SoundsF = OpenOut("sounds.jsonl");
	TextF = OpenOut("text.jsonl");
	Enabled = true;
	if (!pb::MainTable)
		Fail(3, "Init called before the table is loaded");
	if (script)
	{
		LoadScript(script);
		ScriptLoaded = true;
	}
	if (fifo)
	{
		FifoFd = open(fifo, O_RDONLY | O_NONBLOCK);
		if (FifoFd < 0)
			Fail(3, std::string("cannot open FT_TEST_FIFO ") + fifo);
	}
}

bool ft_test_hook::Active() { return Enabled; }

bool ft_test_hook::DefersReplay() { return Enabled && StartupRequested; }

float ft_test_hook::FrameDt(float measuredMs)
{
	if (!Enabled)
		return measuredMs;
	float dt = FixedDt > 0 ? FixedDt : measuredMs;
	GameTime += dt;
	return dt;
}

bool ft_test_hook::HoldFrame() { return Enabled && (Holding || StartupHold); }

void ft_test_hook::BeforeFrame()
{
	if (!Enabled)
		return;
	Holding = false;
	if (!WindowMet)
	{
		int w = 0, h = 0;
		SDL_GetRendererOutputSize(winmain::Renderer, &w, &h);
		if (w == WindowW && h == WindowH)
			WindowMet = true;
		else
		{
			if (std::chrono::steady_clock::now() > WindowDeadline)
				Fail(3, "init window " + std::to_string(WindowW) + "x" + std::to_string(WindowH) +
				     " not met within 30 s (renderer is " + std::to_string(w) + "x" + std::to_string(h) + ")");
			Holding = true;
			return;
		}
	}
	if (StartupHold)
	{
		// Original: UpdateWindow paints the table, then the 2000 ms wait; no game frame runs.
		if (Tick >= StartupTicks)
		{
			StartupHold = false;
			pb::replay_level(false);
		}
		else
			render::update();
	}
	for (size_t i = 0; i < Holds.size(); i++)
	{
		if (!Holds[i].On || i >= pb::MainTable->BallList.size())
			continue;
		auto b = pb::MainTable->BallList[i];
		b->Position = Holds[i].Pos;
		b->Direction = Holds[i].Dir;
		b->Speed = Holds[i].Speed;
		b->CollisionMask = Holds[i].Mask;
	}
	while (NextCmd < Script.size() && Script[NextCmd].Frame <= Tick)
		Exec(Script[NextCmd++]);
	ReadFifo();
	if (!TypeQueue.empty())
	{
		auto ch = static_cast<unsigned char>(TypeQueue.front());
		TypeQueue.pop_front();
		pb::InputDown({InputTypes::Keyboard, ch});
		pb::InputUp({InputTypes::Keyboard, ch});
	}
}

void ft_test_hook::AfterRender(SDL_Renderer*)
{
	if (!Enabled || Holding)
		return;
	for (auto& s : PendingShots)
		Capture(s);
	PendingShots.clear();
}

void ft_test_hook::AfterFrame()
{
	if (!Enabled || Holding)
		return;
	for (auto& c : EndOfFrame)
	{
		if (c.Args.empty())
			Fail(3, "dump needs a label");
		WriteDump(c.Args[0], std::vector<std::string>(c.Args.begin() + 1, c.Args.end()));
	}
	EndOfFrame.clear();
	if (!PendingShots.empty())
		Fail(3, "shot '" + PendingShots[0] + "': the frame was not rendered (AfterRender not reached)");
	if (DumpTicks > 0 && Tick % DumpTicks == 0)
		WriteDump("tick", {});
	WriteTimeline();
	Flush();
	if (QuitPending)
	{
		Note("quit at tick " + std::to_string(Tick));
		Flush();
		SDL_Event quit{};
		quit.type = SDL_QUIT; // winmain sets bQuit (private) when it handles this
		SDL_PushEvent(&quit);
	}
	else if (ScriptLoaded && !HasQuit && Tick > LastFrame + 1000)
		Fail(4, "script ended without quit");
	Tick++;
}

void ft_test_hook::SoundEvent(int id, TPinballComponent* source, const char* info)
{
	if (!Enabled)
		return;
	fprintf(SoundsF, "{%s, \"kind\": \"sound\", \"id\": %d, \"source\": %s, \"info\": %s}\n", Stamp().c_str(), id,
	        source && source->GroupName ? Json(source->GroupName).c_str() : "null",
	        info ? Json(info).c_str() : "null");
}

void ft_test_hook::TrackEvent(int track, bool replay)
{
	if (!Enabled)
		return;
	fprintf(SoundsF, "{%s, \"kind\": \"track\", \"track\": %d, \"replay\": %s}\n", Stamp().c_str(), track,
	        replay ? "true" : "false");
}

void ft_test_hook::TextLog(TTextBox* box, int op, const char* text, float time, int font, bool lowPriority)
{
	if (!Enabled)
		return;
	std::string ids = "[";
	for (size_t i = 0; i < TextIds.size(); i++)
		ids += (i ? ", " : "") + std::to_string(TextIds[i]);
	ids += "]";
	TextIds.clear();
	fprintf(TextF,
	        "{%s, \"box\": %s, \"op\": \"%s\", \"text\": %s, \"time\": %s, \"font\": %d, \"low\": %s, \"code\": %s, \"ids\": %s}\n",
	        Stamp().c_str(), box && box->GroupName ? Json(box->GroupName).c_str() : "null",
	        op ? "clear" : "display", text ? Json(text).c_str() : "null", Num(time).c_str(), font,
	        lowPriority ? "true" : "false", TextCodeNow >= 0 ? std::to_string(TextCodeNow).c_str() : "null",
	        ids.c_str());
}

void ft_test_hook::TextCode(int ftCode) { TextCodeNow = ftCode; }

void ft_test_hook::TextFetched(int id)
{
	if (Enabled)
		TextIds.push_back(id);
}

void ft_test_hook::Shutdown()
{
	if (!Enabled)
		return;
	Flush();
	FILE** files[] = {&DumpsF, &TimelineF, &SoundsF, &TextF, &LogF};
	for (auto f : files)
		if (*f)
		{
			fclose(*f);
			*f = nullptr;
		}
	if (FifoFd >= 0)
		close(FifoFd);
	Enabled = false;
}
#endif
