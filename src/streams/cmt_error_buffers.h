#ifndef SD2CMT2_CMT_ERROR_BUFFERS_H
#define SD2CMT2_CMT_ERROR_BUFFERS_H

#define CMT_ERROR_TEXT_BYTES 17U

/* Only one PLAY backend and one RECORD backend can be active at a time. */
extern char cmt_playback_backend_error[CMT_ERROR_TEXT_BYTES];
extern char cmt_record_backend_error[CMT_ERROR_TEXT_BYTES];

#endif
