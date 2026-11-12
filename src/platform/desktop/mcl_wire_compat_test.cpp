#include <chrono>
#include <atomic>
#include <thread>
#include "Drivers/MD/MD.h"
#include "Drivers/MD/MDSysex.h"
#include "Drivers/MD/UI/MDTrackSelect.h"
#include "GUI/Pages/Sequencer/SeqPage.h"
#include "GUI/Pages/Sequencer/SeqPages.h"
#include "Drivers/MD/GridTracks/MDFXTrack.h"
#include "Drivers/MD/GridTracks/SPSXTrack.h"
#include "ElektronDataEncoder.h"
#include "Host/SpsHostSeqBridge.h"
#include "Host/SpsSeqProtocol.h"
#include "Midi.h"
#include "MidiSysex.h"
#include "MidiUart.h"
#include "Project.h"
#include "Sequencer/MCLSeq.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {

constexpr uint16_t kTestSysexCapacity = 60000;

struct SysexFixture {
    std::array<uint8_t, kTestSysexCapacity> storage{};
    RingBuffer<> ring;
    MidiUartClass uart;
    MidiSysexClass sysex;
    MidiClass midi;

    SysexFixture()
        : ring(storage.data(), (uint16_t)storage.size()),
          sysex(&uart, &ring), midi(&uart, &sysex) {}
};

bool publishToMidiSysex(MidiSysexClass& sysex, const uint8_t* message,
                        uint16_t length) {
    if (!message || length < 2 || message[0] != 0xF0 ||
        message[length - 1] != 0xF7 ||
        length - 2 >= sysex.rb->len) {
        return false;
    }
    const uint8_t record = sysex.msg_wr;
    sysex.startRecord();
    for (uint16_t i = 1; i + 1 < length; ++i)
        sysex.recordByte(message[i]);
    sysex.stopRecord();
    sysex.rd_cur = record;
    return true;
}

bool checkKitWireCapability(uint32_t caps, uint8_t expectedVersion,
                            uint16_t expectedSize) {
    std::array<uint8_t, 2048> message{};
    MD.is_spsx = true;
    MD.fw_caps = FW_CAP_SPSX | caps;
    for (unsigned fx = 0; fx < 3; ++fx)
        for (unsigned p = 0; p < 8; ++p) MD.kit.userBusFx[fx][p] = 11 + fx * 8 + p;
    for (unsigned p = 0; p < 8; ++p) MD.kit.userPostFx[p] = 91 + p;
    for (uint8_t track = 0; track < 16; ++track) {
        constexpr uint32_t modes[]{0,0x20000,0x60000,0xa0000};
        MD.kit.models[track] = modes[track % 4] | (track & 1 ? 0xc2u : 0x10u);
        MD.kit.params[track][MODEL_ENVDEST] = track % 3;
        MD.kit.outputRouting[track] = mdKitRouteFromSelection(track % 8);
    }
    ElektronDataToSysexEncoder encoder(message.data());
    const uint16_t length = MD.kit.toSysex(&encoder);
    if (expectedVersion == SPSX_KIT_VERSION &&
        (std::memcmp(message.data() + 0x463, MD.kit.outputRouting, 16) ||
         std::memcmp(message.data() + 0x473, MD.kit.userBusFx, 24) ||
         std::memcmp(message.data() + 0x48b, MD.kit.userPostFx, 8))) return false;
    auto fixture = std::make_unique<SysexFixture>();
    if (length != expectedSize || message[7] != expectedVersion ||
        !publishToMidiSysex(fixture->sysex, message.data(), length)) {
        return false;
    }
    MDKit decoded;
    if (!decoded.fromSysex(&fixture->midi)) return false;
    for (uint8_t track = 0; track < 16; ++track) {
        if (decoded.models[track] != MD.kit.models[track] ||
            decoded.get_tuning(track) != track % 4 ||
            decoded.params[track][MODEL_ENVDEST] != track % 3 ||
            decoded.outputRouting[track] != MD.kit.outputRouting[track])
            return false;
    }
    std::array<uint8_t, 2048> roundtrip{};
    ElektronDataToSysexEncoder again(roundtrip.data());
    if (decoded.toSysex(&again) != length ||
        std::memcmp(message.data(), roundtrip.data(), length) != 0)
        return false;
    for (uint8_t param = MODEL_ENVATT; param < SPS_PARAMS_PER_TRACK; ++param) {
        const char* label = model_param_name(0, param);
        if (!label || std::strlen(label) != 3) return false;
    }
    return true;
}

bool checkGlobalVersions() {
    static_assert(SPSX_GLOBAL_VERSION == 0x40);
    std::array<uint8_t, 512> message{}, roundtrip{};
    static MDGlobal source;
    source.seqTempoMode = 1;
    source.channelMode = 1;
    for (bool native : {false, true}) {
        MD.is_spsx = native;
        ElektronDataToSysexEncoder encoder(message.data());
        const uint16_t length = source.toSysex(&encoder);
        if (message[7] != (native ? SPSX_GLOBAL_VERSION : MDX_GLOBAL_VERSION)) return false;
        auto fixture = std::make_unique<SysexFixture>();
        if (!publishToMidiSysex(fixture->sysex, message.data(), length)) return false;
        MDGlobal decoded;
        if (!decoded.fromSysex(&fixture->midi) ||
            decoded.seqTempoMode != unsigned(native) ||
            decoded.channelMode != unsigned(native)) return false;
        ElektronDataToSysexEncoder again(roundtrip.data());
        if (decoded.toSysex(&again) != length ||
            std::memcmp(message.data(), roundtrip.data(), length)) return false;
        for (unsigned version = 0; version < 128; ++version) {
            if ((version >= 1 && version <= 6) || version == 0x40) continue;
            message[7] = static_cast<uint8_t>(version);
            if (!publishToMidiSysex(fixture->sysex, message.data(), length) ||
                decoded.fromSysex(&fixture->midi)) return false;
        }
    }
    return true;
}

bool checkMachineRoutePackets() {
    MidiUartClass output;
    auto* savedUart = MD.uart;
    const bool wasConnected = MD.connected;
    MD.uart = &output;
    MD.connected = true;
    MD.is_spsx = true;
    bool ok = true;
    SPSMachine machine{};
    machine.init();
    machine.params[MODEL_ENVDEST] = 2;
    machine.outputRouting = 6;
    for (uint8_t level : {uint8_t(255), uint8_t(59)}) {
        output.init();
        MD.assignMachineBulk(3, &machine, level, 255, true);
        std::array<uint8_t, 128> bytes{};
        const size_t size = output.desktop_egress(bytes.data(), bytes.size());
        ok &= size == size_t(64 + (level != 255)) && bytes[0] == 0xf0 &&
              bytes[size - 1] == 0xf7 && (bytes[11] & 4) &&
              bytes[12 + MODEL_ENVDEST] == 2 && bytes[size - 2] == 6;
        if (level != 255) ok &= bytes[size - 3] == level;
    }
    MD.save_kit_params();
    const uint8_t route = MD.kit.outputRouting[3];
    MD.kit.outputRouting[3] = 0;
    MD.restore_kit_params();
    ok &= MD.kit.outputRouting[3] == route;
    MD.uart = savedUart;
    MD.connected = wasConnected;
    return ok;
}

bool checkTonalOctaveMessages() {
    constexpr uint32_t modelFlags[]{0,0x20000,0x60000,0xa0000};
    constexpr uint8_t wireFlags[]{0,2,10,18};
    const auto oldUart=MD.uart;
    const bool oldConnected=MD.connected;
    const bool oldSpsx=MD.is_spsx;
    const auto oldCaps=MD.fw_caps;
    MidiUartClass output;
    MD.uart=&output;MD.connected=true;
    bool ok=true;
    auto fixture=std::make_unique<SysexFixture>();
    MDSysexListenerClass listener;listener.setup(&fixture->midi);
    MDTrackSelect select;select.setup(&fixture->midi);
    for(unsigned track=0;track<16;++track)for(unsigned mode=0;mode<4;++mode)
    for(unsigned high=0;high<2;++high) {
        const uint8_t model=uint8_t(16+128*high);
        const uint32_t raw=model|modelFlags[mode];
        const uint8_t flags=wireFlags[mode]|high;
        MDMachine legacy{};legacy.track=track;legacy.init();legacy.model=raw;
        SPSMachine native{};native.init();native.model=raw;native.outputRouting=6;
        ok &= legacy.get_model()==model && legacy.get_tuning()==mode &&
              legacy.get_tonal()==bool(mode) && legacy.get_model_raw()==raw &&
              native.get_tuning()==mode && native.get_model_raw()==raw;
        // Existing raw machine/grid storage carries all flags without growing.
        std::array<uint8_t,sizeof(MDMachine)> stored{};
        std::memcpy(stored.data(),&legacy,stored.size());
        MDMachine restored{};std::memcpy(&restored,stored.data(),stored.size());
        ok &= restored.get_model_raw()==raw;
        for(bool spsx : {false,true}) {
            MD.is_spsx=spsx;MD.fw_caps=spsx?FW_CAP_SPSX:0;
            std::array<uint8_t,128> packet{};
            for(uint8_t init : {uint8_t(0),uint8_t(1),uint8_t(2),uint8_t(255)}) {
                output.init();MD.assignMachine(track,model,init,mode);
                const size_t n=output.desktop_egress(packet.data(),packet.size());
                ok &= n==unsigned(init==255?11:12) && packet[6]==0x5b &&
                      packet[7]==track && packet[8]==16 && packet[9]==(spsx?flags:high);
                if(init!=255)ok &= packet[10]==init;
            }
            output.init();MD.setMachine(track,&legacy);
            const size_t setSize=output.desktop_egress(packet.data(),packet.size());
            ok &= setSize>=12 && packet[6]==0x5b && packet[9]==(spsx?flags:high);
            for(unsigned cached=0;cached<2;++cached)for(uint8_t count : {uint8_t(0),uint8_t(1),uint8_t(255)}) {
                output.init();MD.assignMachineBulk(track+16*cached,&legacy,59,count,true);
                const size_t n=output.desktop_egress(packet.data(),packet.size());
                const uint8_t expected=spsx?flags:uint8_t(high|(mode?2:0));
                // The stored machine record carries all 38 parameters, LFO-B
                // and the route, so an X.20 peer gets the wide message too.
                const bool wide=spsx && count==255;
                ok &= n==unsigned(count==0?13:count==1?(spsx?51:37):(spsx?65:45)) && packet[6]==0x70 &&
                      packet[7]==0x5b && packet[9]==track+16*cached && packet[10]==16 &&
                      packet[11]==uint8_t(expected|(wide?4:0));
                output.init();MD.assignMachineBulk(track+16*cached,&native,59,count,true);
                const size_t nativeSize=output.desktop_egress(packet.data(),packet.size());
                const bool routed=spsx && count==255;
                ok &= nativeSize==unsigned(count==0?13:count==1?(spsx?51:37):(spsx?65:45)) &&
                      packet[11]==uint8_t(expected|(routed?4:0));
                if(routed)ok &= packet[nativeSize-2]==6;
            }
        }
        // These are the actual receiver entry points, using literal wire flags.
        const uint8_t update[]{0xf0,0,0x20,0x3c,2,0,0x63,uint8_t(track),16,flags,0,0xf7};
        ok &= publishToMidiSysex(fixture->sysex,update,sizeof(update));
        listener.end();ok &= MD.kit.models[track]==raw;
        const uint8_t trackState[]{0xf0,0x7f,0x0e,0,uint8_t(track),flags,16,0xf7};
        ok &= publishToMidiSysex(fixture->sysex,trackState,sizeof(trackState));
        // Keep the unrelated UI refresh out of this protocol-only test.
        last_primary_track=track;SeqPage::last_md_model=raw;
        select.handle_track_select_legacy(SysexView(&fixture->sysex),6);
        ok &= MD.currentTrack==track && MD.kit.models[track]==raw;
    }
    // Reserved octave combinations normalize like the firmware; route is independent.
    for(unsigned flags=0;flags<128;++flags) {
        const unsigned mode=(flags&2)==0?0:(flags&24)==8?2:(flags&24)==16?3:1;
        ok &= mdModelFromMachineFlags(16,flags)==((16u|((flags&1u)<<7u))|modelFlags[mode]);
    }
    MD.uart=oldUart;MD.connected=oldConnected;MD.is_spsx=oldSpsx;MD.fw_caps=oldCaps;
    return ok;
}

bool checkUsrMetadata() {
    const auto oldUart = MD.uart;
    const auto oldCaps = MD.fw_caps;
    const bool oldConnected = MD.connected;
    MidiUartClass output;
    MD.uart = &output;
    MD.connected = true;
    MD.fw_caps = FW_CAP_USR_QUERY | FW_CAP_SPSX;
    md_usr.reset();
    if (!md_usr.takeChanged() || md_usr.takeChanged()) return false;
    auto fixture = std::make_unique<SysexFixture>();
    std::array<uint8_t, 160> request{}, reply{};
    uint8_t lastTag = 0;
    auto poll = [&](unsigned page, uint32_t generation, uint8_t count = 48) {
        output.init();
        md_usr.service();
        const size_t n = output.desktop_egress(request.data(), request.size());
        if (n != 13 || request[7] != 0x44 || request[10] != page || request[9] == 127) return false;
        lastTag = request[9];
        const uint8_t header[]{0xf0,0,0x20,0x3c,2,0,0x72,0x44,1,lastTag,uint8_t(page),count};
        reply.fill(0);
        std::memcpy(reply.data(), header, sizeof(header));
        for (unsigned b = 0; b < 5; ++b) reply[12+b] = (generation >> (b*7)) & 127;
        for (unsigned slot = 0; slot < 16; ++slot) {
            reply[17+slot*6] = uint8_t(32|16|8|3); // primary pitch is parameter 3
            std::memcpy(reply.data()+18+slot*6, "TXZLB", 5);
        }
        reply[113] = 0xf7;
        return true;
    };
    auto deliver = [&](unsigned n = 114) {
        if (!publishToMidiSysex(fixture->sysex, reply.data(), n)) return false;
        md_usr.receive(SysexView(&fixture->sysex));
        return true;
    };
    bool ok = poll(0, 1);
    reply[9] ^= 1; // stale request tag cannot publish
    ok &= deliver() && md_usr.descriptor(194) == 0;
    reply[9] ^= 1;
    reply[17] |= 64; // reserved descriptor flag
    ok &= deliver() && md_usr.descriptor(194) == 0;
    reply[17] &= ~64;
    ok &= deliver() && md_usr.descriptor(194) == 59;
    ok &= std::strcmp(md_usr.name(194), "TXZ-LB") == 0;
    ok &= poll(1, 1) && deliver() && poll(2, 1) && deliver();
    ok &= md_usr.descriptor(241) == 59 && md_usr.descriptor(242) == 0;
    // A complete cache emits no status polling, even after the old interval.
    output.init();
    std::this_thread::sleep_for(std::chrono::milliseconds(1010));
    md_usr.service();
    ok &= output.desktop_egress(request.data(), request.size()) == 0;
    // With FW_CAP_USR_LABELS the kit's USR machines fetch their labels.
    {
        uint32_t oldModels[16];
        std::memcpy(oldModels, MD.kit.models, sizeof(oldModels));
        for (auto &model : MD.kit.models) model = 0;
        MD.kit.models[5] = 196; // slot 2
        MD.fw_caps |= FW_CAP_USR_LABELS;
        ok &= md_usr.label(196, 0) == nullptr;
        ok &= poll(5, 1) && request[11] == 2;
        static const char labels[] = "FREQDEC\0P3\0\0P4\0\0P5\0\0P6\0\0P7\0\0LAST";
        reply[17] = 3; // another slot's reply cannot publish
        std::memcpy(reply.data() + 18, labels, 32);
        reply[50] = 0xf7;
        ok &= deliver(51) && md_usr.label(196, 0) == nullptr;
        reply[17] = 2;
        ok &= deliver(51) && std::strcmp(md_usr.label(196, 0), "FRE") == 0;
        ok &= std::strcmp(md_usr.label(196, 1), "DEC") == 0;
        ok &= std::strcmp(md_usr.label(196, 7), "LAS") == 0;
        ok &= md_usr.label(197, 0) == nullptr && md_usr.label(196, 8) == nullptr;
        output.init(); md_usr.service(); // complete: nothing more to ask
        ok &= output.desktop_egress(request.data(), request.size()) == 0;
        md_usr.invalidate();
        ok &= md_usr.label(196, 0) == nullptr;
        ok &= poll(0, 1) && deliver() && poll(1, 1) && deliver() &&
              poll(2, 1) && deliver() && poll(5, 1) && request[11] == 2;
        reply[17] = 2; std::memcpy(reply.data() + 18, labels, 32); reply[50] = 0xf7;
        ok &= deliver(51) && std::strcmp(md_usr.label(196, 0), "FRE") == 0;
        MD.fw_caps = FW_CAP_USR_QUERY | FW_CAP_SPSX;
        std::memcpy(MD.kit.models, oldModels, sizeof(oldModels));
    }
    MD.kit.models[0] = 194 | mdTuningModelFlags(MD_TUNING_TONAL);
    ok &= MD.pitchParameter(0) == 3 && MD.getKitModelTuning(0) != nullptr;
    char label[8]{}; uint8_t pitchMax = 0; bool midiModel = true;
    const auto context = DeviceContext::for_device(&MD, DeviceIdx::Primary);
    ok &= MD.step_edit()->configure_kit_sound_panel(context, 0, label, sizeof(label), &pitchMax, &midiModel);
    ok &= std::strcmp(label, "TXZ-LB") == 0 && !midiModel && pitchMax == 64;
    ok &= MD.step_edit()->kit_sound_pitch_from_note(context, 0, pitchMax, 32) == 126;
    mcl_seq.md_tracks[0].clear_locks();
    mcl_seq.md_tracks[0].track_number = 0;
    mcl_seq.md_tracks[0].set_track_pitch(2, 64);
    ok &= mcl_seq.md_tracks[0].get_track_lock_implicit(2, 3) == 64;
    ok &= mcl_seq.md_tracks[0].find_param(0) == 255;
    for (uint8_t mode = 1; mode <= 3; ++mode) {
        MD.kit.models[0] = 194 | mdTuningModelFlags(mode);
        const uint8_t note = 69 + mdTuningNoteOffset(mode);
        ok &= MD.trackGetCCPitch(0, 64, nullptr) == note && MD.trackGetPitch(0, note) == 64;
    }
    auto& nativeTrack = mcl_seq.spsx_tracks[0];
    nativeTrack.track_number = 0;
    nativeTrack.clear_locks();
    nativeTrack.set_track_pitch(2, 64);
    nativeTrack.set_track_pitch(3, 126);
    nativeTrack.set_track_pitch(4, 127);
    MD.kit.params[0][0] = 99;
    MD.kit.params[0][3] = 64;
    nativeTrack.transpose(1);
    ok &= nativeTrack.get_track_lock_implicit(2, 3) == 66 && nativeTrack.find_param(0) == 255;
    ok &= nativeTrack.get_track_lock_implicit(3, 3) == 126 && nativeTrack.get_track_lock_implicit(4, 3) == 127;
    ok &= MD.kit.params[0][3] == 66 && MD.kit.params[0][0] == 99;
    nativeTrack.clear_locks();
    // A new bank/replacement generation invalidates every previously cached page.
    md_usr.invalidate();
    ok &= poll(0, 2) && deliver() && md_usr.descriptor(241) == 0;
    // Empty and non-tonal machines never acquire melodic behavior.
    md_usr.invalidate();
    ok &= poll(0, 2);
    std::memset(reply.data()+17, 0, 6);
    reply[23] = 32;
    ok &= deliver() && md_usr.name(194) == nullptr;
    ok &= MD.pitchParameter(0) == 255 && MD.getModelTuning(195, true) == nullptr;
    // A truncated message cannot partly overwrite a published cache.
    md_usr.invalidate();
    ok &= poll(0, 2);
    reply[112] = 0xf7;
    ok &= deliver(113) && md_usr.descriptor(194) == 0;
    reply[112] = 'B'; reply[113] = 0xf7;
    ok &= deliver();
    // Exercise the actual blocking supplement through the MIDI listener.
    // The existing legacy kit storage gains octave flags without extra rows.
    const auto oldMidi = MD.midi;
    MD.midi = &Midi; MD.uart = &MidiUart;
    MDSysexListener.setup(&Midi);
    MidiUart.init();
    lastTag = (lastTag + 1) & 127;
    const uint8_t kitHeader[]{0xf0,0,0x20,0x3c,2,0,0x72,0x44,1,lastTag,3,48,2,0,0,0,0};
    std::memcpy(reply.data(), kitHeader, sizeof(kitHeader));
    for (uint8_t track = 0; track < 16; ++track) {
        MD.kit.models[track] = 194 + track;
        reply[17 + track*2] = 66 + track;
        reply[18 + track*2] = mdMachineTuningFlags(MD.kit.models[track] | mdTuningModelFlags(track%4), true);
    }
    reply[49] = 0xf7;
    ok &= MidiUart.desktop_ingress(reply.data(), 50) == 50;
    ok &= md_usr.readKitTuning(127, 100);
    for (uint8_t track = 0; track < 16; ++track) ok &= MD.kit.get_tuning(track) == track%4;
    // Public notice needs no outstanding query; malformed notices do nothing.
    const uint8_t notice[]{0xf0,0,0x20,0x3c,2,0,0x72,0x44,1,127,4,48,3,0,0,0,0,0xf7};
    std::memcpy(reply.data(), notice, sizeof(notice));
    const auto descriptorBefore = md_usr.descriptor(194);
    reply[10] = 0;
    ok &= deliver(18) && md_usr.descriptor(194) == descriptorBefore;
    reply[10] = 4;
    MidiUart.init();
    ok &= deliver(18) && md_usr.descriptor(194) == 0;
    ok &= MidiUart.desktop_egress(request.data(), request.size()) == 0; // Callback only schedules.
    // A library-only event fetches compact metadata, without a full kit dump.
    MD.uart = &output;
    ok &= poll(0, 3) && deliver();
    // Tag wrap never collides with unsolicited $7f, and an old reply cannot
    // complete a request invalidated by a library/kit notification.
    md_usr.reset(); MD.uart = &output;
    for (unsigned i = 0; i < 130; ++i) {
        md_usr.invalidate();
        ok &= poll(0, 4) && deliver();
    }
    md_usr.invalidate(); ok &= poll(0, 4);
    md_usr.kitChanged();
    ok &= deliver() && md_usr.descriptor(194) == 0;
    // MDX announces a kit dump it has loaded (0x54) before it answers the
    // capability query MCL sends straight after the kit. Seen in MAME at
    // dd02815d3: notice 12 ms before the reply. Neither may empty fw_caps.
    MD.uart = &MidiUart;
    MD.fw_caps = FW_CAP_USR_QUERY | FW_CAP_SPSX;
    MidiUart.init();
    const uint8_t noticeThenCaps[]{0xf0,0,0x20,0x3c,2,0,0x54,0x7f,0xf7,
                                   0xf0,0,0x20,0x3c,2,0,0x72,0x30,0x7e,0x7f,0x7b,0x01,0xf7};
    ok &= MidiUart.desktop_ingress(noticeThenCaps, sizeof(noticeThenCaps)) == sizeof(noticeThenCaps);
    ok &= MD.get_fw_caps() && MD.fw_caps == 0x017b7f7eUL && MD.currentKit == 0x7f;
    MD.fw_caps = FW_CAP_USR_QUERY | FW_CAP_SPSX;
    MidiUart.init();
    ok &= MidiUart.desktop_ingress(noticeThenCaps, 9) == 9; // the notice alone
    {
        // Nothing advances the millisecond clock in this binary; tick it so
        // the query can time out.
        std::atomic<bool> ticking{true};
        std::thread clock([&ticking] {
            while (ticking) {
                g_clock_ms = uint16_t(g_clock_ms + 1);
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
        });
        ok &= !MD.get_fw_caps() && MD.fw_caps == (FW_CAP_USR_QUERY | FW_CAP_SPSX);
        ticking = false;
        clock.join();
    }
    MD.midi = oldMidi; MDSysexListener.setup(oldMidi);
    mcl_seq.md_tracks[0].clear_locks();
    md_usr.reset();
    MD.uart = oldUart; MD.fw_caps = oldCaps; MD.connected = oldConnected;
    return ok;
}

bool checkTonalLegacyKitExport() {
    const bool wasSpsx=MD.is_spsx;MD.is_spsx=false;
    constexpr uint32_t modes[]{0,0x20000,0x60000,0xa0000};
    for(unsigned track=0;track<16;++track)MD.kit.models[track]=0x10u|modes[track%4];
    std::array<uint8_t,2048> message{};
    ElektronDataToSysexEncoder encoder(message.data());
    const auto length=MD.kit.toSysex(&encoder);
    auto fixture=std::make_unique<SysexFixture>();
    MDKit decoded;
    bool ok=message[7]==MDX_KIT_VERSION && publishToMidiSysex(fixture->sysex,message.data(),length) &&
        decoded.fromSysex(&fixture->midi);
    for(unsigned track=0;track<16;++track) {
        ok &= decoded.models[track]==(0x10u|(track%4?0x20000u:0u));
        ok &= MD.kit.models[track]==(0x10u|modes[track%4]);
    }
    MD.is_spsx=wasSpsx;
    return ok;
}

// An all-zero noise generator seed leaves the Machinedrum's random LFO
// stuck, so a kit never goes out with one: the seed slot of an LFO's run
// state carries 0x029A unless it already holds something.
bool checkLfoSeedMarker() {
    bool ok = true;
    const auto seeded = [](const MDLFO& lfo) {
        return lfo.state[25] == 0x9a && lfo.state[26] == 0x02;
    };

    // A freshly initialised record: the marker and nothing else.
    MDLFO fresh;
    fresh.init(5);
    ok &= fresh.destinationTrack == 5 && seeded(fresh);
    for (uint8_t i = 0; i < sizeof(fresh.state); ++i) {
        if (i != 25 && i != 26) ok &= fresh.state[i] == 0;
    }

    // A seed that is already there is left alone.
    MDLFO running;
    running.init(0);
    std::memset(running.state, 0, sizeof(running.state));
    running.state[23] = 0x11;
    running.ensure_lfsr_magic();
    ok &= running.state[23] == 0x11 && running.state[25] == 0 &&
          running.state[26] == 0;

    // An old-format kit whose LFO states are all zero goes out seeded.
    const bool wasSpsx = MD.is_spsx;
    MD.is_spsx = false;
    for (unsigned track = 0; track < 16; ++track) {
        MD.kit.models[track] = 0x10u;
        std::memset(MD.kit.lfos[track].state, 0, sizeof(MD.kit.lfos[track].state));
    }
    std::array<uint8_t, 2048> message{};
    ElektronDataToSysexEncoder encoder(message.data());
    const auto length = MD.kit.toSysex(&encoder);
    auto fixture = std::make_unique<SysexFixture>();
    MDKit decoded;
    ok &= length != 0 &&
          publishToMidiSysex(fixture->sysex, message.data(), length) &&
          decoded.fromSysex(&fixture->midi);
    for (unsigned track = 0; ok && track < 16; ++track) {
        ok &= seeded(decoded.lfos[track]);
    }
    MD.is_spsx = wasSpsx;
    return ok;
}

// The Machinedrum's LFO run state comes back as it was received: all 31
// bytes in an old-format kit, and in kit 65.1 the phase of a FREE LFO or the
// two held values of a HOLD LFO.
bool checkLfoRunStateRoundTrip() {
    bool ok = true;
    const bool wasSpsx = MD.is_spsx;
    const auto roundTrip = [&ok](MDKit& decoded) {
        std::array<uint8_t, 2048> message{};
        ElektronDataToSysexEncoder encoder(message.data());
        const auto length = MD.kit.toSysex(&encoder);
        auto fixture = std::make_unique<SysexFixture>();
        ok &= length != 0 &&
              publishToMidiSysex(fixture->sysex, message.data(), length) &&
              decoded.fromSysex(&fixture->midi);
    };

    MD.is_spsx = false;
    for (unsigned track = 0; track < 16; ++track) {
        MD.kit.models[track] = 0x10u;
        MDLFO& lfo = MD.kit.lfos[track];
        lfo.init(track);
        lfo.type = track % 3;
        for (unsigned i = 0; i < sizeof(lfo.state); ++i) {
            lfo.state[i] = uint8_t(1 + track * 7 + i * 3);
        }
    }
    {
        auto decoded = std::make_unique<MDKit>();
        roundTrip(*decoded);
        for (unsigned track = 0; ok && track < 16; ++track) {
            ok &= std::memcmp(&decoded->lfos[track], &MD.kit.lfos[track],
                              5 + 31) == 0;
        }
    }

    MD.is_spsx = true;
    for (unsigned track = 0; track < 16; ++track) {
        for (MDLFO* lfo : {&MD.kit.lfos[track], &MD.kit.lfosB[track]}) {
            lfo->init(track);
            lfo->type = track % 3;
            const int32_t phase = 0x1234 + track;
            const int32_t hold0 = -200 - int32_t(track), hold1 = 300 + track;
            std::memcpy(&lfo->state[27], &phase, sizeof(phase));
            std::memcpy(&lfo->state[11], &hold0, sizeof(hold0));
            std::memcpy(&lfo->state[15], &hold1, sizeof(hold1));
        }
    }
    {
        auto decoded = std::make_unique<MDKit>();
        roundTrip(*decoded);
        for (unsigned track = 0; ok && track < 16; ++track) {
            const MDLFO* got[2] = {&decoded->lfos[track], &decoded->lfosB[track]};
            const MDLFO* sent[2] = {&MD.kit.lfos[track], &MD.kit.lfosB[track]};
            for (unsigned which = 0; which < 2; ++which) {
                const uint8_t mode = sent[which]->type % 3;
                if (mode == 0) {
                    ok &= std::memcmp(&got[which]->state[27],
                                      &sent[which]->state[27], 4) == 0;
                } else if (mode == 2) {
                    ok &= std::memcmp(&got[which]->state[11],
                                      &sent[which]->state[11], 8) == 0;
                }
            }
        }
    }
    MD.is_spsx = wasSpsx;
    return ok;
}

// Cross-codec fixture entry point: accepts a firmware-emitted full kit and
// writes the actual MCL decoder/encoder result for byte comparison.
int roundtripKitFile(const char* input, const char* output) {
    std::array<uint8_t, 2048> message{}, roundtrip{};
    FILE* file = std::fopen(input, "rb");
    if (!file) return 2;
    const size_t length = std::fread(message.data(), 1, message.size(), file);
    std::fclose(file);
    auto fixture = std::make_unique<SysexFixture>();
    MDKit decoded;
    MD.is_spsx = true;
    if (!publishToMidiSysex(fixture->sysex, message.data(), length) ||
        !decoded.fromSysex(&fixture->midi)) return 3;
    ElektronDataToSysexEncoder encoder(roundtrip.data());
    const uint16_t size = decoded.toSysex(&encoder);
    file = std::fopen(output, "wb");
    if (!file) return 4;
    const bool ok = std::fwrite(roundtrip.data(), 1, size, file) == size;
    std::fclose(file);
    return ok ? 0 : 5;
}

bool checkPatternWireCapability(uint32_t caps, uint8_t expectedVersion,
                                bool expectBusLock) {
    std::array<uint8_t, 16384> message{};
    MD.is_spsx = true;
    MD.fw_caps = FW_CAP_SPSX | caps;
    MD.pattern.clearPattern();
    MD.pattern.patternLength = 64;

    // A stock-range lock must survive both the stock fallback and compact
    // format. BUS1-3 exercise the compact-only parameter range.
    if (!MD.pattern.addLock(0, 0, MODEL_REV, 55))
        return false;
    MD.pattern.ext_locks_params[0][MODEL_REV] = MODEL_REV + 1;
    constexpr uint8_t tracks[] = {0, 3, 4, 15};
    constexpr uint8_t params[] = {MODEL_BUS1, MODEL_BUS2, MODEL_BUS3};
    for (uint8_t track : tracks) {
        MD.pattern.trigPatterns[track] =
            (uint64_t(1) << 5) | (uint64_t(1) << 45);
        for (uint8_t param : params) {
            MD.pattern.ext_locks_params[track][param] = param + 1;
            if (!MD.pattern.addLock(track, 5, param,
                                    (uint8_t)((20 + track + param) &
                                              0x7f)) ||
                !MD.pattern.addLock(track, 45, param,
                                    (uint8_t)((80 + track + param) &
                                              0x7f))) {
                return false;
            }
        }
    }

    ElektronDataToSysexEncoder encoder(message.data());
    const uint16_t length = MD.pattern.toSysex(&encoder);
    auto fixture = std::make_unique<SysexFixture>();
    if (length == 0 || length > message.size() ||
        message[7] != expectedVersion ||
        !publishToMidiSysex(fixture->sysex, message.data(), length)) {
        std::fprintf(stderr,
                     "pattern capability envelope: len=%u ver=%02x "
                     "expected=%02x\n",
                     length, message[7], expectedVersion);
        return false;
    }

    MDPattern decoded;
    if (!decoded.fromSysex(&fixture->midi) ||
        decoded.version != expectedVersion) {
        std::fprintf(stderr,
                     "pattern capability decode: len=%u decoded-ver=%02x "
                     "expected=%02x\n",
                     length, decoded.version, expectedVersion);
        return false;
    }
    const int16_t legacyRow = decoded.paramLocks[0][MODEL_REV];
    const bool hasLegacyLock =
        legacyRow >= 0 && decoded.lock_row((uint16_t)legacyRow)[0] == 55;
    const uint8_t decodedParameterCount =
        decoded.version == MD_PATTERN_VERSION_SPSX
            ? MD_PATTERN_LOCK_SLOTS
            : 24;
    if (!hasLegacyLock ||
        decodedParameterCount !=
            (expectBusLock ? MD_PATTERN_LOCK_SLOTS : 24)) {
        std::fprintf(stderr,
                     "pattern capability base lock: row=%d value=%d "
                     "expect-bus=%d\n",
                     legacyRow,
                     legacyRow >= 0
                         ? decoded.lock_row((uint16_t)legacyRow)[0]
                         : -2,
                     expectBusLock);
        return false;
    }
    for (uint8_t track : tracks) {
        for (uint8_t param : params) {
            const int16_t row = decoded.paramLocks[track][param];
            const bool present = row >= 0;
            if (present != expectBusLock) {
                std::fprintf(stderr,
                             "pattern capability bus lock: track=%u "
                             "param=%u row=%d expected=%d\n",
                             track, param, row, expectBusLock);
                return false;
            }
            if (present &&
                (decoded.lock_row((uint16_t)row)[5] !=
                     (int8_t)((20 + track + param) & 0x7f) ||
                 decoded.lock_row((uint16_t)row)[45] !=
                     (int8_t)((80 + track + param) & 0x7f))) {
                std::fprintf(stderr,
                             "pattern capability value: track=%u param=%u "
                             "row=%d got=%d/%d expected=%u/%u\n",
                             track, param, row,
                             decoded.lock_row((uint16_t)row)[5],
                             decoded.lock_row((uint16_t)row)[45],
                             (uint8_t)((20 + track + param) & 0x7f),
                             (uint8_t)((80 + track + param) & 0x7f));
                return false;
            }
        }
    }
    return true;
}

uint8_t stressLockValue(uint8_t track, uint8_t param, uint8_t step) {
    return (uint8_t)((track * 53u + param * 29u + step * 17u +
                      step * step + (param ^ step) * 7u) & 0x7Fu);
}

bool hasValidElektronEnvelope(
    const uint8_t* bytes, uint16_t length) {
    if (bytes == nullptr || length < 15 ||
        bytes[0] != 0xf0 || bytes[length - 1] != 0xf7) {
        return false;
    }
    uint16_t checksum = 0;
    for (uint16_t index = 9; index < length - 5; ++index) {
        if (bytes[index] > 0x7f)
            return false;
        checksum = (checksum + bytes[index]) & 0x3fff;
    }
    const uint16_t storedChecksum =
        ((uint16_t)bytes[length - 5] << 7) |
        bytes[length - 4];
    const uint16_t storedLength =
        ((uint16_t)bytes[length - 3] << 7) |
        bytes[length - 2];
    return checksum == storedChecksum &&
           storedLength == (uint16_t)(length - 10);
}

bool checkCanonicalPatternEnvelope(uint16_t& encodedLength) {
    static_assert(MD_PATTERN_VERSION_SPSX == 0x40);
    std::array<uint8_t, 8192> message{};
    MD.is_spsx = true;
    MD.fw_caps = FW_CAP_SPSX | FW_CAP_SPSX_PARAMS_37;
    MD.pattern.clearPattern();
    MD.pattern.origPosition = 37;
    MD.pattern.patternLength = 64;
    MD.pattern.scale = 2;
    MD.pattern.doubleTempo = 1;
    MD.pattern.kit = 9;
    MD.pattern.patternTempo = 0x1234;
    MD.pattern.accentAmount = (13u >> 1) | (13u << 3);
    MD.pattern.swingAmount = 7u * 16384u / 30u;
    MD.pattern.accentPattern = UINT64_C(0x0000000000000004);
    MD.pattern.slidePattern = UINT64_C(0x0000000000000008);
    MD.pattern.swingPattern = UINT64_C(0x00000000aaaaaaaa);
    MD.pattern.accentEditAll = 1;
    MD.pattern.slideEditAll = 1;
    MD.pattern.swingEditAll = 1;
    MD.pattern.ext_scale_mode = 1;
    MD.pattern.ext_tempo_multiplier = 0;
    MD.pattern.ext_chain_row = 52;
    MD.pattern.ext_chain_loops = 3;
    MD.pattern.chain_change = 7;
    MD.pattern.trigPatterns[1] =
        (UINT64_C(1) << 3) | (UINT64_C(1) << 40);
    MD.pattern.ext_track_lengths[1] = 48;
    MD.pattern.ext_track_speeds[1] = 2;
    if (!MD.pattern.addLock(1, 3, 2, 11) ||
        !MD.pattern.addLock(1, 40, 33, 99)) {
        return false;
    }
    MD.pattern.ext_microtiming[1][40] = -7;
    MD.pattern.ext_step_flags[1][3] = 1;
    MD.pattern.ext_step_flags[1][40] = (5u << 2) | 2u;

    ElektronDataToSysexEncoder encoder(message.data());
    encodedLength = MD.pattern.toSysex(&encoder);
    if (encodedLength == 0 ||
        message[7] != MD_PATTERN_VERSION_SPSX) {
        return false;
    }
    if (!hasValidElektronEnvelope(message.data(), encodedLength)) return false;
    auto fixture = std::make_unique<SysexFixture>();
    auto decoded = std::make_unique<MDPattern>();
    if (!publishToMidiSysex(fixture->sysex, message.data(), encodedLength) ||
        !decoded->fromSysex(&fixture->midi)) return false;
    for (uint8_t version : {uint8_t(4), uint8_t(0x41), uint8_t(0x7f)}) {
        message[7] = version;
        if (!publishToMidiSysex(fixture->sysex, message.data(), encodedLength) ||
            decoded->fromSysex(&fixture->midi)) return false;
    }
    return true;
}

bool checkMaximumPatternRoundTrip(uint16_t& encodedLength) {
    std::array<uint8_t, kTestSysexCapacity> message{};
    MD.is_spsx = true;
    MD.fw_caps = FW_CAP_SPSX | FW_CAP_SPSX_PARAMS_37;
    MD.pattern.clearPattern();
    MD.pattern.patternLength = 64;
    ElektronPattern* editingApi = &MD.pattern;

    for (uint8_t track = 0; track < 16; ++track) {
        MD.pattern.trigPatterns[track] = UINT64_MAX;
        for (uint8_t param = 0; param < SPS_PARAMS_PER_TRACK; ++param) {
            MD.pattern.ext_locks_params[track][param] = param + 1;
            for (uint8_t step = 0; step < 64; ++step) {
                if (!editingApi->addLock(
                        track, step, param,
                        stressLockValue(track, param, step))) {
                    return false;
                }
            }
        }
    }

    if (MD.pattern.getLockIdx(15, MODEL_ENVDEST) != MAX_LOCK_ROWS - 1 ||
        editingApi->getNextEmptyLock() != -1 ||
        editingApi->addLock(16, 0, 0, 1)) {
        return false;
    }

    // Exercise clearing/reallocation through the generic API on the final
    // extended row, not just direct wire-array access.
    for (uint8_t step = 0; step < 64; ++step)
        editingApi->clearLock(15, step, MODEL_ENVDEST);
    if (MD.pattern.getLockIdx(15, MODEL_ENVDEST) != -1)
        return false;
    for (uint8_t step = 0; step < 64; ++step) {
        if (!editingApi->addLock(
                15, step, MODEL_ENVDEST,
                stressLockValue(15, MODEL_ENVDEST, step))) {
            return false;
        }
    }

    // A complete 608-lane desktop pattern is a valid hosted live record, but
    // exceeds the MKII hardware event pool. The wire encoder must reject it.
    ElektronDataToSysexEncoder oversizedEncoder(message.data());
    if (MD.pattern.toSysex(&oversizedEncoder) != 0)
        return false;

    // Two fully populated 38-lane tracks fit the MKII event pool and exercise
    // the same dense canonical lane representation.
    MD.pattern.clearPattern();
    MD.pattern.patternLength = 64;
    constexpr uint8_t kWireTracks = 2;
    for (uint8_t track = 0; track < kWireTracks; ++track) {
        MD.pattern.trigPatterns[track] = UINT64_MAX;
        for (uint8_t step = 0; step < 64; ++step)
            MD.pattern.ext_step_flags[track][step] = 1;
        for (uint8_t param = 0;
             param < SPS_PARAMS_PER_TRACK; ++param) {
            MD.pattern.ext_locks_params[track][param] = param + 1;
            for (uint8_t step = 0; step < 64; ++step) {
                if (!editingApi->addLock(
                        track, step, param,
                        stressLockValue(track, param, step))) {
                    return false;
                }
            }
        }
    }

    ElektronDataToSysexEncoder encoder(message.data());
    encodedLength = MD.pattern.toSysex(&encoder);
    if (encodedLength == 0 || encodedLength > 16393 ||
        encodedLength >= message.size() ||
        message[7] != MD_PATTERN_VERSION_SPSX) {
        return false;
    }

    auto fixture = std::make_unique<SysexFixture>();
    if (!publishToMidiSysex(fixture->sysex, message.data(), encodedLength) ||
        fixture->sysex.get_recordLen() != encodedLength - 2) {
        std::fprintf(stderr,
                     "maximum pattern publish: len=%u record=%u\n",
                     encodedLength, fixture->sysex.get_recordLen());
        return false;
    }

    auto decoded = std::make_unique<MDPattern>();
    if (!decoded->fromSysex(&fixture->midi) ||
        decoded->version != MD_PATTERN_VERSION_SPSX ||
        decoded->numRows !=
            kWireTracks * SPS_PARAMS_PER_TRACK) {
        std::fprintf(stderr,
                     "maximum pattern decode: version=%02x rows=%u "
                     "expected=%u\n",
                     decoded->version, decoded->numRows,
                     kWireTracks * SPS_PARAMS_PER_TRACK);
        return false;
    }
    for (uint8_t track = 0; track < kWireTracks; ++track) {
        for (uint8_t param = 0; param < SPS_PARAMS_PER_TRACK; ++param) {
            const int16_t row = decoded->paramLocks[track][param];
            if (row < 0 || row >= MAX_LOCK_ROWS)
                return false;
            const int8_t* values = decoded->lock_row((uint16_t)row);
            for (uint8_t step = 0; step < 64; ++step) {
                if (values[step] !=
                    (int8_t)stressLockValue(track, param, step)) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool checkMalformedCanonicalPatternRejected() {
    std::array<uint8_t, 4096> message{};
    std::array<uint8_t, 4096> body{};
    MD.fw_caps = FW_CAP_SPSX | FW_CAP_SPSX_PARAMS_37;
    MD.pattern.clearPattern();
    MD.pattern.patternLength = 32;
    if (!MD.pattern.addLock(0, 3, 2, 77))
        return false;

    ElektronDataToSysexEncoder encoder(message.data());
    const uint16_t length = MD.pattern.toSysex(&encoder);
    if (length < 15 || message[7] != MD_PATTERN_VERSION_SPSX)
        return false;

    const uint16_t wireBytes = length - 15;
    const uint16_t bodyBytes = ElektronHelper::ElektronSysexToData(
        message.data() + 10, body.data(), wireBytes);
    constexpr uint16_t kFirstEvent =
        16 + 40 + 16 * 40;
    if (bodyBytes <= kFirstEvent + 1)
        return false;

    // Set a reserved descriptor bit, then rebuild a checksum-valid message.
    // The failure therefore exercises the body parser, not the envelope.
    body[kFirstEvent] |= 0x10;
    if (ElektronHelper::ElektronDataToSysex(
            body.data(), message.data() + 10, bodyBytes) != wireBytes) {
        return false;
    }
    ElektronHelper::calculateSysexChecksum(
        message.data(), (uint16_t)(length - 5));

    auto fixture = std::make_unique<SysexFixture>();
    if (!publishToMidiSysex(fixture->sysex, message.data(), length))
        return false;

    MDPattern decoded;
    decoded.clearPattern();
    decoded.patternLength = 64;
    if (!decoded.addLock(1, 5, 3, 99) ||
        decoded.fromSysex(&fixture->midi)) {
        return false;
    }
    return decoded.patternLength == 64 &&
           decoded.paramLocks[1][3] >= 0 &&
           decoded.getLock(1, 5, 3) == 99;
}

#pragma pack(push, 1)
template<uint8_t Params>
struct LegacySpsMachine {
    uint8_t params[Params];
    uint8_t track;
    uint8_t level;
    uint32_t model;
    MDLFO lfos[2];
    uint8_t trigGroup;
    uint8_t muteGroup;
};

template<uint8_t Params>
struct LegacySpsxTrackPayload {
    LegacySpsMachine<Params> machine;
    uint8_t seqVersion;
    BasicStepSeqTrackData<Params> seqData;
    SeqTrackModStorage mod;
    TrackLoadFadeData fade;
};
#pragma pack(pop)

template<uint8_t Params>
bool checkTrackRowMigration(uint8_t version, bool legacySequence) {
    static_assert(sizeof(LegacySpsMachine<Params>) +
                      SPS_PARAMS_PER_TRACK - Params + 1 == sizeof(SPSMachine));
    static_assert(sizeof(LegacySpsxTrackPayload<Params>) +
                      2 * (SPS_PARAMS_PER_TRACK - Params) + 1 ==
                  sizeof(SPSMachine) + sizeof(SPSXTrackStorage) +
                      sizeof(TrackLoadFadeData));
    proj.version = PROJ_VERSION;
    LegacySpsxTrackPayload<Params> old{};
    for (uint8_t param = 0; param < Params; ++param)
        old.machine.params[param] = (uint8_t)(param + 11);
    old.machine.track = 7;
    old.machine.level = 93;
    old.machine.model = 0x20041;
    old.machine.lfos[0].destinationParam = 21;
    old.machine.lfos[1].destinationParam = 28;
    old.machine.trigGroup = 3;
    old.machine.muteGroup = 9;
    old.seqVersion = legacySequence ? SPSX_SEQ_VERSION_LEGACY : SPSX_SEQ_VERSION_SPSX;
    old.seqData.init();
    old.seqData.swing_amount = 19;
    old.seqData.locks_params[Params - 1] = Params;
    old.seqData.steps[7].locks = uint64_t{1} << (Params - 1);
    old.seqData.locks[0] = 88;
    MDSeqTrackData legacy;
    legacy.init();
    legacy.swing_amount = 23;
    legacy.locks_params[0] = 7;
    legacy.steps[3].locks = 1;
    legacy.locks[0] = 49;
    if (legacySequence) std::memcpy(&old.seqData, &legacy, sizeof(legacy));
    std::memset(&old.mod, 0x35, sizeof(old.mod));
    std::memset(&old.fade, 0x59, sizeof(old.fade));

    SPSXTrack migrated;
    migrated.version = version;
    std::memcpy(&migrated.machine, &old, sizeof(old));
    migrated.on_storage_loaded();
    if (migrated.version != SPSX_TRACK_STORAGE_VERSION ||
        std::memcmp(migrated.machine.params, old.machine.params, Params) ||
        std::memcmp(&migrated.machine.track, &old.machine.track,
                    sizeof(old.machine) - Params) ||
        migrated.machine.outputRouting != MD_KIT_ROUTE_GLOBAL ||
        migrated.seq_storage.seq_version != old.seqVersion ||
        std::memcmp(static_cast<SeqTrackModStorage*>(&migrated.seq_storage),
                    &old.mod, sizeof(old.mod)) ||
        std::memcmp(&migrated.load_fade, &old.fade, sizeof(old.fade))) return false;
    for (uint8_t param = Params; param < SPS_PARAMS_PER_TRACK; ++param) {
        if (migrated.machine.params[param] != 0) return false;
    }
    if (legacySequence) {
        if (std::memcmp(&migrated.seq_storage.seq_data.legacy, &legacy,
                        sizeof(legacy))) return false;
    } else {
        const auto& seq = migrated.seq_storage.seq_data.spsx;
        if (seq.swing_amount != 19 || seq.locks_params[Params - 1] != Params ||
            seq.steps[7].locks != (uint64_t{1} << (Params - 1)) ||
            seq.locks[0] != 88) return false;
        for (uint8_t param = Params; param < SPS_PARAMS_PER_TRACK; ++param) {
            if (seq.locks_params[param] != 0) return false;
        }
    }
    // Loading an already migrated record must not change it again.
    std::array<uint8_t, sizeof(SPSXTrack)> before{};
    std::memcpy(before.data(), static_cast<const void*>(&migrated), sizeof(migrated));
    migrated.on_storage_loaded();
    return std::memcmp(before.data(), static_cast<const void*>(&migrated), sizeof(migrated)) == 0;
}

bool checkTrackRowMigrations() {
    for (bool legacy : {false, true}) {
        if (!checkTrackRowMigration<34>(5, legacy) ||
            !checkTrackRowMigration<37>(6, legacy)) return false;
    }
    MDFXTrack oldFx;
    oldFx.version = 0;
    std::memset(oldFx.userBusFx, 1, sizeof(oldFx.userBusFx));
    std::memset(oldFx.userPostFx, 1, sizeof(oldFx.userPostFx));
    oldFx.on_storage_loaded();
    if (oldFx.version != MDFX_TRACK_STORAGE_VERSION_ROUTED_FX) {
        std::fprintf(stderr, "MDFX migration version=%u\n", oldFx.version);
        return false;
    }
    for (const auto& bus : oldFx.userBusFx) {
        for (uint8_t value : bus) {
            if (value != SPS_USER_FX_DEFAULT_PARAM) {
                std::fprintf(stderr, "MDFX bus default=%u\n", value);
                return false;
            }
        }
    }
    for (uint8_t value : oldFx.userPostFx) {
        if (value != SPS_USER_FX_DEFAULT_PARAM) {
            std::fprintf(stderr, "MDFX post default=%u\n", value);
            return false;
        }
    }

    MDFXTrack currentFx;
    currentFx.version = MDFX_TRACK_STORAGE_VERSION_ROUTED_FX;
    std::memset(currentFx.userBusFx, 23, sizeof(currentFx.userBusFx));
    std::memset(currentFx.userPostFx, 47, sizeof(currentFx.userPostFx));
    currentFx.on_storage_loaded();
    const bool preserved = currentFx.userBusFx[0][0] == 23 &&
                           currentFx.userPostFx[0] == 47;
    if (!preserved) {
        std::fprintf(stderr, "MDFX current changed=%u/%u\n",
                     currentFx.userBusFx[0][0], currentFx.userPostFx[0]);
    }
    return preserved;
}

bool checkNegotiatedExtendedCcMapping() {
    MidiUartClass output;
    const bool wasConnected = MD.connected;
    MD.connected = true;
    MD.is_spsx = true;
    MD.global.channelMode = 1;

    for (uint32_t caps : {uint32_t(0), uint32_t(FW_CAP_SPSX_PARAMS_37)}) {
        MD.fw_caps = FW_CAP_SPSX | caps;
        const uint8_t wireParams = SPS_PARAMS_PER_TRACK;
        for (uint8_t base : {uint8_t(0), uint8_t(8)}) {
            MD.global.baseChannel = base;
            for (uint8_t track = 0; track < 16; ++track) {
                for (uint8_t param = MD_PARAMS_PER_TRACK;
                     param < SPS_PARAMS_PER_TRACK; ++param) {
                    SpsExtendedCcAddress address{};
                    if (!spsExtendedParamToCc(base, track, param, address)) {
                        MD.connected = wasConnected;
                        return false;
                    }

                    uint8_t decodedTrack = 0;
                    uint8_t decodedParam = 0;
                    MD.parseCC(address.channel, address.cc, &decodedTrack,
                               &decodedParam);
                    const bool supported = param < wireParams;
                    if (supported ? (decodedTrack != track ||
                                     decodedParam != param)
                                  : decodedTrack != 255) {
                        MD.connected = wasConnected;
                        return false;
                    }

                    output.init();
                    MD.setTrackParam(track, param, 73, &output, true);
                    uint8_t bytes[4] = {};
                    const size_t count =
                        output.desktop_egress(bytes, sizeof(bytes));
                    if (supported) {
                        if (count != 3 ||
                            bytes[0] != (uint8_t)(MIDI_CONTROL_CHANGE |
                                                  address.channel) ||
                            bytes[1] != address.cc || bytes[2] != 73) {
                            MD.connected = wasConnected;
                            return false;
                        }
                    } else if (count != 0) {
                        MD.connected = wasConnected;
                        return false;
                    }
                }
            }
        }
    }

    MD.global.baseChannel = 9;
    output.init();
    MD.setTrackParam(0, MODEL_BUS1, 73, &output, true);
    uint8_t byte = 0;
    const bool invalidBaseRejected = output.desktop_egress(&byte, 1) == 0;
    MD.connected = wasConnected;
    return invalidBaseRejected;
}

bool checkEncoderInterfacePacketWidths() {
    struct Case {
        uint32_t caps;
        uint8_t requestedParams;
        uint8_t expectedParams;
    };
    constexpr Case cases[] = {
        {0, MD_PARAMS_PER_TRACK, MD_PARAMS_PER_TRACK},
        {0, SPS_PARAMS_V1_PER_TRACK, SPS_PARAMS_V1_PER_TRACK},
        {0, SPS_PARAMS_PER_TRACK, SPS_PARAMS_PER_TRACK},
        {FW_CAP_SPSX_PARAMS_37, MD_PARAMS_PER_TRACK, MD_PARAMS_PER_TRACK},
        {FW_CAP_SPSX_PARAMS_37, SPS_PARAMS_V1_PER_TRACK,
         SPS_PARAMS_V1_PER_TRACK},
        {FW_CAP_SPSX_PARAMS_37, SPS_PARAMS_PER_TRACK,
         SPS_PARAMS_PER_TRACK},
    };

    std::array<uint8_t, SPS_PARAMS_PER_TRACK> params{};
    for (uint8_t i = 0; i < params.size(); ++i)
        params[i] = (i % 5 == 2) ? 255 : (uint8_t)(i + 40);

    MidiUartClass output;
    const bool wasConnected = MD.connected;
    const bool wasSpsx = MD.is_spsx;
    const uint32_t wasCaps = MD.fw_caps;
    MidiUartClass* const wasUart = MD.uart;
    bool ok = true;

    MD.connected = true;
    MD.is_spsx = true;
    MD.uart = &output;
    for (const Case& test : cases) {
        MD.fw_caps = FW_CAP_SPSX | test.caps;
        output.init();
        MD.activate_encoder_interface(params.data(), test.requestedParams);

        std::array<uint8_t, 64> packet{};
        const size_t packetBytes =
            output.desktop_egress(packet.data(), packet.size());
        const uint8_t maskBytes =
            (uint8_t)((test.expectedParams + 6) / 7);
        const size_t expectedBytes =
            (size_t)(10 + maskBytes + test.expectedParams);
        if (packetBytes != expectedBytes || packet[0] != 0xF0 ||
            packet[1] != 0x00 || packet[2] != 0x20 ||
            packet[3] != 0x3C || packet[4] != 0x02 ||
            packet[5] != 0x00 || packet[6] != 0x70 ||
            packet[7] != 0x36 || packet[8] != 0x01 ||
            packet[packetBytes - 1] != 0xF7) {
            ok = false;
            break;
        }

        for (uint8_t i = 0; i < test.expectedParams; ++i) {
            const bool expectedEnabled = params[i] != 255;
            const bool enabled =
                (packet[9 + i / 7] & (uint8_t)(1u << (i % 7))) != 0;
            const uint8_t value = packet[9 + maskBytes + i];
            if (enabled != expectedEnabled ||
                (expectedEnabled && value != params[i])) {
                ok = false;
                break;
            }
        }
        if (!ok)
            break;
    }

    MD.uart = wasUart;
    MD.fw_caps = wasCaps;
    MD.is_spsx = wasSpsx;
    MD.connected = wasConnected;
    return ok;
}

bool deliverSequencerFrame(uint8_t command, uint8_t tag,
                           const uint8_t* body, uint16_t bodyBytes) {
    std::array<uint8_t, 128> frame{};
    const uint16_t frameBytes = spsseq::spsSeqBuildFrame(
        command, tag, body, bodyBytes, frame.data(), frame.size());
    if (frameBytes == 0 ||
        MidiUart.desktop_ingress(frame.data(), frameBytes) != frameBytes) {
        return false;
    }
    Midi.processSysex();
    return true;
}

bool readHelloAck(uint8_t expectedVersion, uint8_t expectedParams) {
    std::array<uint8_t, 128> frame{};
    const size_t frameBytes =
        MidiUart.desktop_egress(frame.data(), frame.size());
    if (frameBytes < 2 || frame[0] != 0xF0 ||
        frame[frameBytes - 1] != 0xF7) {
        return false;
    }
    spsseq::Parsed parsed{};
    if (!spsseq::spsSeqParseFrame(frame.data() + 1,
                                  (uint16_t)(frameBytes - 2), parsed) ||
        parsed.cmd != spsseq::CMD_HELLO_ACK) {
        return false;
    }
    std::array<uint8_t, 16> body{};
    const uint16_t bodyBytes =
        spsseq::spsSeqDecodeBody(parsed, body.data(), body.size());
    return bodyBytes >= 6 && body[0] == expectedVersion &&
           body[5] == expectedParams;
}

bool readSequencerResponse(uint8_t expectedCommand,
                           std::array<uint8_t, spsseq::kMaxBodyRaw>& body,
                           uint16_t& bodyBytes) {
    std::array<uint8_t, 1200> frame{};
    const size_t frameBytes =
        MidiUart.desktop_egress(frame.data(), frame.size());
    if (frameBytes < 2 || frame[0] != 0xF0 ||
        frame[frameBytes - 1] != 0xF7) {
        return false;
    }
    spsseq::Parsed parsed{};
    if (!spsseq::spsSeqParseFrame(frame.data() + 1,
                                  (uint16_t)(frameBytes - 2), parsed) ||
        parsed.cmd != expectedCommand) {
        return false;
    }
    bodyBytes = spsseq::spsSeqDecodeBody(
        parsed, body.data(), (uint16_t)body.size());
    return bodyBytes <= body.size();
}

bool readSingleBusLockResponse(uint8_t expectedParam, uint8_t expectedValue) {
    std::array<uint8_t, spsseq::kMaxBodyRaw> body{};
    uint16_t bodyBytes = 0;
    if (!readSequencerResponse(spsseq::CMD_TRACK_LOCKS, body, bodyBytes) ||
        bodyBytes < 9 || body[0] != 0 || body[1] != 0x41 ||
        body[2] != 0 || body[3] != 1 || body[4] != 1 ||
        body[5] != expectedParam || body[6] != 1 || body[7] != 0 ||
        body[8] != 1 || bodyBytes != 10) {
        return false;
    }
    return body[9] == expectedValue;
}

bool checkSequencerNegotiation(uint8_t param, uint8_t value) {
    MidiUart.init();
    MidiSysex.rb->init();
    std::memset(MidiSysex.ledger, 0, sizeof(MidiSysex.ledger));
    MidiSysex.msg_wr = 0;
    MidiSysex.msg_rd = 0;
    MidiSysex.rd_cur = 0;
    MidiSysex.initSysexListeners();
    sps_host_seq_bridge.setup();

    SPSXSeqTrack& track = mcl_seq.spsx_tracks[0];
    track.clear_locks();
    const uint8_t capabilities[2] = {
        (uint8_t)(spsseq::CAP_SPSX & 0xFF),
        (uint8_t)(spsseq::CAP_SPSX >> 8),
    };

    const uint8_t helloV1[3] = {
        spsseq::kProtoVersionV1, capabilities[0], capabilities[1]};
    if (!deliverSequencerFrame(spsseq::CMD_HELLO, 1, helloV1,
                               sizeof(helloV1)) ||
        sps_host_seq_bridge.negotiatedProtoVersion() !=
            spsseq::kProtoVersionV1 ||
        sps_host_seq_bridge.negotiatedLockParams() !=
            spsseq::kNumLockParamsV1 ||
        !readHelloAck(spsseq::kProtoVersionV1,
                      spsseq::kNumLockParamsV1)) {
        return false;
    }

    const uint8_t v1BusLock[4] = {0, 0, param, value};
    if (!deliverSequencerFrame(spsseq::CMD_SET_LOCK, 2, v1BusLock,
                               sizeof(v1BusLock)) ||
        track.find_param(param) != 255) {
        return false;
    }
    uint8_t responseByte = 0;
    if (MidiUart.desktop_egress(&responseByte, 1) != 0)
        return false;

    const uint8_t helloV2[3] = {
        spsseq::kProtoVersion, capabilities[0], capabilities[1]};
    if (!deliverSequencerFrame(spsseq::CMD_HELLO, 3, helloV2,
                               sizeof(helloV2)) ||
        sps_host_seq_bridge.negotiatedProtoVersion() !=
            spsseq::kProtoVersion ||
        sps_host_seq_bridge.negotiatedLockParams() !=
            spsseq::kNumLockParams ||
        !readHelloAck(spsseq::kProtoVersion,
                      spsseq::kNumLockParams)) {
        return false;
    }

    // Exercise the exact host-facing transport and bridge surface used by
    // both an external SPS-X peer and the hosted WASM module: set BUS3 through
    // a framed command, then query TRACK_LOCKS and decode param 36 back from
    // the framed response.
    const uint8_t v2BusLock[4] = {0, 0, param, value};
    if (!deliverSequencerFrame(spsseq::CMD_SET_LOCK, 4, v2BusLock,
                               sizeof(v2BusLock))) {
        return false;
    }
    std::array<uint8_t, spsseq::kMaxBodyRaw> notification{};
    uint16_t notificationBytes = 0;
    if (!readSequencerResponse(spsseq::CMD_NOTIFY_DIRTY, notification,
                               notificationBytes) ||
        notificationBytes < 2 || notification[0] != 0 ||
        (notification[1] & (spsseq::DIRTY_LOCKS |
                            spsseq::DIRTY_SUMMARY)) == 0) {
        return false;
    }

    std::array<uint8_t, SPS_PARAMS_PER_TRACK> locks{};
    locks.fill(255);
    track.get_step_locks(0, locks.data(), false);
    const bool acceptedByV2 =
        track.find_param(param) != 255 &&
        locks[param] == value;
    const uint8_t trackRequest = 0;
    const bool roundTripped = acceptedByV2 &&
        deliverSequencerFrame(spsseq::CMD_REQ_TRACK_LOCKS, 5,
                              &trackRequest, 1) &&
        readSingleBusLockResponse(param, value);
    track.clear_locks();
    return roundTripped;
}

} // namespace

// MDX keeps a step's condition mode in two bits. MCL carries both through a
// pattern and into an SPS-X track, and plays the third mode, LOCK.
bool checkConditionModes() {
    // The play rule: which of the step's locks and trig go out.
    struct Case {
        uint8_t result;
        bool plock, lockOnly, trig, muted, locks, fire;
    };
    static const Case cases[] = {
        // TRIG: the locks always go; the trig waits on the condition.
        {SPSX_TRIG_TRUE, false, false, true, false, true, true},
        {SPSX_TRIG_FALSE, false, false, true, false, true, false},
        // TRIG+LOCK: both wait on the condition.
        {SPSX_TRIG_TRUE, true, false, true, false, true, true},
        {SPSX_TRIG_FALSE, true, false, true, false, false, false},
        // LOCK: the trig plays anyway; only the locks wait.
        {SPSX_TRIG_TRUE, true, true, true, false, true, true},
        {SPSX_TRIG_FALSE, true, true, true, false, false, true},
        // A muted LOCK step is silent; a spent one-shot keeps its plain hit.
        {SPSX_TRIG_ONESHOT, true, true, true, true, false, false},
        {SPSX_TRIG_ONESHOT, true, true, true, false, false, true},
        // LOCK on a step with locks but no trig.
        {SPSX_TRIG_TRUE, true, true, false, false, true, false},
        {SPSX_TRIG_FALSE, true, true, false, false, false, false},
        // A muted TRIG step sends nothing.
        {SPSX_TRIG_ONESHOT, false, false, true, true, false, false},
    };
    for (const Case& c : cases) {
        const SPSXStepPlan plan = spsx_plan_step(
            c.result, c.plock, c.lockOnly, c.trig, c.muted);
        if (plan.apply_locks != c.locks || plan.fire != c.fire) return false;
    }

    // A pattern keeps the second mode bit (bit 14 of a condition word).
    const uint8_t kTrack = 2;
    const uint8_t kRatio = SEQ_COND_ITER_BASE; // 1:2
    std::array<uint8_t, 8192> message{};
    std::array<uint8_t, 8192> again{};
    MD.is_spsx = true;
    MD.fw_caps = FW_CAP_SPSX | FW_CAP_SPSX_PARAMS_37;
    MD.pattern.clearPattern();
    MD.pattern.patternLength = 64;
    MD.pattern.trigPatterns[kTrack] =
        (UINT64_C(1) << 1) | (UINT64_C(1) << 5) | (UINT64_C(1) << 9) |
        (UINT64_C(1) << 13);
    MD.pattern.ext_step_flags[kTrack][1] = (kRatio << 2) | 2u;  // TRIG+LOCK
    MD.pattern.ext_step_flags[kTrack][5] = (kRatio << 2) | 2u;  // LOCK
    MD.pattern.ext_step_flags[kTrack][9] = 0;                   // explicit TRIG
    // 90% on the Machinedrum is condition 1; MCL's 90% is condition 7.
    const uint8_t kMd90 = 1;
    MD.pattern.ext_step_flags[kTrack][13] = kMd90 << 2;          // TRIG
    MD.pattern.ext_condition_mode_high[kTrack] =
        (UINT64_C(1) << 5) | (UINT64_C(1) << 9);

    ElektronDataToSysexEncoder encoder(message.data());
    const uint16_t length = MD.pattern.toSysex(&encoder);
    if (length == 0 || message[7] != MD_PATTERN_VERSION_SPSX) return false;
    auto fixture = std::make_unique<SysexFixture>();
    auto decoded = std::make_unique<MDPattern>();
    if (!publishToMidiSysex(fixture->sysex, message.data(), length) ||
        !decoded->fromSysex(&fixture->midi)) {
        return false;
    }
    if (decoded->ext_condition_mode_high[kTrack] !=
            MD.pattern.ext_condition_mode_high[kTrack] ||
        decoded->ext_step_flags[kTrack][1] != ((kRatio << 2) | 2u) ||
        decoded->ext_step_flags[kTrack][5] != ((kRatio << 2) | 2u) ||
        decoded->ext_step_flags[kTrack][9] != 0 ||
        decoded->ext_step_flags[kTrack][13] != (kMd90 << 2)) {
        return false;
    }
    for (uint8_t track = 0; track < 16; ++track) {
        if (track != kTrack && decoded->ext_condition_mode_high[track] != 0)
            return false;
    }
    ElektronDataToSysexEncoder reencoder(again.data());
    if (decoded->toSysex(&reencoder) != length ||
        std::memcmp(message.data(), again.data(), length) != 0) {
        return false;
    }

    // An SPS-X track takes both bits from the pattern.
    SPSXSeqTrack& track = mcl_seq.spsx_tracks[kTrack];
    track.merge_from_md(kTrack, decoded.get());
    const auto& steps = track.steps;
    if (steps[1].cond_id != kRatio || !steps[1].cond_plock ||
        steps[1].cond_explicit || steps[1].cond_lock_only() ||
        steps[5].cond_id != kRatio || !steps[5].cond_lock_only() ||
        steps[9].cond_id != 0 || steps[9].cond_plock ||
        !steps[9].cond_explicit || steps[9].cond_lock_only() ||
        steps[13].cond_id != SEQ_COND_90PCT || steps[13].cond_plock ||
        steps[13].cond_explicit) {
        return false;
    }
    // The probability mapping mirrors 1-7 and leaves every other id alone.
    for (uint8_t id = 0; id < SEQ_NUM_TRIG_CONDITIONS; ++id) {
        const uint8_t mapped = seq_cond_from_md(id);
        const uint8_t expected = (id >= 1 && id <= 7) ? uint8_t(8 - id) : id;
        if (mapped != expected || seq_cond_to_md(mapped) != id) return false;
    }

    // The hosted editor names conditions with the Machinedrum's numbers, so
    // the bridge converts them both ways.
    {
        std::array<uint8_t, 1200> drained{};
        while (MidiUart.desktop_egress(drained.data(), drained.size()) != 0) {}
        SPSXSeqTrack& bridged = mcl_seq.spsx_tracks[0];
        const uint8_t setCondition[3] = {0, 3, uint8_t(kMd90 | 0x80)};
        if (!deliverSequencerFrame(spsseq::CMD_SET_CONDITION, 6, setCondition,
                                   sizeof(setCondition)) ||
            bridged.steps[3].cond_id != SEQ_COND_90PCT ||
            !bridged.steps[3].cond_plock) {
            return false;
        }
        while (MidiUart.desktop_egress(drained.data(), drained.size()) != 0) {}
        const uint8_t detailTrack = 0;
        std::array<uint8_t, spsseq::kMaxBodyRaw> body{};
        uint16_t bodyBytes = 0;
        if (!deliverSequencerFrame(spsseq::CMD_REQ_TRACK_DETAIL, 7,
                                   &detailTrack, 1) ||
            !readSequencerResponse(spsseq::CMD_TRACK_DETAIL, body, bodyBytes) ||
            bodyBytes < 2 + 2 * spsseq::kNumSteps ||
            body[2 + spsseq::kNumSteps + 3] != uint8_t(kMd90 | 0x80)) {
            return false;
        }
        bridged.set_conditional_value(3, 0, false);
    }

    // An edit from MCL chooses TRIG or TRIG+LOCK. A LOCK step stays LOCK
    // while it stays on the lock side, and loses it on the trig side.
    track.set_conditional_value(5, kRatio + 1, true);
    if (!track.steps[5].cond_lock_only() ||
        track.steps[5].cond_id != kRatio + 1) {
        return false;
    }
    track.set_conditional_value(5, kRatio + 1, false);
    track.set_conditional_value(9, 0, false);
    return !track.steps[5].cond_explicit && !track.steps[5].cond_plock &&
           !track.steps[9].cond_explicit;
}

// MCL sends a step's condition and mode with the microtiming popup, in the
// Machinedrum's numbers, and steps the mode in the Machinedrum's order.
bool checkMicrotimingPopupCondition() {
    MidiUartClass output;
    const bool wasConnected = MD.connected;
    MidiUartClass* const wasUart = MD.uart;
    MD.connected = true;
    MD.uart = &output;
    bool ok = true;
    std::array<uint8_t, 32> packet{};
    const auto sent = [&](const uint8_t* expected, size_t bytes) {
        const size_t n = output.desktop_egress(packet.data(), packet.size());
        return n == bytes && std::memcmp(packet.data(), expected, bytes) == 0;
    };

    // Without a condition the command is the one older firmware expects.
    output.init();
    MD.draw_microtiming(2, 130);
    const uint8_t plain[] = {0xF0, 0x00, 0x20, 0x3C, 0x02, 0x00, 0x70,
                             0x3C, 0x20, 2,    1,    2,    0xF7};
    ok &= sent(plain, sizeof(plain));

    // With one, the condition and mode follow the timing. MCL's 90% is the
    // Machinedrum's condition 1; mode 5 is LOCK.
    output.init();
    MD.draw_microtiming(2, 130, seq_cond_to_md(SEQ_COND_90PCT), 5);
    const uint8_t ruler[] = {0xF0, 0x00, 0x20, 0x3C, 0x02, 0x00, 0x70, 0x3C,
                             0x20, 2,    1,    2,    1,    5,    0xF7};
    ok &= sent(ruler, sizeof(ruler));

    output.init();
    MD.draw_microtiming_signed(0, -7, seq_cond_to_md(SEQ_COND_ITER_BASE), 1);
    const uint8_t signedForm[] = {0xF0, 0x00, 0x20, 0x3C, 0x02,
                                  0x00, 0x70, 0x3C, 0x27, 0,
                                  0,    120,  SEQ_COND_ITER_BASE, 1, 0xF7};
    ok &= sent(signedForm, sizeof(signedForm));

    // PAGE steps an SPS-X step TRIG -> LOCK -> TRIG+LOCK -> TRIG, reported
    // as the Machinedrum's mode numbers 0, 5, 1.
    SPSXSeqTrack& track = mcl_seq.spsx_tracks[4];
    track.set_conditional_value(6, SEQ_COND_ITER_BASE, false);
    ok &= track.condition_mode_value(6) == 0;
    track.cycle_condition_mode_value(6);
    ok &= track.condition_mode_value(6) == 5 && track.steps[6].cond_lock_only();
    track.cycle_condition_mode_value(6);
    ok &= track.condition_mode_value(6) == 1 && track.steps[6].cond_plock &&
          !track.steps[6].cond_explicit;
    track.cycle_condition_mode_value(6);
    ok &= track.condition_mode_value(6) == 0 && !track.steps[6].cond_plock;
    ok &= track.steps[6].cond_id == SEQ_COND_ITER_BASE;
    // A step with no condition has no mode to step.
    track.set_conditional_value(7, SEQ_COND_100PCT, false);
    track.cycle_condition_mode_value(7);
    ok &= track.condition_mode_value(7) == 0 && !track.steps[7].cond_plock &&
          !track.steps[7].cond_explicit;
    track.set_conditional_value(6, SEQ_COND_100PCT, false);

    MD.connected = wasConnected;
    MD.uart = wasUart;
    return ok;
}

// Rewrites the settings and track headers of an encoded compact pattern, as
// MDX would write them, and keeps the envelope and checksum valid.
template <typename Patch>
bool patchCompactPattern(std::array<uint8_t, 8192>& message, uint16_t length,
                         Patch patch) {
    std::array<uint8_t, 8192> body{};
    const uint16_t wireBytes = length - 15;
    const uint16_t bodyBytes = ElektronHelper::ElektronSysexToData(
        message.data() + 10, body.data(), wireBytes);
    if (bodyBytes < 16 + 40 + 16 * 40)
        return false;
    patch(body.data() + 16, body.data() + 16 + 40);
    if (ElektronHelper::ElektronDataToSysex(
            body.data(), message.data() + 10, bodyBytes) != wireBytes) {
        return false;
    }
    ElektronHelper::calculateSysexChecksum(
        message.data(), (uint16_t)(length - 5));
    return true;
}

bool decodePatternMessage(const uint8_t* message, uint16_t length,
                          MDPattern& decoded) {
    auto fixture = std::make_unique<SysexFixture>();
    return publishToMidiSysex(fixture->sysex, message, length) &&
           decoded.fromSysex(&fixture->midi);
}

// MDX pattern timing: master length 0 (INF) to 255, the ALL timing stored
// apart from the master cycle (byte 33 bit 7), and older records.
bool checkMdxPatternTiming() {
    MD.is_spsx = true;
    MD.fw_caps = FW_CAP_SPSX | FW_CAP_SPSX_PARAMS_37;
    MD.pattern.clearPattern();
    MD.pattern.patternLength = 64;
    MD.pattern.scale = 0;
    MD.pattern.doubleTempo = 0;
    MD.pattern.trigPatterns[1] = 1;
    MD.pattern.trigPatterns[2] = 1;
    std::array<uint8_t, 8192> base{};
    ElektronDataToSysexEncoder encoder(base.data());
    const uint16_t length = MD.pattern.toSysex(&encoder);
    if (length == 0 || base[7] != MD_PATTERN_VERSION_SPSX)
        return false;

    struct Case {
        uint8_t mode, masterLength, allScaleSpeed, masterSpeed, allLength;
        bool valid;
        uint8_t length, speed, scale;
        // Expected length/speed of track 1 (stores length 48) and track 2
        // (stores speed 1/2x).
        uint8_t length1, speed1, length2, speed2;
    };
    static const Case cases[] = {
        // TRACK, INF, ALL 24 steps at 2x.
        {0, 0, 0x80 | 1 | (1 << 2), 0, 24, true, 24, 1, 1, 48, 1, 24, 4},
        // TRACK, master length 200, ALL 64 steps at 1/2x.
        {0, 200, 0x80 | 3 | (4 << 2), 2, 64, true, 64, 4, 3, 48, 4, 64, 4},
        // ALL ignores what the tracks store.
        {1, 128, 0x80 | 2 | (2 << 2), 0, 40, true, 40, 2, 2, 40, 2, 40, 2},
        {1, 0, 0x80 | 0 | (7 << 2), 5, 1, true, 1, 7, 0, 1, 7, 1, 7},
        // Older records: INF takes the scale's length, longer ones stop at 64.
        {0, 0, 3, 1, 0, true, 64, 1, 3, 48, 1, 64, 4},
        {1, 100, 0, 0, 1, true, 64, 0, 0, 64, 0, 64, 0},
        {0, 20, 1, 3, 0, true, 20, 3, 1, 48, 3, 20, 4},
        // Invalid timing.
        {0, 16, 0x80, 0, 0, false},
        {0, 16, 0x80 | 1, 0, 40, false},
        {0, 16, 0xa0 | 1, 0, 16, false},
        {0, 16, 0, 0, 2, false},
        {0, 16, 4, 0, 0, false},
        {2, 16, 0, 0, 0, false},
        {0, 16, 0, 8, 0, false},
    };
    for (const Case& c : cases) {
        std::array<uint8_t, 8192> message = base;
        if (!patchCompactPattern(message, length,
                [&c](uint8_t* settings, uint8_t* tracks) {
                    settings[31] = c.mode;
                    settings[32] = c.masterLength;
                    settings[33] = c.allScaleSpeed;
                    settings[34] = c.masterSpeed;
                    settings[35] = c.allLength;
                    for (uint8_t track = 0; track < 16; ++track) {
                        tracks[track * 40 + 32] = 0;
                        tracks[track * 40 + 33] = 0xff;
                    }
                    tracks[1 * 40 + 32] = 48;
                    tracks[2 * 40 + 33] = SEQ_SPEED_1_2X;
                })) {
            return false;
        }
        auto decoded = std::make_unique<MDPattern>();
        const bool ok = decodePatternMessage(message.data(), length, *decoded);
        if (ok != c.valid) {
            std::fprintf(stderr, "mdx timing %u/%u/%02x/%u/%u: decode=%d\n",
                         c.mode, c.masterLength, c.allScaleSpeed,
                         c.masterSpeed, c.allLength, ok);
            return false;
        }
        if (!ok)
            continue;
        SPSXSeqTrack spsx1, spsx2;
        for (SPSXSeqTrack* track : {&spsx1, &spsx2}) {
            // As SPSXTrack imports a row.
            track->length = decoded->patternLength;
            track->speed = decoded->doubleTempo;
        }
        spsx1.merge_from_md(1, decoded.get());
        spsx2.merge_from_md(2, decoded.get());
        if (decoded->patternLength != c.length ||
            decoded->doubleTempo != c.speed || decoded->scale != c.scale ||
            decoded->ext_scale_mode != c.mode ||
            decoded->trackLength(1) != c.length1 ||
            decoded->trackSpeed(1) != c.speed1 ||
            decoded->trackLength(2) != c.length2 ||
            decoded->trackSpeed(2) != c.speed2 ||
            decoded->trackLength(5) != c.length ||
            decoded->trackSpeed(5) != c.speed ||
            spsx1.length != c.length1 || spsx1.speed != c.speed1 ||
            spsx2.length != c.length2 || spsx2.speed != c.speed2) {
            std::fprintf(stderr,
                         "mdx timing %u/%u/%02x/%u/%u: len=%u speed=%u "
                         "scale=%u t1=%u/%u t2=%u/%u spsx=%u/%u %u/%u\n",
                         c.mode, c.masterLength, c.allScaleSpeed,
                         c.masterSpeed, c.allLength, decoded->patternLength,
                         decoded->doubleTempo, decoded->scale,
                         decoded->trackLength(1), decoded->trackSpeed(1),
                         decoded->trackLength(2), decoded->trackSpeed(2),
                         spsx1.length, spsx1.speed, spsx2.length,
                         spsx2.speed);
            return false;
        }
    }
    return true;
}

// MDX X.20 and later play locks on steps without trigs; a stock Machinedrum
// or X.14 does not, and MCL keeps only the locks it would play.
bool checkTriglessLockImport() {
    constexpr uint8_t kTrack = 4;
    constexpr uint8_t kParam = MODEL_P2;
    constexpr uint8_t kOther = MODEL_P4;
    constexpr uint8_t kCondition = 3;  // Machinedrum 3, MCL 5
    const auto build = [&]() {
        MD.pattern.clearPattern();
        MD.pattern.patternLength = 16;
        MD.pattern.trigPatterns[kTrack] = UINT64_C(1) << 2;
        // Step 2 has a trig, 6 and 9 have none; 9's locks are switched off.
        MD.pattern.addLock(kTrack, 2, kParam, 40);
        MD.pattern.addLock(kTrack, 6, kParam, 50);
        MD.pattern.addLock(kTrack, 6, kOther, 51);
        MD.pattern.addLock(kTrack, 9, kParam, 60);
        MD.pattern.ext_step_flags[kTrack][2] = 1;
        MD.pattern.ext_step_flags[kTrack][6] = 1 | 2 | (kCondition << 2);
        MD.pattern.ext_step_flags[kTrack][9] = 0;
        MD.pattern.ext_microtiming[kTrack][6] = 64;
    };
    struct Expect {
        uint8_t step2, step6, other6, step9;
    };
    const auto check = [&](const char* name, MDPattern& decoded,
                           const Expect& expect, bool compact) {
        SPSXSeqTrack spsx;
        spsx.length = 16;
        spsx.speed = SEQ_SPEED_1X;
        spsx.merge_from_md(kTrack, &decoded);
        MDSeqTrack md;
        md.init();
        md.length = 16;
        md.speed = SEQ_SPEED_1X;
        md.merge_from_md(kTrack, &decoded);
        const uint8_t gotSpsx[4] = {
            spsx.get_track_lock_implicit(2, kParam),
            spsx.get_track_lock_implicit(6, kParam),
            spsx.get_track_lock_implicit(6, kOther),
            spsx.get_track_lock_implicit(9, kParam)};
        const uint8_t gotMd[4] = {
            md.get_track_lock_implicit(2, kParam),
            md.get_track_lock_implicit(6, kParam),
            md.get_track_lock_implicit(6, kOther),
            md.get_track_lock_implicit(9, kParam)};
        const uint8_t want[4] = {expect.step2, expect.step6, expect.other6,
                                 expect.step9};
        bool ok = std::memcmp(gotSpsx, want, 4) == 0 &&
                  std::memcmp(gotMd, want, 4) == 0 &&
                  !md.steps[6].trig && !SPSX_IS_BIT_SET64(spsx.trig_mask, 6);
        if (compact) {
            // The trigless step keeps its condition and microtiming too.
            const uint8_t mclCondition = seq_cond_from_md(kCondition);
            ok = ok && spsx.steps[6].cond_id == mclCondition &&
                 spsx.steps[6].cond_plock && spsx.microtiming[6] == 64 &&
                 md.steps[6].cond_id == mclCondition &&
                 md.steps[6].cond_plock &&
                 md.microtiming[6] ==
                     spsx_microtiming_to_md_microtiming(
                         64, SeqTrack::get_ticks_per_step(SEQ_SPEED_1X));
        }
        if (!ok) {
            std::fprintf(stderr,
                         "trigless locks %s: spsx=%u/%u/%u/%u md=%u/%u/%u/%u "
                         "want=%u/%u/%u/%u\n",
                         name, gotSpsx[0], gotSpsx[1], gotSpsx[2], gotSpsx[3],
                         gotMd[0], gotMd[1], gotMd[2], gotMd[3], want[0],
                         want[1], want[2], want[3]);
        }
        return ok;
    };

    const bool wasSpsx = MD.is_spsx;
    const uint32_t wasCaps = MD.fw_caps;
    bool ok = true;
    std::array<uint8_t, 8192> message{};
    {
        // Compact pattern: only MDX X.20 or later sends one.
        MD.is_spsx = true;
        MD.fw_caps = FW_CAP_SPSX | FW_CAP_SPSX_PARAMS_37;
        build();
        ElektronDataToSysexEncoder encoder(message.data());
        const uint16_t length = MD.pattern.toSysex(&encoder);
        auto decoded = std::make_unique<MDPattern>();
        ok = ok && length != 0 && message[7] == MD_PATTERN_VERSION_SPSX &&
             decodePatternMessage(message.data(), length, *decoded) &&
             check("compact", *decoded, {40, 50, 51, 255}, true);
    }
    {
        // Stock format from an SPS-X peer: every lock is kept; the format
        // cannot mark locks as switched off.
        MD.is_spsx = true;
        MD.fw_caps = FW_CAP_SPSX;
        build();
        ElektronDataToSysexEncoder encoder(message.data());
        const uint16_t length = MD.pattern.toSysex(&encoder);
        auto decoded = std::make_unique<MDPattern>();
        ok = ok && length != 0 && message[7] == 0x03 &&
             decodePatternMessage(message.data(), length, *decoded) &&
             check("stock-spsx", *decoded, {40, 50, 51, 60}, false);
    }
    {
        // Stock Machinedrum or X.14: only locks on trigs.
        MD.is_spsx = false;
        MD.fw_caps = 0;
        build();
        ElektronDataToSysexEncoder encoder(message.data());
        const uint16_t length = MD.pattern.toSysex(&encoder);
        auto decoded = std::make_unique<MDPattern>();
        ok = ok && length != 0 && message[7] == 0x03 &&
             decodePatternMessage(message.data(), length, *decoded) &&
             check("stock", *decoded, {40, 255, 255, 255}, false);
    }
    MD.is_spsx = wasSpsx;
    MD.fw_caps = wasCaps;
    return ok;
}

// Microtiming between MDX/SPS-X steps (1/384-note ticks, at most 23 and one
// short of the next step) and MD track steps (12 ticks per step at 1x).
bool checkMicrotimingConversion() {
    SPSXSeqTrack& spsx = mcl_seq.spsx_tracks[0];
    for (uint8_t speed = SEQ_SPEED_1X; speed <= SEQ_SPEED_4X; ++speed) {
        const uint16_t mdTps = SeqTrack::get_ticks_per_step(speed);
        const uint16_t spsxTps = spsx.get_ticks_per_step(speed);
        if (spsxTps != mdTps * 2)
            return false;
        const int16_t range = spsxTps - 1 < 23 ? spsxTps - 1 : 23;
        // MDX pattern -> MD track -> SPS-X.
        for (int mt = -127; mt <= 127; ++mt) {
            const int16_t sent = stepseq_microtiming_to_ticks((int8_t)mt, spsxTps);
            const int8_t md =
                spsx_microtiming_to_md_microtiming((int8_t)mt, mdTps);
            const int16_t played =
                2 * SeqTrack::microtiming_to_ticks(md, mdTps);
            const int16_t error = played - sent;
            // An odd tick falls between two MD track ticks: the nearer the
            // grid of the two.
            const bool exact = (sent & 1) == 0;
            if ((exact ? error != 0 : (error != (sent > 0 ? -1 : 1)))) {
                std::fprintf(stderr,
                             "microtiming spsx->md speed=%u mt=%d sent=%d "
                             "played=%d\n",
                             speed, mt, sent, played);
                return false;
            }
            const int8_t back = md_microtiming_to_spsx_microtiming(md, mdTps);
            if (stepseq_microtiming_to_ticks(back, spsxTps) != played) {
                std::fprintf(stderr,
                             "microtiming spsx->md->spsx speed=%u mt=%d\n",
                             speed, mt);
                return false;
            }
        }
        // MD track -> SPS-X track -> MD track.
        for (int mt = -128; mt <= 127; ++mt) {
            const int16_t md = SeqTrack::microtiming_to_ticks((int8_t)mt, mdTps);
            int16_t want = 2 * md;
            if (want > range) want = range;
            if (want < -range) want = -range;
            const int8_t converted =
                md_microtiming_to_spsx_microtiming((int8_t)mt, mdTps);
            const int16_t played =
                stepseq_microtiming_to_ticks(converted, spsxTps);
            if (played != want) {
                std::fprintf(stderr,
                             "microtiming md->spsx speed=%u mt=%d md=%d "
                             "played=%d want=%d\n",
                             speed, mt, md, played, want);
                return false;
            }
            const int8_t back =
                spsx_microtiming_to_md_microtiming(converted, mdTps);
            if (SeqTrack::microtiming_to_ticks(back, mdTps) != want / 2) {
                std::fprintf(stderr,
                             "microtiming md->spsx->md speed=%u mt=%d\n",
                             speed, mt);
                return false;
            }
        }
    }
    // Known points at 1x: 64 is 12 of 24 ticks, half a step's range.
    return stepseq_microtiming_to_ticks(64, 24) == 12 &&
           SeqTrack::microtiming_to_ticks(
               spsx_microtiming_to_md_microtiming(64, 12), 12) == 6 &&
           spsx_microtiming_to_md_microtiming(0, 12) == 0 &&
           md_microtiming_to_spsx_microtiming(0, 12) == 0;
}

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "--roundtrip-kit") == 0)
        return roundtripKitFile(argv[2], argv[3]);
    if (!checkUsrMetadata()) {
        std::fprintf(stderr, "MCL USR metadata/tuning checks failed\n");
        return 1;
    }
    const bool octaves=checkTonalOctaveMessages();
    const bool legacyOctaves=checkTonalLegacyKitExport();
    if (!checkLfoSeedMarker()) {
        std::fprintf(stderr, "MCL LFO seed marker checks failed\n");
        return 1;
    }
    if (!checkLfoRunStateRoundTrip()) {
        std::fprintf(stderr, "MCL LFO run state round trip failed\n");
        return 1;
    }
    if(!octaves || !legacyOctaves) {
        std::fprintf(stderr,"MCL tonal octave checks failed: messages=%d legacy-kit=%d\n",octaves,legacyOctaves);
        return 1;
    }
    const bool globalVersions = checkGlobalVersions();
    const bool machineRoutes = checkMachineRoutePackets();
    const bool kitV1 = checkKitWireCapability(
        0, SPSX_KIT_VERSION,
        mdKitWireSizeForVersion(SPSX_KIT_VERSION));
    const bool kitV2 = checkKitWireCapability(
        FW_CAP_SPSX_PARAMS_37, SPSX_KIT_VERSION,
        mdKitWireSizeForVersion(SPSX_KIT_VERSION));
    const bool patternStockFallback = checkPatternWireCapability(
        0, 0x03, false);
    const bool patternCompact = checkPatternWireCapability(
        FW_CAP_SPSX_PARAMS_37, MD_PATTERN_VERSION_SPSX, true);
    uint16_t maximumPatternBytes = 0;
    const bool maximumPattern =
        checkMaximumPatternRoundTrip(maximumPatternBytes);
    const bool malformedPattern =
        checkMalformedCanonicalPatternRejected();
    uint16_t envelopePatternBytes = 0;
    const bool envelopePattern =
        checkCanonicalPatternEnvelope(envelopePatternBytes);
    const bool trackRows = checkTrackRowMigrations();
    const bool extendedCc = checkNegotiatedExtendedCcMapping();
    const bool encoderPackets = checkEncoderInterfacePacketWidths();
    const bool seqNegotiation =
        checkSequencerNegotiation(MODEL_BUS3, 91) &&
        checkSequencerNegotiation(MODEL_ENVDEST, 2);
    if (!checkConditionModes()) {
        std::fprintf(stderr, "MCL condition mode checks failed\n");
        return 1;
    }
    if (!checkMicrotimingPopupCondition()) {
        std::fprintf(stderr, "MCL microtiming popup condition checks failed\n");
        return 1;
    }
    if (!checkMdxPatternTiming()) {
        std::fprintf(stderr, "MCL MDX pattern timing import checks failed\n");
        return 1;
    }
    if (!checkTriglessLockImport()) {
        std::fprintf(stderr, "MCL trigless lock import checks failed\n");
        return 1;
    }
    if (!checkMicrotimingConversion()) {
        std::fprintf(stderr, "MCL microtiming conversion checks failed\n");
        return 1;
    }
    if (!globalVersions || !machineRoutes || !kitV1 || !kitV2 || !patternStockFallback || !patternCompact ||
        !maximumPattern ||
        !malformedPattern || !envelopePattern || !trackRows ||
        !extendedCc || !encoderPackets || !seqNegotiation) {
        std::fprintf(stderr,
                     "MCL wire smoke failed: globals=%d machine-routes=%d kit65=%d kit65-caps=%d stock03=%d "
                     "compact04=%d max608=%d malformed=%d envelope=%d rows=%d cc=%d "
                     "encoder=%d seq=%d bytes=%u envelope-bytes=%u\n",
                     globalVersions, machineRoutes, kitV1, kitV2, patternStockFallback, patternCompact,
                     maximumPattern,
                     malformedPattern, envelopePattern, trackRows, extendedCc,
                     encoderPackets,
                     seqNegotiation, maximumPatternBytes,
                     envelopePatternBytes);
        return 1;
    }
    std::fprintf(stderr,
                 "MCL wire compatibility passed: tonal octaves, kit/pattern, 24/34/38 "
                 "packets, row migrations, v1/v2 negotiation, BUS3/DST bridge "
                 "round-trip, malformed-body rejection, and 608 rows "
                 "(%u-byte dense MKII fixture)\n",
                 maximumPatternBytes);
    return 0;
}
