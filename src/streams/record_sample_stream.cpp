#include "record_sample_stream.h"

#include <stddef.h>
#include <util/atomic.h>

static volatile uint16_t record_read_byte_sequence = 0U;
static volatile uint16_t record_write_byte_sequence = 0U;

void record_sample_stream_reset(void)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        record_read_byte_sequence = 0U;
        record_write_byte_sequence = 0U;
    }
}

bool record_sample_stream_push_byte_from_isr(uint8_t packed_byte)
{
    uint16_t write_local = record_write_byte_sequence;
    uint16_t read_local = record_read_byte_sequence;

    if ((uint16_t)(write_local - read_local) >=
        RECORD_SAMPLE_STREAM_CAPACITY_BYTES)
    {
        return false;
    }

    record_sample_stream_bytes[
        write_local & RECORD_SAMPLE_STREAM_BYTE_MASK
    ] = packed_byte;

    asm volatile("" ::: "memory");
    record_write_byte_sequence = (uint16_t)(write_local + 1U);
    return true;
}

uint16_t record_sample_stream_pop_bytes(uint8_t *destination,
                                        uint16_t max_bytes)
{
    uint16_t read_local;
    uint16_t write_snapshot;
    uint16_t available;
    uint16_t count;

    if ((destination == NULL) || (max_bytes == 0U))
    {
        return 0U;
    }

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        read_local = record_read_byte_sequence;
        write_snapshot = record_write_byte_sequence;
    }

    available = (uint16_t)(write_snapshot - read_local);

    if (available > RECORD_SAMPLE_STREAM_CAPACITY_BYTES)
    {
        return 0U;
    }

    count = available;
    if (count > max_bytes)
    {
        count = max_bytes;
    }

    for (uint16_t i = 0U; i < count; i++)
    {
        destination[i] = record_sample_stream_bytes[
            (uint16_t)(read_local + i) & RECORD_SAMPLE_STREAM_BYTE_MASK
        ];
    }

    if (count != 0U)
    {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
        {
            record_read_byte_sequence = (uint16_t)(read_local + count);
        }
    }

    return count;
}

uint16_t record_sample_stream_available_bytes(void)
{
    uint16_t read_snapshot;
    uint16_t write_snapshot;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        read_snapshot = record_read_byte_sequence;
        write_snapshot = record_write_byte_sequence;
    }

    return (uint16_t)(write_snapshot - read_snapshot);
}

uint8_t record_sample_stream_fill_percent(void)
{
    uint32_t percent =
        ((uint32_t)record_sample_stream_available_bytes() * 100UL) /
        RECORD_SAMPLE_STREAM_CAPACITY_BYTES;

    if (percent > 100UL)
    {
        percent = 100UL;
    }

    return (uint8_t)percent;
}
