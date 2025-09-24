#ifndef THINWIRE_H
#define THINWIRE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TWDEF
#define TWDEF
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef _WIN32
typedef int socklen_t;
#define close(fd) closesocket(fd)

#define poll(fds, nfds, timeout) WSAPoll(fds, nfds, timeout)
#endif

typedef enum { TW_INFO, TW_WARNING, TW_ERROR } tw_log_level;

TWDEF void tw_log(tw_log_level level, const char *fmt, ...);

typedef struct {
  char **keys;
  char **values;
  size_t capacity;
  size_t size;
} tw_map;

TWDEF bool tw_map_init(tw_map *map);
TWDEF void tw_map_free(tw_map *map);
TWDEF bool tw_map_set(tw_map *map, const char *key, const char *value);
TWDEF const char *tw_map_get(tw_map *map, const char *key);
TWDEF bool tw_map_remove(tw_map *map, const char *key);
TWDEF bool tw_map_remove_at(tw_map *map, size_t index);
TWDEF bool tw_map_empty(tw_map *map);

#ifndef TW_DEFAULT_PORT
#define TW_DEFAULT_PORT 8080
#endif

#ifndef TW_MAX_CLIENTS
#define TW_MAX_CLIENTS 100
#endif

typedef struct {
  int fd;
  struct sockaddr_in addr;
} tw_conn;

typedef struct {
  int fd;
  struct sockaddr_in addr;

  struct pollfd fds[TW_MAX_CLIENTS + 1];
  tw_conn conns[TW_MAX_CLIENTS + 1];
  int nfds;
} tw_server;

#ifndef TW_MAX_HEADERS
#define TW_MAX_HEADERS 64
#endif

#ifndef TW_MAX_HEADER_NAME
#define TW_MAX_HEADER_NAME 256
#endif

#ifndef TW_MAX_HEADER_VALUE
#define TW_MAX_HEADER_VALUE 8192
#endif

typedef struct {
  char name[TW_MAX_HEADER_NAME];
  char value[TW_MAX_HEADER_VALUE];
} tw_header;

#ifndef TW_MAX_REQUEST_SIZE
#define TW_MAX_REQUEST_SIZE 8192
#endif

#ifndef TW_MAX_REQUEST_BODY
#define TW_MAX_REQUEST_BODY (128 * 1024 * 1024)
#endif

typedef struct {
  char method[64];
  size_t method_len;

  char version[16];
  size_t version_len;

  char path[1024];
  size_t path_len;

  tw_map headers;

  bool keep_alive;

  char *body;
  size_t body_len;
} tw_request;

typedef struct {
  int status;

  tw_map headers;

  char *body;
  size_t body_len;
} tw_response;

typedef void (*tw_request_handler_fn)(tw_conn *conn, tw_request *req,
                                      tw_response *res);

TWDEF bool tw_server_init(tw_server *server, int port);
TWDEF bool tw_server_run(tw_server *server, tw_request_handler_fn handler);
TWDEF bool tw_server_stop(tw_server *server);
TWDEF bool tw__set_nonblocking(int fd);

TWDEF ssize_t tw_conn_read(tw_conn *conn, char *buf, size_t len);
TWDEF ssize_t tw_conn_write(tw_conn *conn, const char *buf, size_t len);
TWDEF void tw_conn_close(tw_conn *conn);

typedef enum {
  TW_REQUEST_PARSE_SUCCESS = 0,
  TW_REQUEST_PARSE_ERROR = 1,
  TW_REQUEST_PARSE_BLOCK = 2
} tw_request_parse_result;

TWDEF bool tw_request_init(tw_request *req);
TWDEF void tw_request_free(tw_request *req);
TWDEF tw_request_parse_result tw_request_parse(tw_conn *conn, tw_request *req);
TWDEF tw_request_parse_result tw_request_parse_body(tw_conn *conn,
                                                    tw_request *req);
TWDEF const char *tw_request_get_header(tw_request *req, const char *name);

TWDEF bool tw_response_init(tw_response *res);
TWDEF void tw_response_free(tw_response *res);
TWDEF void tw_response_set_status(tw_response *res, int status);
TWDEF void tw_response_set_header(tw_response *res, const char *name,
                                  const char *value);
TWDEF bool tw_response_set_body(tw_response *res, const char *body,
                                size_t body_len);
TWDEF bool tw_response_send(tw_conn *conn, tw_response *res);

#ifdef __cplusplus
}
#endif

#endif  // THINWIRE_H

#ifdef THINWIRE_IMPL

TWDEF void tw_log(tw_log_level level, const char *fmt, ...) {
  FILE *stream = stdout;

  switch (level) {
    case TW_INFO:
      fprintf(stream, "[INFO] ");
      break;
    case TW_WARNING:
      fprintf(stream, "[WARNING] ");
      break;
    case TW_ERROR:
      stream = stderr;
      fprintf(stream, "[ERROR] ");
      break;
    default:
      break;
  }

  va_list args;
  va_start(args, fmt);
  vfprintf(stream, fmt, args);
  va_end(args);
  fprintf(stream, "\n");
}

TWDEF bool tw_map_init(tw_map *map) {
  map->values = NULL;
  map->keys = (char **)malloc(sizeof(char *));
  if (map->keys == NULL) {
    tw_log(TW_ERROR, "Failed to allocate memory for tw_map->keys");
    return false;
  }
  map->keys[0] = NULL;

  map->values = (char **)malloc(sizeof(char *));
  if (map->values == NULL) {
    tw_log(TW_ERROR, "Failed to allocate memory for tw_map->values");
    free(map->keys);
    map->keys = NULL;
    return false;
  }
  map->values[0] = NULL;

  map->capacity = 1;
  map->size = 0;

  return true;
}

TWDEF void tw_map_free(tw_map *map) {
  for (size_t i = 0; i < map->size; i++) {
    free(map->keys[i]);
    free(map->values[i]);
  }

  free(map->keys);
  free(map->values);
  map->keys = NULL;
  map->values = NULL;
  map->capacity = 0;
  map->size = 0;
}

TWDEF bool tw_map_set(tw_map *map, const char *key, const char *value) {
  for (size_t i = 0; i < map->size; i++) {
    if (strcmp(map->keys[i], key) == 0) {
      char *dup_value = strdup(value);
      if (dup_value == NULL) {
        tw_log(TW_ERROR, "Failed to allocate memory for tw_map->values");
        return false;
      }

      free(map->values[i]);
      map->values[i] = dup_value;
      return true;
    }
  }

  if (map->size >= map->capacity) {
    size_t new_capacity = map->capacity * 2;
    char **new_keys =
        (char **)realloc(map->keys, new_capacity * sizeof(char *));
    if (new_keys == NULL) {
      tw_log(TW_ERROR, "Failed to allocate memory for tw_map->keys");
      return false;
    }
    map->keys = new_keys;

    char **new_values =
        (char **)realloc(map->values, new_capacity * sizeof(char *));
    if (new_values == NULL) {
      tw_log(TW_ERROR, "Failed to allocate memory for tw_map->values");
      return false;
    }
    map->values = new_values;

    map->capacity = new_capacity;
  }

  map->keys[map->size] = strdup(key);
  if (map->keys[map->size] == NULL) {
    tw_log(TW_ERROR, "Failed to allocate memory for tw_map->keys");
    return false;
  }
  map->values[map->size] = strdup(value);
  if (map->values[map->size] == NULL) {
    tw_log(TW_ERROR, "Failed to allocate memory for tw_map->values");
    free(map->keys[map->size]);
    return false;
  }

  map->size++;
  return true;
}

TWDEF const char *tw_map_get(tw_map *map, const char *key) {
  for (size_t i = 0; i < map->size; i++) {
    if (strcmp(map->keys[i], key) == 0) {
      return map->values[i];
    }
  }

  return NULL;
}

TWDEF bool tw_map_remove(tw_map *map, const char *key) {
  for (size_t i = 0; i < map->size; i++) {
    if (strcmp(map->keys[i], key) == 0) {
      return tw_map_remove_at(map, i);
    }
  }

  return false;
}

TWDEF bool tw_map_remove_at(tw_map *map, size_t index) {
  free(map->keys[index]);
  free(map->values[index]);

  for (size_t i = index; i < map->size; i++) {
    map->keys[i] = map->keys[i + 1];
    map->values[i] = map->values[i + 1];
  }

  map->size--;

  if (map->size > 0) {
    char **new_keys = (char **)realloc(map->keys, map->size * sizeof(char *));
    if (new_keys == NULL) {
      tw_log(TW_ERROR, "Failed to allocate memory for tw_map->keys");
      return false;
    }
    map->keys = new_keys;

    char **new_values =
        (char **)realloc(map->values, map->size * sizeof(char *));
    if (new_values == NULL) {
      tw_log(TW_ERROR, "Failed to allocate memory for tw_map->values");
      return false;
    }
    map->values = new_values;
  }

  return true;
}

TWDEF bool tw_map_empty(tw_map *map) {
  tw_map_free(map);
  return tw_map_init(map);
}

TWDEF bool tw_server_init(tw_server *server, int port) {
#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    tw_log(TW_ERROR, "WSAStartup failed");
    return false;
  }
#endif

  if ((server->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    tw_log(TW_ERROR, "Socket failed");
    return false;
  }

  if (port <= 0) {
    port = TW_DEFAULT_PORT;
  }

  server->addr.sin_family = AF_INET;
  server->addr.sin_addr.s_addr = INADDR_ANY;
  server->addr.sin_port = htons(port);

  if (bind(server->fd, (struct sockaddr *)&server->addr, sizeof(server->addr)) <
      0) {
    tw_log(TW_ERROR, "Socket bind failed");
    return false;
  }

  if (listen(server->fd, 10) < 0) {
    tw_log(TW_ERROR, "Socket listen failed");
    return false;
  }

  if (!tw__set_nonblocking(server->fd)) {
    return false;
  }

  server->nfds = 1;
  memset(server->fds, 0, sizeof(server->fds));
  server->fds[0].fd = server->fd;
  server->fds[0].events = POLLIN;

  return true;
}

TWDEF bool tw_server_run(tw_server *server, tw_request_handler_fn handler) {
  //while (1)
  {
    int ret = poll(server->fds, server->nfds, -1);
    if (ret < 0) {
      tw_log(TW_ERROR, "Poll failed");
      return false;
    }

    if (server->fds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
      while (server->nfds < TW_MAX_CLIENTS + 1) {
        tw_conn conn;
        socklen_t addr_len = sizeof(conn.addr);
#ifdef _WIN32
        SOCKET conn_fd =
            accept(server->fd, (struct sockaddr *)&conn.addr, &addr_len);
        if (conn_fd == INVALID_SOCKET) {
          int werr = WSAGetLastError();
          if (werr == WSAEWOULDBLOCK) {
            /* no more pending connections */
            break;
          } else {
            tw_log(TW_ERROR, "accept failed: %d", werr);
            break;
          }
        }
        conn.fd = (int)conn_fd;
#else
        int conn_fd =
            accept(server->fd, (struct sockaddr *)&conn.addr, &addr_len);
        if (conn_fd < 0) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* no more pending connections */
            break;
          } else {
            tw_log(TW_ERROR, "accept failed: %s", strerror(errno));
            break;
          }
        }
        conn.fd = conn_fd;
#endif

        tw__set_nonblocking(conn_fd);

        server->conns[server->nfds] = conn;
        server->fds[server->nfds].fd = conn_fd;
        server->fds[server->nfds].events = POLLIN;
        server->fds[server->nfds].revents = 0;
        server->nfds++;
      }

      server->fds[0].revents = 0;
    }

    for (int i = 1; i < server->nfds; i++) {
      tw_conn *conn = &server->conns[i];
      short revents = server->fds[i].revents;

      if (!(revents & POLLIN)) {
        if (revents & (POLLHUP | POLLERR | POLLNVAL)) {
          tw_conn_close(conn);
#ifdef _WIN32
          server->fds[i].fd = (SOCKET)-1;
#else
          server->fds[i].fd = -1;
#endif
          conn->fd = -1;
        }
        server->fds[i].revents = 0;
        continue;
      }

      bool keep_alive = true;
      while (keep_alive) {
        tw_request req;
        if (!tw_request_init(&req)) {
          keep_alive = false;
          break;
        };

        tw_request_parse_result req_parse_result = tw_request_parse(conn, &req);
        if (req_parse_result == TW_REQUEST_PARSE_ERROR) {
          tw_response res;
          if (!tw_response_init(&res)) {
            keep_alive = false;
            break;
          };

          tw_response_set_status(&res, 400);
          const char *body = "Bad Request";
          tw_response_set_body(&res, body, strlen(body));

          tw_response_send(conn, &res);
          tw_request_free(&req);
          tw_response_free(&res);
          keep_alive = false;
          break;
        } else if (req_parse_result == TW_REQUEST_PARSE_BLOCK) {
          /* no data available yet */
          tw_request_free(&req);
          keep_alive = false;
          break;
        }

        tw_response res;
        if (!tw_response_init(&res)) {
          tw_request_free(&req);
          keep_alive = false;
          break;
        };

        if (req.keep_alive) {
          tw_response_set_header(&res, "Connection", "keep-alive");
        } else {
          tw_response_set_header(&res, "Connection", "close");
          keep_alive = false;
        }

        handler(conn, &req, &res);

        tw_request_free(&req);
        tw_response_free(&res);

        if (!keep_alive) {
          tw_conn_close(conn);
#ifdef _WIN32
          server->fds[i].fd = (SOCKET)-1;
#else
          server->fds[i].fd = -1;
#endif
          conn->fd = -1;
          break;
        }
      }

      server->fds[i].revents = 0;
    }

    int current = 1;
    for (int i = 1; i < server->nfds; i++) {
#ifdef _WIN32
      if (server->fds[i].fd != (SOCKET)-1) {
#else
      if (server->fds[i].fd != -1) {
#endif
        if (current != i) {
          server->fds[current] = server->fds[i];
          server->conns[current] = server->conns[i];
        }
        current++;
      } else {
        server->fds[i].events = 0;
        server->fds[i].revents = 0;
        server->conns[i].fd = -1;
      }
    }

    server->nfds = current;
  }

  return true;
}

TWDEF bool tw_server_stop(tw_server *server) {
  if (close(server->fd) < 0) {
    return false;
  }

#ifdef _WIN32
  WSACleanup();
#endif

  return true;
}

TWDEF bool tw__set_nonblocking(int fd) {
#ifdef _WIN32
  DWORD mode = 1;
  if (ioctlsocket(fd, FIONBIO, &mode) != 0) {
    tw_log(TW_ERROR, "ioctlsocket failed");
    return false;
  }
#else
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    tw_log(TW_ERROR, "fcntl(F_GETFL) failed");
    return false;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    tw_log(TW_ERROR, "fcntl(F_SETFL) failed");
    return false;
  }
#endif
  return true;
}

TWDEF ssize_t tw_conn_read(tw_conn *conn, char *buf, size_t len) {
  ssize_t bytes_read = recv(conn->fd, buf, len, 0);
  return bytes_read;
};

TWDEF ssize_t tw_conn_write(tw_conn *conn, const char *buf, size_t len) {
  ssize_t bytes_sent = send(conn->fd, buf, len, 0);
  return bytes_sent;
};

TWDEF void tw_conn_close(tw_conn *conn) { close(conn->fd); };

TWDEF bool tw_request_init(tw_request *req) {
  if (req != NULL) {
    tw_map_init(&req->headers);
    req->keep_alive = false;
    req->body = NULL;
    req->body_len = 0;
    return true;
  } else {
    return false;
  }
}

TWDEF void tw_request_free(tw_request *req) {
  if (req != NULL) {
    free(req->body);
    tw_map_free(&req->headers);
    req->body = NULL;
    req->body_len = 0;
  }
}

TWDEF tw_request_parse_result tw_request_parse(tw_conn *conn, tw_request *req) {
  char buf[TW_MAX_REQUEST_SIZE];
  ssize_t bytes_read = 0;
  ssize_t total_read = 0;
  const char *headers_end = NULL;

  while (total_read < TW_MAX_REQUEST_SIZE - 1) {
    bytes_read = tw_conn_read(conn, buf + total_read,
                              TW_MAX_REQUEST_SIZE - total_read - 1);
    if (bytes_read <= 0) {
#ifdef _WIN32
      if (WSAGetLastError() == WSAEWOULDBLOCK) {
        return TW_REQUEST_PARSE_BLOCK;
      }
#else
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return TW_REQUEST_PARSE_BLOCK;
      }
#endif
      return TW_REQUEST_PARSE_ERROR;
    }

    total_read += bytes_read;
    buf[total_read] = '\0';

    headers_end = strstr(buf, "\r\n\r\n");
    if (headers_end) {
      break;
    }
  }

  if (!headers_end) {
    /* headers too large or malformed */
    return TW_REQUEST_PARSE_ERROR;
  }

  const char *pos = buf;

  const char *method_end = strchr(buf, ' ');
  if (!method_end) {
    return TW_REQUEST_PARSE_ERROR;
  }
  req->method_len = (size_t)(method_end - pos);
  if (req->method_len >= sizeof(req->method)) {
    return TW_REQUEST_PARSE_ERROR;
  }
  memcpy(req->method, pos, req->method_len);
  req->method[req->method_len] = '\0';

  const char *path_start = method_end + 1;
  const char *path_end = strchr(path_start, ' ');
  if (!path_end) {
    return TW_REQUEST_PARSE_ERROR;
  }
  req->path_len = (size_t)(path_end - path_start);
  if (req->path_len >= sizeof(req->path)) {
    return TW_REQUEST_PARSE_ERROR;
  }
  memcpy(req->path, path_start, req->path_len);
  req->path[req->path_len] = '\0';

  const char *version_start = path_end + 1;
  const char *version_end = strstr(version_start, "\r\n");
  if (!version_end) {
    version_end = strchr(version_start, '\n');
    if (!version_end) {
      return TW_REQUEST_PARSE_ERROR;
    }
  }
  req->version_len = (size_t)(version_end - version_start);
  if (req->version_len >= sizeof(req->version)) {
    return TW_REQUEST_PARSE_ERROR;
  }
  memcpy(req->version, version_start, req->version_len);
  req->version[req->version_len] = '\0';

  pos = version_end + 2;

  while (*pos && !(pos[0] == '\r' && pos[1] == '\n')) {
    const char *colon = strchr(pos, ':');
    if (!colon) return TW_REQUEST_PARSE_ERROR;

    size_t name_len = (size_t)(colon - pos);
    if (name_len >= TW_MAX_HEADER_NAME) return TW_REQUEST_PARSE_ERROR;
    char name[TW_MAX_HEADER_NAME];
    memcpy(name, pos, name_len);
    name[name_len] = '\0';

    const char *value_start = colon + 1;
    while (*value_start == ' ') value_start++;

    const char *line_end = strstr(value_start, "\r\n");
    if (!line_end) return TW_REQUEST_PARSE_ERROR;

    size_t value_len = (size_t)(line_end - value_start);
    if (value_len >= TW_MAX_HEADER_VALUE) return TW_REQUEST_PARSE_ERROR;
    char value[TW_MAX_HEADER_VALUE];
    memcpy(value, value_start, value_len);
    value[value_len] = '\0';

    tw_map_set(&req->headers, name, value);
    pos = line_end + 2;
  }

  req->keep_alive = false;

  const char *conn_hdr = tw_request_get_header(req, "Connection");
  if (conn_hdr) {
    if (strcasecmp(conn_hdr, "keep-alive") == 0) {
      req->keep_alive = true;
    }
  } else if (strcmp(req->version, "HTTP/1.1") == 0) {
    /* HTTP/1.1 defaults to keep-alive */
    req->keep_alive = true;
  }

  size_t header_bytes = headers_end + 4 - buf;
  size_t leftover = total_read - header_bytes;

  req->body = NULL;
  req->body_len = 0;

  if (leftover > 0) {
    req->body = (char*)malloc(leftover + 1);
    if (!req->body) {
      tw_log(TW_ERROR, "Failed to allocate memory for request body");
      return TW_REQUEST_PARSE_ERROR;
    }
    memcpy(req->body, headers_end + 4, leftover);
    req->body[leftover] = '\0';
    req->body_len = leftover;
  }

  return TW_REQUEST_PARSE_SUCCESS;
}

TWDEF tw_request_parse_result tw_request_parse_body(tw_conn *conn,
                                                    tw_request *req) {
  const char *cl_hdr = tw_request_get_header(req, "Content-Length");
  if (!cl_hdr) {
    /* no body to parse */
    return TW_REQUEST_PARSE_SUCCESS;
  }

  size_t content_length = strtoul(cl_hdr, NULL, 10);
  if (content_length == 0) {
    /* empty body */
    return TW_REQUEST_PARSE_SUCCESS;
  }

  if (content_length > TW_MAX_REQUEST_BODY) {
    content_length = TW_MAX_REQUEST_BODY;
  }

  if (!req->body) {
    /* allocate memory for the body */
    req->body = (char *)malloc((size_t)content_length + 1);
    if (!req->body) {
      tw_log(TW_ERROR, "Failed to allocate memory for request body");
      return TW_REQUEST_PARSE_ERROR;
    }
  } else {
    char *new_body = (char*)realloc(req->body, content_length + 1);
    if (!new_body) {
      free(req->body);
      req->body = NULL;
      req->body_len = 0;
      return TW_REQUEST_PARSE_ERROR;
    }
    req->body = new_body;
  }

  size_t total_read = req->body_len;
  size_t bytes_remaining = content_length - req->body_len;

  while (bytes_remaining > 0) {
    ssize_t bytes_read =
        tw_conn_read(conn, req->body + total_read, bytes_remaining);
    if (bytes_read <= 0) {
#ifdef _WIN32
      if (WSAGetLastError() == WSAEWOULDBLOCK) {
        return TW_REQUEST_PARSE_BLOCK;
      }
#else
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return TW_REQUEST_PARSE_BLOCK;
      }
#endif
      free(req->body);
      req->body = NULL;
      return TW_REQUEST_PARSE_ERROR;
    }

    total_read += bytes_read;
    bytes_remaining -= bytes_read;
  }

  req->body[content_length] = '\0';
  req->body_len = content_length;

  return TW_REQUEST_PARSE_SUCCESS;
}

TWDEF const char *tw_request_get_header(tw_request *req, const char *name) {
  return tw_map_get(&req->headers, name);
}

TWDEF bool tw_response_init(tw_response *res) {
  if (res != NULL) {
    tw_map_init(&res->headers);
    res->status = 200;
    res->body = NULL;
    res->body_len = 0;
    return true;
  } else {
    return false;
  }
}

TWDEF void tw_response_free(tw_response *res) {
  if (res != NULL) {
    free(res->body);
    tw_map_free(&res->headers);
    res->body = NULL;
    res->body_len = 0;
  }
}

TWDEF void tw_response_set_status(tw_response *res, int status) {
  res->status = status;
}

TWDEF void tw_response_set_header(tw_response *res, const char *name,
                                  const char *value) {
  size_t name_len = strlen(name);
  if (name_len >= TW_MAX_HEADER_NAME) {
    tw_log(TW_WARNING, "Header name too long.");
    return;
  }

  size_t value_len = strlen(value);
  if (value_len >= TW_MAX_HEADER_VALUE) {
    tw_log(TW_WARNING, "Header value too long.");
    return;
  }

  tw_map_set(&res->headers, name, value);
}

static const char *tw_status_text(int status) {
  switch (status) {
    case 100:
      return "Continue";
    case 101:
      return "Switching Protocols";
    case 200:
      return "OK";
    case 201:
      return "Created";
    case 202:
      return "Accepted";
    case 203:
      return "Non-Authoritative Information";
    case 204:
      return "No Content";
    case 205:
      return "Reset Content";
    case 206:
      return "Partial Content";
    case 207:
      return "Multi-Status";
    case 300:
      return "Multiple Choices";
    case 301:
      return "Moved Permanently";
    case 302:
      return "Found";
    case 303:
      return "See Other";
    case 304:
      return "Not Modified";
    case 307:
      return "Temporary Redirect";
    case 400:
      return "Bad Request";
    case 401:
      return "Unauthorized";
    case 402:
      return "Payment Required";
    case 403:
      return "Forbidden";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 406:
      return "Not Acceptable";
    case 407:
      return "Proxy Authentication Required";
    case 408:
      return "Request Timeout";
    case 409:
      return "Conflict";
    case 410:
      return "Gone";
    case 411:
      return "Length Required";
    case 412:
      return "Precondition Failed";
    case 413:
      return "Content Too Large";
    case 414:
      return "URI Too Long";
    case 415:
      return "Unsupported Media Type";
    case 416:
      return "Range Not Satisfiable";
    case 417:
      return "Expectation Failed";
    case 421:
      return "Misdirected Request";
    case 500:
      return "Internal Server Error";
    case 501:
      return "Not Implemented";
    case 502:
      return "Bad Gateway";
    case 503:
      return "Service Unavailable";
    case 504:
      return "Gateway Timeout";
    case 505:
      return "HTTP Version Not Supported";
    default:
      return "";
  }
}

TWDEF bool tw_response_set_body(tw_response *res, const char *body,
                                size_t body_len) {
  if (res != NULL && body != NULL) {
    free(res->body);
    res->body = (char *)malloc(body_len + 1);
    if (res->body == NULL) {
      tw_log(TW_ERROR, "Failed to allocate memory for tw_response->body");
      return false;
    }

    memcpy(res->body, body, body_len);
    res->body[body_len] = '\0';
    res->body_len = body_len;

    return true;
  } else {
    return false;
  }
}

TWDEF bool tw_response_send(tw_conn *conn, tw_response *res) {
  char header_buf[TW_MAX_HEADER_VALUE * 2];
  int offset = 0;

  const char *status_text = tw_status_text(res->status);
  offset += snprintf(header_buf + offset, sizeof(header_buf) - offset,
                     "HTTP/1.1 %d %s\r\n", res->status, status_text);

  offset += snprintf(header_buf + offset, sizeof(header_buf) - offset,
                     "Content-Length: %zu\r\n", res->body_len);

  for (size_t i = 0; i < res->headers.size; i++) {
    offset +=
        snprintf(header_buf + offset, sizeof(header_buf) - offset, "%s: %s\r\n",
                 res->headers.keys[i], res->headers.values[i]);
  }

  offset += snprintf(header_buf + offset, sizeof(header_buf) - offset, "\r\n");

  if (tw_conn_write(conn, header_buf, offset) < 0) {
    tw_log(TW_ERROR, "Failed to send response headers");
    return false;
  }

  if (res->body && res->body_len > 0) {
    if (tw_conn_write(conn, res->body, res->body_len) < 0) {
      tw_log(TW_ERROR, "Failed to send response body");
      return false;
    }
  }

  return true;
}

#endif  // THINWIRE_IMPL