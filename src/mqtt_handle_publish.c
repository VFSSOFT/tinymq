#include "mqtt_conn.h"
#include "mqtt_utils.h"

#include <internal/ts_log.h>
#include <internal/ts_miscellany.h>

static int tm_mqtt_conn__send_puback(ts_t* server, ts_conn_t* c, int pkt_id, tm_mqtt_msg_t* msg) {
  tm_mqtt_conn_t* conn = (tm_mqtt_conn_t*) ts_server__get_conn_user_data(server, c);
  char puback[4] = { 0x40, 0x02, 0x00, 0x00 };
  uint162bytes_be(pkt_id, puback+2);
  LOG_DUMP(puback, 4, "[%s][%s] Send [PUBACK]", ts_server__get_conn_remote_host(server, c), conn->session->client_id);
  return tm_mqtt_conn__send_packet(server, c, puback, 4, pkt_id, msg);
}
static int tm_mqtt_conn__send_pubrec(ts_t* server, ts_conn_t* c, int pkt_id, tm_mqtt_msg_t* msg) {
  tm_mqtt_conn_t* conn = (tm_mqtt_conn_t*) ts_server__get_conn_user_data(server, c);
  char pubrec[4] = { 0x52, 0x02, 0x00, 0x00 };
  uint162bytes_be(pkt_id, pubrec+2);
  LOG_DUMP(pubrec, 4, "[%s][%s] Send [PUBREC]", ts_server__get_conn_remote_host(server, c), conn->session->client_id);
  return tm_mqtt_conn__send_packet(server, c, pubrec, 4, pkt_id, msg);
}
static int tm_mqtt_conn__send_pubcomp(ts_t* server, ts_conn_t* c, int pkt_id, tm_mqtt_msg_t* msg) {
  tm_mqtt_conn_t* conn = (tm_mqtt_conn_t*) ts_server__get_conn_user_data(server, c);
  char pubcomp[4] = { 0x70, 0x02, 0x00, 0x00 };
  uint162bytes_be(pkt_id, pubcomp+2);
  LOG_DUMP(pubcomp, 4, "[%s][%s] Send [PUBCOMP]", ts_server__get_conn_remote_host(server, c), conn->session->client_id);
  return tm_mqtt_conn__send_packet(server, c, pubcomp, 4, pkt_id, msg);
}

int tm_mqtt_conn__process_publish(ts_t* server, ts_conn_t* c, const char* pkt_bytes, int pkt_bytes_len, int variable_header_off) {
  int err;
  tm_mqtt_conn_t* conn;
  tm_server_t* s;
  const char* conn_id;
  tm_packet_decoder_t* decoder;
  const char* tmp_ptr = "";
  int tmp_len, pkt_id = 0;
  char topic[65536];
  char first_byte = pkt_bytes[0];
  int dup = (pkt_bytes[0] & 0x08) == 0x08;
  int qos = (pkt_bytes[0] & 0x06) >> 1;
  tm_mqtt_msg_t* msg;

  conn = (tm_mqtt_conn_t*) ts_server__get_conn_user_data(server, c);
  conn_id = ts_server__get_conn_remote_host(server, c);
  s = conn->server;
  decoder = &conn->decoder;
  
  LOG_DUMP(pkt_bytes, pkt_bytes_len, "[%s][%s] Receive [PUBLISH]", conn_id, conn->session->client_id);
  
  tm_packet_decoder__set(decoder, pkt_bytes + variable_header_off, pkt_bytes_len - variable_header_off);

  if (!tm__is_valid_qos(qos)) {
    LOG_ERROR("[%s][%s] Invalid QoS", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  if (qos == 0 && dup) {
    LOG_ERROR("[%s][%s] Invalid DUP flag with the QoS is 0", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }

  err = tm_packet_decoder__read_int16_string(decoder, &tmp_len, &tmp_ptr);
  if (err || tmp_len == 0) {
    LOG_ERROR("[%s][%s] Invalid Topic Name", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  memcpy(topic, tmp_ptr, tmp_len);
  topic[tmp_len] = 0;

  if (qos != 0) {
    err = tm_packet_decoder__read_int16(decoder, &pkt_id);
    if (err || pkt_id <= 0) {
      LOG_ERROR("[%s][%s] Invalid Packet Id", conn_id, conn->session->client_id);
      tm_mqtt_conn__abort(server, c);
      goto done;
    }
  }

  msg = tm__create_message(
      s,
      topic,
      tm_packet_decoder__ptr(decoder), tm_packet_decoder__available(decoder),
      dup, qos, (first_byte & 0x01) == 0x01
  );
  if (msg == NULL) {
    LOG_ERROR("[%s][%s] Out of memory", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  msg->pkt_id = pkt_id;
  
  LOG_VERB(
      "[%s][%s] Received a message: MID=%" PRIu64 ", Topic='%s', Qos=%d, Retain=%d",
      conn_id,
      conn->session->client_id,
      msg->id,
      topic,
      qos,
      dup
  );

  tm_mqtt_session__add_in_msg(conn->session, msg);
  tm_mqtt_msg__set_state(msg, MSG_STATE_RECEIVE_PUB);
  
  err = tm_mqtt_conn__update_msg_state(server, c, msg);
  if (err) {
    tm_mqtt_conn__abort(server, c);
    goto done;
  }

  if (qos == 0){
    // nothing
  } else if (qos == 1) {
    err = tm_mqtt_conn__send_puback(server, c, pkt_id, msg);
    if (err) {
      LOG_ERROR("[%s][%s] Failed to send PUBACK", conn_id, conn->session->client_id);
      tm_mqtt_conn__abort(server, c);
      goto done;
    }
  } else if (qos == 2) {
    err = tm_mqtt_conn__send_pubrec(server, c, pkt_id, msg);
    if (err) {
      LOG_ERROR("[%s][%s] Failed to send PUBREC", conn_id, conn->session->client_id);
      tm_mqtt_conn__abort(server, c);
      goto done;
    }
  }
done:
  return 0;
}

int tm_mqtt_conn__process_puback(ts_t* server, ts_conn_t* c, const char* pkt_bytes, int pkt_bytes_len, int variable_header_off) {
  int err;
  tm_mqtt_conn_t* conn;
  tm_server_t* s;
  tm_packet_decoder_t* decoder;
  int pkt_id;
  tm_mqtt_msg_t* msg;
  const char* conn_id;
  
  conn = (tm_mqtt_conn_t*) ts_server__get_conn_user_data(server, c);
  conn_id = ts_server__get_conn_remote_host(server, c);
  s = conn->server;
  decoder = &conn->decoder;
  
  LOG_DUMP(pkt_bytes, pkt_bytes_len, "[%s][%s] Receive [PUBACK]", conn_id, conn->session->client_id);
  
  tm_packet_decoder__set(decoder, pkt_bytes + variable_header_off, pkt_bytes_len - variable_header_off);
  
  err = tm_packet_decoder__read_int16(decoder, &pkt_id);
  if (err || pkt_id <= 0) {
    LOG_ERROR("[%s][%s] Invalid Packet id", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  
  msg = tm_mqtt_session__find_out_msg(conn->session, pkt_id);
  if (msg == NULL) {
    LOG_ERROR("[%s][%s] Invalid Packet id", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  if (tm_mqtt_msg__qos(msg) != 1 || tm_mqtt_msg__get_state(msg) != MSG_STATE_WAIT_PUBACK) {
    LOG_ERROR("[%s][%s] Invalid Message state", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  
  tm_mqtt_conn__update_msg_state(server, c, msg);
  
done:
  return 0;
}
int tm_mqtt_conn__process_pubrec(ts_t* server, ts_conn_t* c, const char* pkt_bytes, int pkt_bytes_len, int variable_header_off) {
  int err;
  tm_mqtt_conn_t* conn;
  tm_server_t* s;
  tm_packet_decoder_t* decoder;
  int pkt_id;
  tm_mqtt_msg_t* msg;
  const char* conn_id;
  
  conn = (tm_mqtt_conn_t*) ts_server__get_conn_user_data(server, c);
  conn_id = ts_server__get_conn_remote_host(server, c);
  s = conn->server;
  decoder = &conn->decoder;
  
  LOG_DUMP(pkt_bytes, pkt_bytes_len, "[%s][%s] Receive [PUBREC]", conn_id, conn->session->client_id);
  
  tm_packet_decoder__set(decoder, pkt_bytes + variable_header_off, pkt_bytes_len - variable_header_off);
  
  err = tm_packet_decoder__read_int16(decoder, &pkt_id);
  if (err || pkt_id <= 0) {
    LOG_ERROR("[%s][%s] Invalid Packet id", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  
  msg = tm_mqtt_session__find_out_msg(conn->session, pkt_id);
  if (msg == NULL) {
    LOG_ERROR("[%s][%s] Invalid Packet id", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  if (tm_mqtt_msg__qos(msg) != 2 || tm_mqtt_msg__get_state(msg) != MSG_STATE_WAIT_PUBREC) {
    LOG_ERROR("[%s][%s] Invalid Message state", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  
  tm_mqtt_conn__update_msg_state(server, c, msg);
  err = tm_mqtt_conn__send_pubrel(server, c, pkt_id, msg);
  if (err) {
    LOG_ERROR("[%s][%s] Failed to send PUBREL", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  
done:
  return 0;
}
int tm_mqtt_conn__process_pubrel(ts_t* server, ts_conn_t* c, const char* pkt_bytes, int pkt_bytes_len, int variable_header_off) {
  int err;
  tm_mqtt_conn_t* conn;
  tm_server_t* s;
  tm_packet_decoder_t* decoder;
  int pkt_id;
  tm_mqtt_msg_t* msg;
  const char* conn_id;
  
  conn = (tm_mqtt_conn_t*) ts_server__get_conn_user_data(server, c);
  conn_id = ts_server__get_conn_remote_host(server, c);
  s = conn->server;
  decoder = &conn->decoder;
  
  LOG_DUMP(pkt_bytes, pkt_bytes_len, "[%s][%s] Receive [PUBREL]", conn_id, conn->session->client_id);
  
  tm_packet_decoder__set(decoder, pkt_bytes + variable_header_off, pkt_bytes_len - variable_header_off);
  
  err = tm_packet_decoder__read_int16(decoder, &pkt_id);
  if (err || pkt_id <= 0) {
    LOG_ERROR("[%s][%s] Invalid Packet id", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  
  msg = tm_mqtt_session__find_in_msg(conn->session, pkt_id);
  if (msg == NULL || tm_mqtt_msg__qos(msg) != 2) {
    LOG_ERROR("[%s][%s] Invalid Packet id", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  
  tm_mqtt_conn__update_msg_state(server, c, msg);
  err = tm_mqtt_conn__send_pubcomp(server, c, pkt_id, msg);
  if (err) {
    LOG_ERROR("[%s][%s] Failed to send PUBCOMP", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  
done:
  return 0;
}
int tm_mqtt_conn__process_pubcomp(ts_t* server, ts_conn_t* c, const char* pkt_bytes, int pkt_bytes_len, int variable_header_off) {
  int err;
  tm_mqtt_conn_t* conn;
  tm_server_t* s;
  tm_packet_decoder_t* decoder;
  int pkt_id;
  tm_mqtt_msg_t* msg;
  const char* conn_id;
  
  conn = (tm_mqtt_conn_t*) ts_server__get_conn_user_data(server, c);
  conn_id = ts_server__get_conn_remote_host(server, c);
  s = conn->server;
  decoder = &conn->decoder;
  
  LOG_DUMP(pkt_bytes, pkt_bytes_len, "[%s][%s] Receive [PUBCOMP]", conn_id, conn->session->client_id);
  
  tm_packet_decoder__set(decoder, pkt_bytes + variable_header_off, pkt_bytes_len - variable_header_off);
  
  err = tm_packet_decoder__read_int16(decoder, &pkt_id);
  if (err) {
    LOG_ERROR("[%s][%s] Invalid Packet id", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  
  msg = tm_mqtt_session__find_out_msg(conn->session, pkt_id);
  if (msg == NULL) {
    LOG_ERROR("[%s][%s] Invalid Packet id", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  if (tm_mqtt_msg__qos(msg) != 2 || tm_mqtt_msg__get_state(msg) != MSG_STATE_WAIT_PUBCOMP) {
    LOG_ERROR("[%s][%s] Invalid Message state", conn_id, conn->session->client_id);
    tm_mqtt_conn__abort(server, c);
    goto done;
  }
  
  tm_mqtt_conn__update_msg_state(server, c, msg);
  
done:
  return 0;
}
