
#include "mqtt_conn.h"

#include <internal/ts_log.h>

int tm_mqtt_conn__process_disconnect(ts_t* server, ts_conn_t* c, const char* pkt_bytes, int pkt_bytes_len) {
  tm_mqtt_conn_t* conn;
  tm_server_t* s;
  const char* conn_id;
  
  conn = (tm_mqtt_conn_t*) ts_server__get_conn_user_data(server, c);
  s = conn->server;
  conn_id = ts_server__get_conn_remote_host(server, c);
  
  LOG_DUMP(pkt_bytes, pkt_bytes_len, "[%s][%s] Receive [DISCONNECT]", ts_server__get_conn_remote_host(server, c), conn->session->client_id);
  
  if (conn->will) {
    LOG_VERB("[%s][%s] Client is disconnected gracefully, discard the Will message silently", conn_id, conn->session->client_id);
    tm__remove_message(s, conn->will);
    conn->will = NULL;
  }
  
  return tm_mqtt_conn__process_tcp_disconnect(server, c);
}

int tm_mqtt_conn__process_tcp_disconnect(ts_t* server, ts_conn_t* c) {
  tm_mqtt_conn_t* conn;
  tm_server_t* s;
  const char* conn_id;
  
  conn = (tm_mqtt_conn_t*) ts_server__get_conn_user_data(server, c);
  if (conn == NULL || conn->session == NULL) {
    // We're at the very beginning state that mqtt_conn is not created or the session is not initialized
    return 0;
  }
  s = conn->server;
  conn_id = ts_server__get_conn_remote_host(server, c);
  
  LOG_DEBUG("[%s][%s] Disconnected", conn_id, conn->session->client_id);
  
  tm_mqtt_conn__abort(server, c);
  
  tm_mqtt_session__detach(conn->session);
  
  if (conn->will) {
    LOG_VERB("[%s][%s] Client is disconnected abnormally, publish the Will message silently", conn_id, conn->session->client_id);
    tm__on_publish_received(conn->server, c, conn->will);
    tm__remove_message(s, conn->will);
    conn->will = NULL;
  }
  
  if (conn->session->clean_session) {
    LOG_VERB("[%s][%s] Clean session is set, discard the session state", conn_id, conn->session->client_id);
    tm__remove_session(s, conn->session);
    conn->session = NULL;
  }
  
  tm__internal_disconnected_cb(s, c);
  return 0;
}