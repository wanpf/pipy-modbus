#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <pipy/nmi.h>

#include "modbus.h"

enum {
  id_variable_modbusDeviceName,
  id_variable_modbusSlaveID,
  id_variable_modbusBaud,
};

typedef enum DATA_TYPE {
  SHORT,
  FLOAT,
} DATA_TYPE;

typedef struct read_task {
  int addr;
  char *addr_str;
  DATA_TYPE type;
} read_task;

// clang-format off
static read_task task_list[] = {
  { 0, "0", SHORT },
  { 1, "1", SHORT },
  { 2, "2", SHORT },
  { 3, "3", SHORT },
  { 4, "4", SHORT },
  { 5, "5", SHORT },
  { 6, "6", SHORT },
  { 7, "7", FLOAT },
  { 8, "8", FLOAT },
  { 9, "9", SHORT },
  { 0x0a, "10", SHORT },
  { 0x0b, "11", SHORT },
  { 0x0c, "12", SHORT },
  { 0x0d, "13", SHORT },
  { 0x0e, "14", SHORT },
};
// clang-format on

static modbus_t *init_modbus(char *device, int slave, int baud, char **err_msg) {
  modbus_t *ctx = NULL;
  uint32_t old_response_to_sec;
  uint32_t old_response_to_usec;
  uint32_t new_response_to_sec;
  uint32_t new_response_to_usec;

  if ((ctx = modbus_new_rtu(device, baud, 'N', 8, 1)) == NULL) {
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
      int baud = get_int(ppl, id_variable_modbusBaud);
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
        static int precision = 0;
        modbus_t *ctx = init_modbus(device, slave, baud, &err_msg);

        if (ctx == NULL) {
          printf("========= init modbus error: %s\n", (const char *)err_msg);
        } else {
          buffer_append(&err_msg, &ptr, &space, "\"data\":[");

          for (int i = 0; (err_msg == NULL) && (i < sizeof(task_list) / sizeof(task_list[0])); i++) {
            int rc;
            read_task *task = &task_list[i];

            // printf("====== addr: %d, size: %d\n", task->addr, size);

            rc = modbus_read_registers(ctx, task->addr, 1, (unsigned short *)data);
            if (rc != 1) {
              buffer_append(&err_msg, &ptr, &space, "{\"Address\":\"%s\",\"Error\":\"%s\"},", task->addr_str, modbus_strerror(errno));
              // printf("====== addr: %d, size: %d, type: float, error: %s\n", task->addr, size, modbus_strerror(errno));
            } else {
              int value = *((short *)data);

              if (task->addr == 3) {
                precision = value;
              }
              if (task->type == SHORT) {
                buffer_append(&err_msg, &ptr, &space, "{\"Address\":\"%s\",\"Type\":\"short\",\"Value\":\"%d\"},", task->addr_str, value);
                //	        printf("====== addr: %d, size: %d, type: short, value: %d\n", task->addr, size, value);
              } else {
                int round = pow(10, precision);
                // printf("precision: %d, float value : %d\n", precision, value);
                buffer_append(&err_msg, &ptr, &space, "{\"Address\":\"%s\",\"Type\":\"float\",\"Value\":\"%d.%d\"},", task->addr_str, value / round,
                              abs(value) % round);
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
  pipy_define_variable(id_variable_modbusBaud, "__modbusBaud", "modbus-nmi", pjs_undefined());
  pipy_define_pipeline("", pipeline_init, pipeline_free, pipeline_process);
}
