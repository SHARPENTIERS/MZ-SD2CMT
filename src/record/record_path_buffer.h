#ifndef SD2CMT2_RECORD_PATH_BUFFER_H
#define SD2CMT2_RECORD_PATH_BUFFER_H

#define CMT_SESSION_PATH_BUFFER_MAX 160U
#define RECORD_PATH_BUFFER_MAX CMT_SESSION_PATH_BUFFER_MAX

/* PLAY and RECORD sessions are mutually exclusive. One full-path buffer is
   sufficient for every transport. */
extern char cmt_session_path_buffer[CMT_SESSION_PATH_BUFFER_MAX];
#define record_path_buffer cmt_session_path_buffer

const char *record_path_filename(void);

#endif
