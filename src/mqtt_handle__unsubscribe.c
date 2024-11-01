#include "mqtt_conn.h"

#include <internal/ts_log.h>

static int tm_mqtt_conn__send_unsuback(ts_t* server, ts_conn_t* c, int pkt_id) {
  tm_mqtt_conn_t* conn = (tm_mqtt_conn_t*) ts_server__get_conn_user_data(server, c);
  char unsuback[4];
  unsuback[0] = (char)0xB0;
  unsuback[1] = (char)(2);
  uint162bytes_be(pkt_id, unsuback+2);
  LOG_DUMP(unsuback, 4, "[%s][%s] Send [UNSUBACK]", ts_server__get_conn_remote_host(server, c), conn->session->client_id);
  return tm_mqtt_conn__send_packet(server, c, unsuback, 4, pkt_id, NULL);
}

int tm_mqtt_conn__process_unsubscribe(ts_t* server, ts_conn_t* c, const char* pkt_bytes, int pkt_bytes_len, int variable_header_off) {
  int err;
  tm_mqtt_conn_t* conn;
  tm_server_t* s;
  tm_packet_decoder_t* decoder;
  const char* tmp_ptr = "";
  int tmp_val, tmp_len, pkt_id;
  char topic[65536];
  ts_error_t errt;
  const char* conn_id;
  
  conn = (tm_mqtt_conn_t*) ts_server__get_conn_user_data(server, c);
  conn_id = ts_server__get_conn_remote_host(server, c);
  s = conn->server;
  decoder = &conn->decoder;
  
  LOG_DUMP(pkt_bytes, pkt_bytes_len, "[%s][%s] Receive [UNSUBSCRIBE]", conn_id, conn->session->client_id);
  
  ts_error__init(&errt);
  tm_packet_decoder__set(decoder, pkt_bytes + variable_header_off, pkt_bytes_len - variable_header_off);
  
  err = tm_packet_decoder__read_int16(decoder, &pkt_id);
  if (err || pkt_id <= 0) {
    LOG_ERROR("[%s][%s] Invalid Packet Identifier", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  
  if (tm_packet_decoder__available(decoder) == 0) {
    LOG_ERROR("[%s][%s] No Topic filter in the UNSUBSCRIBE packet", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  
  while (tm_packet_decoder__available(decoder) > 0) {
    err = tm_packet_decoder__read_int16_string(decoder, &tmp_len, &tmp_ptr);
    if (err) {
      LOG_ERROR("[%s][%s] Invalid Topic Filter", conn_id, conn->session->client_id);
      tm_mqtt_conn__abort(server, c);
      goto done;
    }
    err = tm_topics__valid_topic_filter(tmp_ptr, tmp_len, &errt);
    if (err) {
      LOG_ERROR("[%s][%s] Invalid Topic Filter: %s", conn_id, conn->session->client_id, errt.msg);
      tm_mqtt_conn__abort(server, c);
      goto done;
    }
    
    memcpy(topic, tmp_ptr, tmp_len);
    topic[tmp_len] = 0;
    
    tm__internal_unsubscribe_cb(s, c, topic);
    LOG_VERB("[%s][%s] Unsubscribe: '%s'", conn_id, conn->session->client_id, topic);
    
    err = tm__on_unsubscription(s, c, topic);
    if (err == TS_ERR_NOT_FOUND) {
      // permit it
      LOG_VERB("[%s][%s] Try to unsubscribe a topic that is not subscribed: %s", conn_id, conn->session->client_id, topic);
    } else if (err) {
      tm_mqtt_conn__abort(s, c);
      goto done;
    }
    
  }
  
  err = tm_mqtt_conn__send_unsuback(server, c, pkt_id);
  if (err) {
    LOG_ERROR("[%s][%s] Fail to send UNSUBACK", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  
done:
  return 0;
}