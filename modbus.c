#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <pipy/nmi.h>

#include "modbus.h"

enum {
  id_variable_modbusDeviceName,
  id_variable_modbusSlaveID,
};

typedef enum DATA_TYPE {
  SHOT,
  FLOAT,
} DATA_TYPE;

typedef struct read_task {
  int addr;
  char *addr_str;
  DATA_TYPE type;
} read_task;

// clang-format off
static read_task task_list[] = { 
  { 0, "0", SHOT }, 
  { 1, "1", SHOT }, 
  { 2, "2", SHOT }, 
  { 3, "3", SHOT }, 
  { 4, "4", SHOT }, 
  { 5, "5", SHOT }, 
  { 6, "6", SHOT }, 
  { 7, "7", SHOT }, 
  { 8, "8", SHOT }, 
  { 0x1415, "20-21", FLOAT },
  { 0x1617, "22-23", FLOAT }, 
  { 0x1819, "24-25", FLOAT }, 
  { 0x1a1b, "26-27", FLOAT }, 
  { 0x1c1d, "28-29", FLOAT }, 
  { 0x1e1f, "30-31", FLOAT }, 
  { 0x2021, "32-33", FLOAT }, 
  { 0x2a2b, "42-43", FLOAT },
  { 0xb5b6, "181-182", FLOAT }, 
};
// clang-format on

static modbus_t *init_modbus(char *device, int slave, char **err_msg) {
  modbus_t *ctx = NULL;
  uint32_t old_response_to_sec;
  uint32_t old_response_to_usec;
  uint32_t new_response_to_sec;
  uint32_t new_response_to_usec;

  if ((ctx = modbus_new_rtu(device, 115200, 'N', 8, 1)) == NULL) {
    *err_msg = "Unable to allocate libmodbus context\n";
    return NULL;
  }
  modbus_set_debug(ctx, FALSE);
  modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);
  modbus_set_slave(ctx, slave);
  modbus_get_response_timeout(ctx, &old_response_to_sec, &old_response_to_usec);
  if (modbus_connect(ctx) == -1) {
    *err_msg = (char *)modbus_strerror(errno);
    modbus_free(ctx);
    return NULL;
  }
  modbus_get_response_timeout(ctx, &new_response_to_sec, &new_response_to_usec);
  assert(old_response_to_sec == new_response_to_sec && old_response_to_usec == new_response_to_usec);

  return ctx;
}

struct pipeline_state {
  int is_started;
};

static void pipeline_init(pipy_pipeline ppl, void **user_ptr) {
  *user_ptr = calloc(1, sizeof(struct pipeline_state));
  ((struct pipeline_state *)*user_ptr)->is_started = 0;
}

static void pipeline_free(pipy_pipeline ppl, void *user_ptr) {
  struct pipeline_state *state = (struct pipeline_state *)user_ptr;
  free(user_ptr);
}

static char *get_string(pipy_pipeline ppl, int id, char *buf, int size) {
  pjs_value value = pjs_undefined();
  pipy_get_variable(ppl, id, value);
  if (pjs_is_undefined(value)) {
    return NULL;
  }
  int n = pjs_string_get_utf8_data(value, buf, size - 1);
  if (n > 0) {
    buf[n] = '\0';
    return buf;
  }
  return NULL;
}

static int get_int(pipy_pipeline ppl, int id) {
  pjs_value value = pjs_undefined();
  pipy_get_variable(ppl, id, value);
  if (pjs_is_undefined(value)) {
    return -1;
  }
  return pjs_to_number(value);
}

static void buffer_append(char **err_msg, char **buffer, int *space, const char *fmt, ...) {
  int n = 0;
  va_list args;

  va_start(args, fmt);
  n = vsnprintf(*buffer, *space, fmt, args);
  va_end(args);
  if (n <= 0 || n + 1 >= *space) {
    *err_msg = "too many data";
  } else {
    *buffer += n;
    *space -= n;
  }
}

static void pipeline_process(pipy_pipeline ppl, void *user_ptr, pjs_value evt) {
  struct pipeline_state *state = (struct pipeline_state *)user_ptr;
  if (pipy_is_MessageStart(evt)) {
    state->is_started = 1;
  } else if (pipy_is_MessageEnd(evt)) {
    if (state->is_started == 1) {
      char device[256] = {'\0'};
      unsigned char data[256]; // max 250 bytes
      int slave = get_int(ppl, id_variable_modbusSlaveID);
      char *err_msg = NULL;
      pjs_value response_head = pjs_object();
      char buffer[2000];
      char *ptr = buffer;
      int space = sizeof(buffer);
      int ts = time(NULL);

      get_string(ppl, id_variable_modbusDeviceName, device, sizeof(device));
      pjs_object_set_property(response_head, pjs_string("id", strlen("id")), pjs_number(slave));
      pjs_object_set_property(response_head, pjs_string("ts", strlen("ts")), pjs_number(ts));

      buffer_append(&err_msg, &ptr, &space, "{\"id\":%d,\"ts\":%d,", slave, ts);

      if (device[0] == '\0') {
        err_msg = "__modbusDeviceName is undefined";
      } else if (slave == -1) {
        err_msg = "__modbusSlaveID is undefined";
      }

      if (err_msg == NULL) {
        modbus_t *ctx = init_modbus(device, slave, &err_msg);

        if (ctx == NULL) {
          printf("========= init modbus error: %s\n", (const char *)err_msg);
        } else {
          buffer_append(&err_msg, &ptr, &space, "\"data\":[");

          for (int i = 0; (err_msg == NULL) && (i < sizeof(task_list) / sizeof(task_list[0])); i++) {
            int rc, size;
            read_task *task = &task_list[i];

            if (task->type == SHOT) {
              size = 1;
            } else {
              size = 2;
            }
            rc = modbus_read_registers(ctx, task->addr, size, (unsigned short *)data);
            if (rc != size) {
              buffer_append(&err_msg, &ptr, &space, "{\"Address\":\"%s\",\"Error\":\"%s\"},", task->addr_str, modbus_strerror(errno));
            } else {
              if (task->type == SHOT) {
                int value = *((short *)data);
                buffer_append(&err_msg, &ptr, &space, "{\"Address\":\"%s\",\"Type\":\"short\",\"Value\":%d},", task->addr_str, value);
              } else {
                float value = *((float *)data);
                buffer_append(&err_msg, &ptr, &space, "{\"Address\":\"%s\",\"Type\":\"float\",\"Value\":%.6f},", task->addr_str, value);
              }
            }
          }
          if (ptr > buffer && *(ptr - 1) == ',') {
            --ptr;
            ++space;
          }
          buffer_append(&err_msg, &ptr, &space, "]");

          modbus_close(ctx);
          modbus_free(ctx);
        }
      }

      if (err_msg != NULL) {
        printf("========= modbus error message: %s\n", (const char *)err_msg);
        buffer_append(&err_msg, &ptr, &space, ",\"error\":\"%s\"}", err_msg);
        pjs_object_set_property(response_head, pjs_string("error", strlen("error")), pjs_string(err_msg, strlen(err_msg)));
      } else {
        buffer_append(&err_msg, &ptr, &space, "}");
      }
      pipy_output_event(ppl, pipy_MessageStart_new(response_head));
      if (ptr > buffer) {
        pipy_output_event(ppl, pipy_Data_new(buffer, ptr - buffer));
      }
      pipy_output_event(ppl, pipy_MessageEnd_new(0, 0));
    }
  }
}

void pipy_module_init() {
  pipy_define_variable(id_variable_modbusDeviceName, "__modbusDeviceName", "modbus-nmi", pjs_undefined());
  pipy_define_variable(id_variable_modbusSlaveID, "__modbusSlaveID", "modbus-nmi", pjs_undefined());
  pipy_define_pipeline("", pipeline_init, pipeline_free, pipeline_process);
}
