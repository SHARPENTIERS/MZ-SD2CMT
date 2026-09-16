#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "../src/formats/mz_tape_decoder.h"

namespace
{

struct DecodeResult
{
    std::vector<std::vector<uint8_t>> records;
    std::vector<std::vector<uint8_t>> invalid_headers;
    std::vector<uint8_t> header;
    std::vector<uint8_t> payload;
    uint32_t expected_payload = 0U;
    unsigned int invalid_blocks = 0U;
};

[[noreturn]] void fail(const std::string &message)
{
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    std::exit(1);
}

std::vector<uint8_t> read_file(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) fail("cannot open " + path);
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    input.seekg(0, std::ios::beg);
    if (length < 0) fail("cannot determine length of " + path);
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    if (!bytes.empty())
        input.read(reinterpret_cast<char *>(bytes.data()), length);
    if (!input) fail("cannot read " + path);
    return bytes;
}

uint16_t read_le16(const uint8_t *source)
{
    return static_cast<uint16_t>(source[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(source[1]) << 8U);
}

uint32_t read_le32(const uint8_t *source)
{
    return static_cast<uint32_t>(source[0]) |
           (static_cast<uint32_t>(source[1]) << 8U) |
           (static_cast<uint32_t>(source[2]) << 16U) |
           (static_cast<uint32_t>(source[3]) << 24U);
}

bool normalize_ic_header(std::vector<uint8_t> &header)
{
    static constexpr uint8_t loader_prefix[8] =
        {0x3EU, 0x08U, 0xD3U, 0xCEU, 0xCDU, 0x3EU, 0x07U, 0x36U};
    if ((header.size() != MZ_TAPE_HEADER_BYTES) ||
        (header[0x18U] != 0x01U) ||
        ((header[0x19U] != 0x11U) &&
         (header[0x19U] != 0x16U) &&
         (header[0x19U] != 0x20U)) ||
        !std::equal(std::begin(loader_prefix), std::end(loader_prefix),
                    header.begin() + 0x20U))
        return false;

    header[0] = 0x01U;
    std::copy(header.begin() + 0x1AU, header.begin() + 0x20U,
              header.begin() + 0x12U);
    std::fill(header.begin() + 0x18U, header.end(), 0U);
    return true;
}

void begin_decode(DecodeResult &result)
{
    result = DecodeResult{};
    mz_tape_decoder_begin_header();
}

void take_events(DecodeResult &result)
{
    mz_tape_decoder_event_t event{};
    while (mz_tape_decoder_take_event(&event))
    {
        if (event.type == MZ_TAPE_DECODER_EVENT_HEADER_VALID)
        {
            const uint8_t *header = mz_tape_decoder_get_header();
            if (header == nullptr) fail("HEADER_VALID without header data");
            result.header.assign(header, header + MZ_TAPE_HEADER_BYTES);
            (void)normalize_ic_header(result.header);
            result.payload.clear();
            result.expected_payload = read_le16(result.header.data() + 0x12U);
            mz_tape_decoder_start_data(result.expected_payload);
        }
        else if (event.type == MZ_TAPE_DECODER_EVENT_DATA_BYTE)
        {
            if (event.byte_index != result.payload.size())
                fail("non-contiguous payload byte index");
            result.payload.push_back(event.value);
        }
        else if (event.type == MZ_TAPE_DECODER_EVENT_BLOCK_VALID)
        {
            if ((result.header.size() != MZ_TAPE_HEADER_BYTES) ||
                (result.payload.size() != result.expected_payload))
                fail("valid block has unexpected decoded length");
            std::vector<uint8_t> record = result.header;
            record.insert(record.end(), result.payload.begin(),
                          result.payload.end());
            result.records.push_back(std::move(record));
            result.header.clear();
            result.payload.clear();
            result.expected_payload = 0U;
            mz_tape_decoder_begin_header();
        }
        else if (event.type == MZ_TAPE_DECODER_EVENT_BLOCK_INVALID)
        {
            ++result.invalid_blocks;
            result.invalid_headers.push_back(result.header);
            result.header.clear();
            result.payload.clear();
            result.expected_payload = 0U;
            mz_tape_decoder_begin_header();
        }
    }
}

void feed_interval(DecodeResult &result, uint32_t duration, uint8_t level,
                   bool waveform_inverted, bool explicit_correction)
{
    const uint8_t transform = static_cast<uint8_t>(waveform_inverted) ^
                              static_cast<uint8_t>(explicit_correction);
    const uint16_t bounded = static_cast<uint16_t>(
        std::min<uint32_t>(duration, 0xFFFFU));
    (void)mz_tape_decoder_feed_interval(
        bounded, static_cast<uint8_t>((level ? 1U : 0U) ^ transform));
    take_events(result);
}

DecodeResult decode_l16(const std::string &path, bool waveform_inverted,
                        bool explicit_correction)
{
    const std::vector<uint8_t> bytes = read_file(path);
    DecodeResult result;
    bool have_interval = false;
    uint8_t level = 0U;
    uint32_t duration = 0U;
    begin_decode(result);

    for (uint8_t byte : bytes)
    {
        const int8_t slot = static_cast<int8_t>(byte);
        if (slot == 0)
        {
            if (!have_interval) fail("L16 starts with a zero extension");
            duration = std::min<uint32_t>(duration + 127U, 0xFFFFU);
            continue;
        }

        if (have_interval)
            feed_interval(result, duration, level,
                          waveform_inverted, explicit_correction);
        level = (slot > 0) ? 1U : 0U;
        duration = static_cast<uint32_t>(
            (slot < 0) ? -static_cast<int16_t>(slot) : slot);
        have_interval = true;
    }

    if (have_interval)
        feed_interval(result, duration, level,
                      waveform_inverted, explicit_correction);
    mz_tape_decoder_stop();
    return result;
}

struct WavData
{
    const uint8_t *samples = nullptr;
    size_t sample_count = 0U;
    uint32_t sample_rate = 0U;
};

WavData parse_wav(const std::vector<uint8_t> &bytes)
{
    if ((bytes.size() < 12U) ||
        (std::memcmp(bytes.data(), "RIFF", 4U) != 0) ||
        (std::memcmp(bytes.data() + 8U, "WAVE", 4U) != 0))
        fail("invalid WAV RIFF header");

    WavData wav;
    bool format_valid = false;
    size_t offset = 12U;
    while (offset + 8U <= bytes.size())
    {
        const uint8_t *chunk = bytes.data() + offset;
        const uint32_t size = read_le32(chunk + 4U);
        const size_t payload = offset + 8U;
        if ((size > bytes.size()) || (payload > bytes.size() - size))
            fail("invalid WAV chunk length");

        if ((std::memcmp(chunk, "fmt ", 4U) == 0) && (size >= 16U))
        {
            if ((read_le16(bytes.data() + payload) != 1U) ||
                (read_le16(bytes.data() + payload + 2U) != 1U) ||
                (read_le16(bytes.data() + payload + 14U) != 8U))
                fail("WAV is not 8-bit mono PCM");
            wav.sample_rate = read_le32(bytes.data() + payload + 4U);
            format_valid = true;
        }
        else if (std::memcmp(chunk, "data", 4U) == 0)
        {
            wav.samples = bytes.data() + payload;
            wav.sample_count = size;
        }

        offset = payload + size + (size & 1U);
    }

    if (!format_valid || (wav.samples == nullptr) || (wav.sample_rate == 0U))
        fail("WAV lacks a supported fmt/data chunk");
    return wav;
}

DecodeResult decode_wav(const std::string &path, bool waveform_inverted,
                        bool explicit_correction)
{
    const std::vector<uint8_t> bytes = read_file(path);
    const WavData wav = parse_wav(bytes);
    DecodeResult result;
    bool digital_level = false;
    bool have_run = false;
    uint8_t run_level = 0U;
    uint32_t run = 0U;
    begin_decode(result);

    for (size_t i = 0U; i < wav.sample_count; ++i)
    {
        const uint8_t sample = wav.samples[i];
        if (!digital_level)
        {
            if (sample >= 155U) digital_level = true;
        }
        else if (sample <= 100U)
        {
            digital_level = false;
        }

        const uint8_t level = digital_level ? 1U : 0U;
        if (!have_run)
        {
            have_run = true;
            run_level = level;
            run = 1U;
        }
        else if (level == run_level)
        {
            if (run != 0xFFFFFFFFU) ++run;
        }
        else
        {
            feed_interval(result, run, run_level,
                          waveform_inverted, explicit_correction);
            run_level = level;
            run = 1U;
        }
    }

    if (have_run)
        feed_interval(result, run, run_level,
                      waveform_inverted, explicit_correction);
    mz_tape_decoder_stop();
    return result;
}

std::vector<std::vector<uint8_t>> expected_records(const std::string &directory)
{
    std::vector<std::vector<uint8_t>> expected;
    for (unsigned int index = 1U; index <= 6U; ++index)
    {
        const std::string path = directory + "/Barbar" +
                                 std::to_string(index) + ".mzf";
        std::vector<uint8_t> bytes = read_file(path);
        if (bytes.size() < MZ_TAPE_HEADER_BYTES)
            fail("short expected MZF: " + path);
        const uint16_t payload = read_le16(bytes.data() + 0x12U);
        const size_t expected_size = MZ_TAPE_HEADER_BYTES + payload;
        if (bytes.size() < expected_size)
            fail("truncated expected MZF: " + path);
        bytes.resize(expected_size);
        expected.push_back(std::move(bytes));
    }
    return expected;
}

bool exact_match(const DecodeResult &actual,
                 const std::vector<std::vector<uint8_t>> &expected)
{
    if (actual.records.size() != expected.size()) return false;
    for (size_t i = 0U; i < expected.size(); ++i)
    {
        if ((actual.records[i].size() != expected[i].size()) ||
            !std::equal(actual.records[i].begin() + MZ_TAPE_HEADER_BYTES,
                        actual.records[i].end(),
                        expected[i].begin() + MZ_TAPE_HEADER_BYTES))
            return false;
    }
    return true;
}

void report(const char *format, const char *mode, const DecodeResult &result,
            const std::vector<std::vector<uint8_t>> &expected, bool exact)
{
    std::printf("%-4s %-19s records=%zu invalid=%u payload-exact=%s\n",
                format, mode, result.records.size(), result.invalid_blocks,
                exact ? "yes" : "no");
    if (!exact)
    {
        for (size_t i = 0U; i < result.records.size(); ++i)
        {
            size_t matched = expected.size();
            for (size_t j = 0U; j < expected.size(); ++j)
            {
                if (result.records[i] == expected[j])
                {
                    matched = j;
                    break;
                }
            }
            std::printf("     decoded[%zu] bytes=%zu expected=%s\n", i,
                        result.records[i].size(),
                        (matched < expected.size()) ?
                            std::to_string(matched + 1U).c_str() : "none");
        }
        for (size_t i = 0U; i < result.invalid_headers.size(); ++i)
        {
            const auto &header = result.invalid_headers[i];
            std::printf("     invalid[%zu] header-bytes=%zu declared=%u\n", i,
                        header.size(),
                        (header.size() >= 20U) ? read_le16(header.data() + 18U) : 0U);
        }
    }
}

template<typename Decoder>
void test_format(const char *format, Decoder decoder,
                 const std::vector<std::vector<uint8_t>> &expected)
{
    const DecodeResult correct = decoder(false, false);
    const bool correct_exact = exact_match(correct, expected);
    report(format, "correct", correct, expected, correct_exact);
    if (!correct_exact) fail(std::string(format) + " correct decode differs");

    const DecodeResult inverted = decoder(true, false);
    const bool inverted_exact = exact_match(inverted, expected);
    report(format, "inverted", inverted, expected, inverted_exact);
    if (inverted_exact)
        fail(std::string(format) + " inverted waveform was accepted exactly");

    const DecodeResult corrected = decoder(true, true);
    const bool corrected_exact = exact_match(corrected, expected);
    report(format, "inverted+explicit", corrected, expected, corrected_exact);
    if (!corrected_exact)
        fail(std::string(format) + " explicit correction did not recover");
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::fprintf(stderr,
            "usage: mz_tape_decoder_file_test <Barbarian2.l16> "
            "<Barbarian2.wav> <expected-mzf-directory>\n");
        return 2;
    }

    const std::string l16_path = argv[1];
    const std::string wav_path = argv[2];
    const auto expected = expected_records(argv[3]);

    test_format("L16",
        [&](bool inverted, bool correction) {
            return decode_l16(l16_path, inverted, correction);
        }, expected);
    test_format("WAV",
        [&](bool inverted, bool correction) {
            return decode_wav(wav_path, inverted, correction);
        }, expected);

    std::printf("PASS: Barbarian2 L16/WAV production-decoder polarity test\n");
    return 0;
}
