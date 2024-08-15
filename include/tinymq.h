
#ifndef TINYMQ_TINYMQ_H
#define TINYMQ_TINYMQ_H


#ifdef __cplusplus
extern "C" {
#endif

#define TM_EXTERN /* nothing */


typedef struct tm_s tm_t;
typedef struct tm_callbacks_s tm_callbacks_t;

typedef int (*tm_log_cb)(void* ctx, const char* msg);

struct tm_s {
  void* internal;
};

struct tm_callbacks_s {
    void* cb_ctx;
    tm_log_cb log_cb;
};

TM_EXTERN int tm__init(tm_t* mq);
TM_EXTERN int tm_destroy(tm_t* mq);

TM_EXTERN int tm__set_listener_count(tm_t* mq, int cnt);
TM_EXTERN int tm__set_listener_host_port(tm_t* mq, int idx, const char* host, int port);
TM_EXTERN int tm__set_listener_use_ipv6(tm_t* mq, int idx, int use);
TM_EXTERN int tm__set_listener_protocol(tm_t* mq, int idx, int proto);
TM_EXTERN int tm__set_listener_certs(tm_t* mq, int idx, const char* cert, const char* key);

TM_EXTERN int tm__set_log_level(tm_t* mq, int log_level);
TM_EXTERN int tm__set_log_dest(tm_t* mq, int dest);
TM_EXTERN int tm__set_log_dir(tm_t* mq, const char* dir);

TM_EXTERN int tm__set_callbacks(tm_t* mq, tm_callbacks_t* cbs);

TM_EXTERN int tm__start(tm_t* mq);
TM_EXTERN int tm__run(tm_t* mq);
TM_EXTERN int tm__stop(tm_t* mq);

#ifdef __cplusplus
}
#endif


#endif //TINYMQ_TINYMQ_H
