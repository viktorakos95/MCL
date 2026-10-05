// Headless pitch-knob probe for the Gearmulator MD/MM emulator (joelanders/gearmulator-md-mm).
// Build it as a target next to mdAudioFirmwareTest (see run_emulator_probe.sh), then:
//   md_pitch_probe <firmware.bin> <outdir> <id,id,...|all-melodic> [note] [decay]
// For every machine id it writes <outdir>/m<id>.f32 : mono float32 @44100, using the exact
// slot layout of measure_tunings.py (t0 = 0.5 s, 128 slots of 0.45 s, key held 0.30 s).
#include "mdLib/mddevice.h"
#include "mdLib/mdromloader.h"
#include "mdLib/mdtypes.h"

#include "baseLib/filesystem.h"
#include "synthLib/plugin.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	constexpr double SR = 44100.0;
	constexpr double T0 = 0.5, SLOT = 0.45, HOLD = 0.30;
	constexpr size_t BLOCK = 64;

	struct Ev { uint64_t at; int kind; std::vector<uint8_t> data; }; // kind 0 sysex, 1 raw 3-byte

	uint64_t S(double t) { return static_cast<uint64_t>(t * SR); }
}

int main(int argc, char** argv)
{
	if(argc < 4) { std::cerr << "usage: md_pitch_probe fw.bin outdir ids [note] [decay]\n"; return 2; }
	const std::string fw = argv[1], outDir = argv[2];
	const int note = argc > 4 ? std::atoi(argv[4]) : 36;
	const int decay = argc > 5 ? std::atoi(argv[5]) : 100;
	const bool reverse = argc > 6 && std::string(argv[6]) == "rev"; // sweep 127 -> 0 (debug)
	std::vector<int> ids;
	{
		std::stringstream ss(argv[3]); std::string t;
		while(std::getline(ss, t, ',')) ids.push_back(std::atoi(t.c_str()));
	}

	std::vector<uint8_t> firmware;
	if(!baseLib::filesystem::readFile(firmware, fw)) { std::cerr << "cannot read firmware\n"; return 2; }
	if(!md::RomLoader::isRomForModel(firmware, md::MachineModel::Machinedrum))
	{ std::cerr << "firmware not accepted by RomLoader (is the custom-OS patch applied?)\n"; return 2; }

	synthLib::DeviceCreateParams params;
	params.romData = std::move(firmware);
	params.romName = fw;
	params.customData = md::deviceCustomData(md::MachineModel::Machinedrum);
	auto device = std::make_unique<md::Device>(params);
	if(!device->isValid()) { std::cerr << "device invalid\n"; return 2; }
	synthLib::Plugin plugin(device.get(), [](synthLib::Device*) {});
	plugin.reserveMidiEventCapacity();
	plugin.setHostSamplerate(static_cast<float>(SR), 44100.0f);
	plugin.setBlockSize(BLOCK);

	std::array<float, BLOCK> inL{}, inR{};
	std::array<std::array<float, BLOCK>, 6> out{};
	const synthLib::TAudioInputs inputs{inL.data(), inR.data(), nullptr, nullptr};
	synthLib::TAudioOutputs outputs{};
	for(size_t c = 0; c < out.size(); ++c) outputs[c] = out[c].data();
	std::vector<synthLib::SMidiEvent> midiOut;

	// boot: process until the firmware accepts MIDI
	uint64_t booted = 0;
	for(; booted < S(60.0); booted += BLOCK)
	{
		plugin.process(inputs, outputs, BLOCK, 120.0f, 0.0f, true);
		if(device->getHardware().isFirmwareMidiReady()) break;
	}
	std::cerr << "booted after " << booted / SR << " s of audio, midiReady="
		<< device->getHardware().isFirmwareMidiReady() << "\n";
	for(int i = 0; i < 400; ++i) plugin.process(inputs, outputs, BLOCK, 120.0f, 0.0f, true);

	if(std::getenv("PROBE_LCD")) // debug: print the emulated LCD after boot, then exit
	{
		const int secs = std::atoi(std::getenv("PROBE_LCD"));
		for(uint64_t pos = 0; pos < S(secs); pos += BLOCK)
			plugin.process(inputs, outputs, BLOCK, 120.0f, 0.0f, true);
		for(int i = 0; i < 12; ++i) // debug: sample the emulated CPU's program counter
		{
			for(int b2 = 0; b2 < 200; ++b2) plugin.process(inputs, outputs, BLOCK, 120.0f, 0.0f, true);
			std::cerr << "PC=" << std::hex << device->getHardware().getUC().getPC() << std::dec << "\n";
		}
		std::cerr << "audioReady=" << device->getHardware().isAudioReady() << " midiReady=" << device->getHardware().isFirmwareMidiReady() << "\n";
		const auto panel = device->getHardware().getFrontPanelSnapshot();
		for(uint32_t y = 0; y < 64; y += 2)
		{
			for(uint32_t x = 0; x < 128; ++x)
				std::cout << ((panel.getLcdPixel(x, y) || panel.getLcdPixel(x, y + 1)) ? '#' : ' ');
			std::cout << '\n';
		}
		return 0;
	}

	const int maxWarm = std::getenv("PROBE_WARMUP_S") ? std::atoi(std::getenv("PROBE_WARMUP_S")) * 4 : 240;
	// warm-up: the emulated OS stays silent for a while after boot. Trigger track 1 until sound
	// comes out so the sweep never starts in the silent phase.
	{
		uint64_t pos = 0;
		bool heard = false;
		for(int attempt = 0; attempt < maxWarm && !heard; ++attempt)
		{
			plugin.addMidiEvent({synthLib::MidiEventSource::Host, 0x90, static_cast<uint8_t>(note), 127, 0});
			float energy = 0;
			for(int b = 0; b < static_cast<int>(S(0.25) / BLOCK); ++b)
			{
				plugin.process(inputs, outputs, BLOCK, 120.0f, static_cast<float>(pos / SR), true);
				plugin.getMidiOut(midiOut);
				for(size_t i = 0; i < BLOCK; ++i) energy += std::abs(out[0][i]) + std::abs(out[1][i]);
				pos += BLOCK;
			}
			plugin.addMidiEvent({synthLib::MidiEventSource::Host, 0x80, static_cast<uint8_t>(note), 0, 0});
			plugin.process(inputs, outputs, BLOCK, 120.0f, static_cast<float>(pos / SR), true);
			heard = energy > 1.0f;
			if(heard) std::cerr << "first sound after " << (attempt * 0.25) << " s of warm-up\n";
		}
		for(int b = 0; b < static_cast<int>(S(1.0) / BLOCK); ++b)
			plugin.process(inputs, outputs, BLOCK, 120.0f, 0.0f, true);
		if(!heard) std::cerr << "warning: no sound during warm-up\n";
	}

	for(const int id : ids)
	{
		std::vector<Ev> ev;
		ev.push_back({0, 0, {0xF0, 0x00, 0x20, 0x3C, 0x02, 0x00, 0x5B, 0x00,
			static_cast<uint8_t>(id & 0x7F), static_cast<uint8_t>(id >= 128 ? 1 : 0), 0x00, 0xF7}});
		ev.push_back({S(0.2), 1, {0xB0, 16 + 23, 127}});
		ev.push_back({S(0.2), 1, {0xB0, 16 + 1, static_cast<uint8_t>(decay)}});
		for(int v = 0; v < 128; ++v)
		{
			const double ts = T0 + v * SLOT;
			ev.push_back({S(ts), 1, {0xB0, 16, static_cast<uint8_t>(reverse ? 127 - v : v)}});
			ev.push_back({S(ts + 0.005), 1, {0x90, static_cast<uint8_t>(note), 127}});
			ev.push_back({S(ts + HOLD), 1, {0x80, static_cast<uint8_t>(note), 0}});
		}
		std::sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.at < b.at; });
		const uint64_t total = S(T0 + 128 * SLOT + 0.5);
		std::vector<float> mono(total, 0.0f);
		size_t next = 0;
		const auto t1 = std::chrono::steady_clock::now();
		for(uint64_t pos = 0; pos < total; pos += BLOCK)
		{
			const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(BLOCK, total - pos));
			while(next < ev.size() && ev[next].at < pos + n)
			{
				const auto off = static_cast<uint32_t>(ev[next].at > pos ? ev[next].at - pos : 0);
				if(ev[next].kind == 0)
				{
					synthLib::SMidiEvent e(synthLib::MidiEventSource::Host);
					e.assignRawData(ev[next].data.data(), ev[next].data.size(), synthLib::MidiEventSource::Host, off);
					plugin.addMidiEvent(e);
				}
				else
				{
					plugin.addMidiEvent({synthLib::MidiEventSource::Host, ev[next].data[0],
						ev[next].data[1], ev[next].data[2], off});
				}
				++next;
			}
			plugin.process(inputs, outputs, n, 120.0f, static_cast<float>(pos / SR), true);
			plugin.getMidiOut(midiOut);
			for(uint32_t i = 0; i < n; ++i) mono[pos + i] = out[0][i] + out[1][i];
		}
		const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
		float peak = 0; for(float f : mono) peak = std::max(peak, std::abs(f));
		std::ofstream f(outDir + "/m" + std::to_string(id) + ".f32", std::ios::binary);
		f.write(reinterpret_cast<const char*>(mono.data()), static_cast<std::streamsize>(mono.size() * sizeof(float)));
		std::printf("id %d: %.1f s audio in %.1f s (x%.2f realtime), peak %.3f\n", id, total / SR, secs, (total / SR) / secs, peak);
		std::fflush(stdout);
	}
	return 0;
}
