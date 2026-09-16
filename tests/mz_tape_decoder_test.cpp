#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../src/formats/mz_tape_decoder.h"

namespace
{

struct Timing
{
    const char *name;
    uint16_t short_high;
    uint16_t short_low;
    uint16_t long_high;
    uint16_t long_low;
};

struct EventLog
{
    std::vector<mz_tape_decoder_event_t> events;
};

[[noreturn]] void fail(const char *message, const char *profile)
{
    std::fprintf(stderr, "FAIL [%s]: %s\n", profile, message);
    std::exit(1);
}

uint8_t popcount8(uint8_t value)
{
    uint8_t count = 0U;
    while (value != 0U)
    {
        count = static_cast<uint8_t>(count + (value & 1U));
        value >>= 1U;
    }
    return count;
}

uint16_t checksum(const uint8_t *bytes, size_t count)
{
    uint16_t value = 0U;
    for (size_t i = 0U; i < count; ++i) value += popcount8(bytes[i]);
    return value;
}

void drain_events(EventLog &log)
{
    mz_tape_decoder_event_t event{};
    while (mz_tape_decoder_take_event(&event)) log.events.push_back(event);
}

void feed_interval(uint16_t duration, uint8_t physical_level, EventLog &log)
{
    (void)mz_tape_decoder_feed_interval(duration, physical_level);
    drain_events(log);
}

void feed_pulse(const Timing &timing, bool is_long, EventLog &log,
                bool waveform_inverted = false,
                bool explicit_correction = false)
{
    const uint16_t logical_high =
        is_long ? timing.long_high : timing.short_high;
    const uint16_t logical_low =
        is_long ? timing.long_low : timing.short_low;
    const uint8_t transform = static_cast<uint8_t>(waveform_inverted) ^
                              static_cast<uint8_t>(explicit_correction);

    /* External hardware mapping: logical HIGH owns physical LOW duration. */
    feed_interval(logical_high, static_cast<uint8_t>(0U ^ transform), log);
    feed_interval(logical_low, static_cast<uint8_t>(1U ^ transform), log);
}

void feed_byte(const Timing &timing, uint8_t value, EventLog &log,
               bool waveform_inverted = false,
               bool explicit_correction = false)
{
    for (uint8_t mask = 0x80U; mask != 0U; mask >>= 1U)
    {
        feed_pulse(timing, (value & mask) != 0U, log,
                   waveform_inverted, explicit_correction);
    }
    feed_pulse(timing, true, log, waveform_inverted, explicit_correction);
}

void feed_bytes_and_checksum(const Timing &timing, const uint8_t *bytes,
                             size_t count, EventLog &log,
                             bool corrupt_checksum = false,
                             bool waveform_inverted = false,
                             bool explicit_correction = false)
{
    for (size_t i = 0U; i < count; ++i)
    {
        feed_byte(timing, bytes[i], log,
                  waveform_inverted, explicit_correction);
    }

    uint16_t recorded = checksum(bytes, count);
    if (corrupt_checksum) recorded ^= 1U;
    feed_byte(timing, static_cast<uint8_t>(recorded >> 8U), log,
              waveform_inverted, explicit_correction);
    feed_byte(timing, static_cast<uint8_t>(recorded), log,
              waveform_inverted, explicit_correction);
}

void feed_leader_and_mark(const Timing &timing, EventLog &log,
                          bool waveform_inverted = false,
                          bool explicit_correction = false)
{
    for (uint8_t i = 0U; i < 32U; ++i)
        feed_pulse(timing, false, log,
                   waveform_inverted, explicit_correction);
    for (uint8_t i = 0U; i < 16U; ++i)
        feed_pulse(timing, true, log,
                   waveform_inverted, explicit_correction);
    for (uint8_t i = 0U; i < 16U; ++i)
        feed_pulse(timing, false, log,
                   waveform_inverted, explicit_correction);
    for (uint8_t i = 0U; i < 2U; ++i)
        feed_pulse(timing, true, log,
                   waveform_inverted, explicit_correction);
}

void feed_framed_block(const Timing &timing, const uint8_t *bytes,
                       size_t count, EventLog &log,
                       bool corrupt_checksum = false,
                       bool waveform_inverted = false,
                       bool explicit_correction = false)
{
    feed_leader_and_mark(timing, log,
                         waveform_inverted, explicit_correction);
    feed_bytes_and_checksum(timing, bytes, count, log, corrupt_checksum,
                            waveform_inverted, explicit_correction);
}

std::array<uint8_t, MZ_TAPE_HEADER_BYTES> make_header(void)
{
    std::array<uint8_t, MZ_TAPE_HEADER_BYTES> header{};
    for (size_t i = 0U; i < header.size(); ++i)
        header[i] = static_cast<uint8_t>((i * 37U + 11U) & 0xFFU);
    return header;
}

size_t count_events(const EventLog &log, mz_tape_decoder_event_type_t type)
{
    size_t count = 0U;
    for (const auto &event : log.events)
        if (event.type == type) ++count;
    return count;
}

const mz_tape_decoder_event_t *last_event(
    const EventLog &log, mz_tape_decoder_event_type_t type)
{
    for (auto it = log.events.rbegin(); it != log.events.rend(); ++it)
        if (it->type == type) return &*it;
    return nullptr;
}

void test_profile_header(const Timing &timing)
{
    const auto header = make_header();
    EventLog log;

    mz_tape_decoder_begin_header();
    feed_framed_block(timing, header.data(), header.size(), log);

    if (count_events(log, MZ_TAPE_DECODER_EVENT_HEADER_VALID) != 1U)
        fail("valid header was not decoded exactly once", timing.name);
    if ((mz_tape_decoder_get_header() == nullptr) ||
        (std::memcmp(mz_tape_decoder_get_header(), header.data(),
                     header.size()) != 0))
        fail("decoded header differs", timing.name);
    if (mz_tape_decoder_get_header_short_high_x8() !=
        static_cast<uint16_t>(timing.short_high * 8U))
        fail("logical-HIGH leader reference differs", timing.name);
}

void test_data_and_recovery(const Timing &timing)
{
    const auto header = make_header();
    const std::array<uint8_t, 3U> payload{{0xA5U, 0x00U, 0x7EU}};
    EventLog log;

    mz_tape_decoder_begin_header();
    feed_framed_block(timing, header.data(), header.size(), log);
    mz_tape_decoder_start_data(payload.size());
    feed_framed_block(timing, payload.data(), payload.size(), log, true);

    if (count_events(log, MZ_TAPE_DECODER_EVENT_BLOCK_INVALID) != 1U)
        fail("invalid data checksum was not reported", timing.name);

    mz_tape_decoder_start_recovery_data(payload.size());
    for (uint16_t i = 0U; i < 256U; ++i)
        feed_pulse(timing, false, log);
    feed_bytes_and_checksum(timing, payload.data(), payload.size(), log);

    const auto *valid = last_event(log, MZ_TAPE_DECODER_EVENT_BLOCK_VALID);
    if ((valid == nullptr) || (valid->copy_index != 1U))
        fail("duplicate recovery did not validate copy 1", timing.name);
    if (count_events(log, MZ_TAPE_DECODER_EVENT_DATA_BYTE) !=
        payload.size() * 2U)
        fail("data byte event count differs", timing.name);
}

void test_duplicate_header(const Timing &timing)
{
    const auto header = make_header();
    EventLog log;

    mz_tape_decoder_begin_header();
    feed_framed_block(timing, header.data(), header.size(), log, true);
    if (count_events(log, MZ_TAPE_DECODER_EVENT_HEADER_VALID) != 0U)
        fail("bad first header checksum was accepted", timing.name);

    for (uint16_t i = 0U; i < 256U; ++i)
        feed_pulse(timing, false, log);
    feed_bytes_and_checksum(timing, header.data(), header.size(), log);

    const auto *valid = last_event(log, MZ_TAPE_DECODER_EVENT_HEADER_VALID);
    if ((valid == nullptr) || (valid->copy_index != 1U))
        fail("duplicate header copy was not recovered", timing.name);
}

void test_explicit_polarity(void)
{
    /* The opposite half is deliberately constant. A full-period or dual-level
       decoder can hide the inversion; the fixed physical-LOW decoder cannot. */
    const Timing asymmetric{"asymmetric polarity", 5U, 12U, 10U, 12U};
    const auto header = make_header();

    EventLog correct;
    mz_tape_decoder_begin_header();
    feed_framed_block(asymmetric, header.data(), header.size(), correct);
    if (count_events(correct, MZ_TAPE_DECODER_EVENT_HEADER_VALID) != 1U)
        fail("correct physical polarity failed", asymmetric.name);

    EventLog inverted;
    mz_tape_decoder_begin_header();
    feed_framed_block(asymmetric, header.data(), header.size(), inverted,
                      false, true, false);
    if (count_events(inverted, MZ_TAPE_DECODER_EVENT_HEADER_VALID) != 0U)
        fail("inverted waveform was silently accepted", asymmetric.name);

    EventLog corrected;
    mz_tape_decoder_begin_header();
    feed_framed_block(asymmetric, header.data(), header.size(), corrected,
                      false, true, true);
    if (count_events(corrected, MZ_TAPE_DECODER_EVENT_HEADER_VALID) != 1U)
        fail("explicit polarity correction did not recover", asymmetric.name);
}

} // namespace

int main()
{
    /* L16-quantized physical intervals derived from production profiles. */
    const std::array<Timing, 10U> profiles{{
        {"NORMAL MZ800 1:1", 15U, 16U, 29U, 30U},
        {"NORMAL MZ700 1:1", 15U, 17U, 29U, 31U},
        {"NORMAL 1:2", 7U, 9U, 15U, 16U},
        {"NORMAL 1:3", 5U, 8U, 11U, 14U},
        {"NORMAL 1:4", 5U, 7U, 10U, 11U},
        {"IC 1:4", 5U, 7U, 10U, 11U},
        {"IC 1:3", 5U, 8U, 11U, 14U},
        {"IC 1:2", 7U, 9U, 15U, 16U},
        {"TC 1:3", 7U, 7U, 13U, 13U},
        {"TC 1:2", 9U, 9U, 18U, 18U},
    }};

    for (const auto &profile : profiles) test_profile_header(profile);
    test_data_and_recovery(profiles[0]);
    test_duplicate_header(profiles[1]);
    test_explicit_polarity();

    std::printf("PASS: %zu profiles, data checksum, duplicate/recovery, "
                "and explicit polarity\n", profiles.size());
    return 0;
}
