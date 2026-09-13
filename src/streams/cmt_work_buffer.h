#ifndef SD2CMT2_CMT_WORK_BUFFER_H
#define SD2CMT2_CMT_WORK_BUFFER_H

/*
    Compatibility header retained for existing project layout.
    v0.21 stores the sole 512-byte foreground work block privately in
    wav_sample_stream.cpp and RECORD obtains it through
    wav_sample_stream_get_shared_work_buffer(). No RAM is allocated here.
*/
#define CMT_WORK_BUFFER_BYTES 512U

#endif
