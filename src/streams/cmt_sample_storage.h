#ifndef SD2CMT2_CMT_SAMPLE_STORAGE_H
#define SD2CMT2_CMT_SAMPLE_STORAGE_H

/*
    Legacy compatibility header.

    v0.19 moved both PLAY FIFO and work buffer into a union and this altered
    the previously working WAV PLAY path. v0.20 deliberately restores the
    original PLAY FIFO and keeps only cmt_work_buffer[512] as shared storage.
    This header intentionally owns no RAM.
*/

#endif
