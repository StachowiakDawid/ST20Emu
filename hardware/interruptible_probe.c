#include <asc.h>
#include <hw.h>
#include <pio.h>
#include <task.h>
#include <string.h>

#define PROBE_ROWS 256U
#define PROBE_WIDTH 4096U
#define PROBE_STRIDE (PROBE_WIDTH + 16U)
#define PROBE_STORAGE (PROBE_ROWS * PROBE_STRIDE)
#define PROBE_BYTES (PROBE_ROWS * PROBE_WIDTH)
#define MAX_SAMPLES 16U
#define DEST_SENTINEL 0xa5U

#define OP_MOVE 1U
#define OP_DEVMOVE 2U
#define OP_MOVE2DALL 3U
#define OP_MOVE2DNZ 4U
#define OP_MOVE2DZ 5U

typedef struct probe_sample_s {
  unsigned int operation;
  unsigned int words[12];
} probe_sample_t;

unsigned int probe_source_pointer;
unsigned int probe_destination_pointer;
unsigned int probe_byte_count;
unsigned int probe_rows;
unsigned int probe_source_stride;
unsigned int probe_destination_stride;
volatile unsigned int probe_snapshot_address;

static unsigned char probe_source[PROBE_STORAGE];
static unsigned char probe_destination[PROBE_STORAGE];
static volatile unsigned int probe_active;
static volatile unsigned int probe_stop_sampler;
static volatile unsigned int probe_operation;
static volatile unsigned int probe_sample_count;
static probe_sample_t probe_samples[MAX_SAMPLES];
static asc_dev_t *serial_device;

extern void hw_probe_move(void);
extern void hw_probe_devmove(void);
extern void hw_probe_move2dall(void);
extern void hw_probe_move2dnonzero(void);
extern void hw_probe_move2dzero(void);
extern void hw_probe_capture_low_shadow(void);
extern unsigned int hw_probe_current_priority(void);

static void serial_text(const char *text) {
  if (serial_device != 0)
    asc_write((const unsigned char *)text, (int)strlen(text), serial_device);
}

static void serial_hex(unsigned int value) {
  static const char digits[] = "0123456789abcdef";
  char text[8];
  unsigned int index;

  for (index = 0; index < 8; ++index)
    text[index] = digits[(value >> ((7U - index) * 4U)) & 0xfU];
  if (serial_device != 0)
    asc_write((const unsigned char *)text, 8, serial_device);
}

static int serial_init(void) {
  int chip_index = get_chip_index();
  int asc_number = get_asc_to_use();
  pio_init_t pio_config[5];
  asc_config_t config;

  fill_pio_config(pio_config, chip_index);
  pio_config[0].pio_init_config = pio_config_alternative_output;
  pio_config[1].pio_init_config = pio_config_input;
  pio_config[2].pio_init_config = pio_config_input;
  pio_config[3].pio_init_config = pio_config_alternative_output;
  pio_config[4].pio_init_base = (pio_reg_t *)-1;
  if (pio_init(pio_config) != 0) return -1;

  config.asc_config_ascbase = get_asc_base(asc_number);
  config.asc_config_flags = asc_flags_mode1;
  config.asc_config_clock_speed = get_cpu_speed();
  config.asc_config_int = get_asc_interrupt_number(asc_number);
  serial_device = asc_init(config);
  return serial_device == 0 ? -1 : 0;
}

static void sampler_task(void *unused) {
  unsigned int index;
  (void)unused;

  while (!probe_stop_sampler) {
    task_delay(1);
    if (!probe_active || probe_sample_count >= MAX_SAMPLES) continue;

    index = probe_sample_count;
    probe_samples[index].operation = probe_operation;
    probe_snapshot_address = (unsigned int)&probe_samples[index].words[0];
    hw_probe_capture_low_shadow();
    probe_sample_count = index + 1;
  }
}

static void initialize_buffers(void) {
  unsigned int index;
  for (index = 0; index < PROBE_STORAGE; ++index) {
    probe_source[index] = (index % 53U == 0) ? 0 : (unsigned char)(index * 37U + 11U);
    probe_destination[index] = DEST_SENTINEL;
  }
}

static unsigned int verify_result(unsigned int operation) {
  unsigned int index;
  unsigned int mismatch_count = 0;

  for (index = 0; index < PROBE_STORAGE; ++index) {
    unsigned char expected = DEST_SENTINEL;
    if (operation == OP_MOVE || operation == OP_DEVMOVE) {
      if (index < PROBE_BYTES) expected = probe_source[index];
    } else {
      unsigned int row = index / PROBE_STRIDE;
      unsigned int column = index % PROBE_STRIDE;
      if (row < PROBE_ROWS && column < PROBE_WIDTH) {
        unsigned char source = probe_source[row * PROBE_STRIDE + column];
        if (operation == OP_MOVE2DALL ||
            (operation == OP_MOVE2DNZ && source != 0) ||
            (operation == OP_MOVE2DZ && source == 0))
          expected = source;
      }
    }
    if (probe_destination[index] != expected) ++mismatch_count;
  }
  return mismatch_count;
}

static void print_samples(void) {
  unsigned int sample;
  unsigned int word;

  serial_text("snapshots=");
  serial_hex(probe_sample_count);
  serial_text("\r\n");
  for (sample = 0; sample < probe_sample_count; ++sample) {
    serial_text("sample=");
    serial_hex(sample);
    serial_text(" op=");
    serial_hex(probe_samples[sample].operation);
    for (word = 0; word < 12; ++word) {
      serial_text(" ");
      serial_hex(probe_samples[sample].words[word]);
    }
    serial_text("\r\n");
  }
}

static void run_case(unsigned int operation, const char *name, void (*entry)(void)) {
  unsigned int mismatches;

  initialize_buffers();
  probe_source_pointer = (unsigned int)probe_source;
  probe_destination_pointer = (unsigned int)probe_destination;
  probe_byte_count = (operation == OP_MOVE || operation == OP_DEVMOVE)
                         ? PROBE_BYTES
                         : PROBE_WIDTH;
  probe_rows = PROBE_ROWS;
  probe_source_stride = PROBE_STRIDE;
  probe_destination_stride = PROBE_STRIDE;
  probe_operation = operation;
  probe_sample_count = 0;

  serial_text("\r\nBEGIN ");
  serial_text(name);
  serial_text("\r\n");
  probe_active = 1;
  entry();
  probe_active = 0;
  task_delay(2);

  mismatches = verify_result(operation);
  serial_text("mismatches=");
  serial_hex(mismatches);
  serial_text("\r\n");
  print_samples();
}

int main(int argc, char **argv) {
  task_t *sampler;
  unsigned int priority;
  (void)argc;
  (void)argv;

  if (serial_init() != 0) return 1;
  serial_text("STi5512 interruptible-copy probe\r\n");
  priority = hw_probe_current_priority();
  serial_text("main priority=");
  serial_hex(priority);
  serial_text(" (expect 00000001 for LP)\r\n");
  if (priority != 1) {
    serial_text("ERROR: test runner is not low priority\r\n");
    asc_uninit(serial_device);
    return 1;
  }

  sampler = task_create(sampler_task, 0, 4096, 0, "shadow_sampler",
                        task_flags_high_priority_process);
  if (sampler == 0) {
    serial_text("ERROR: unable to create high-priority sampler\r\n");
    asc_uninit(serial_device);
    return 1;
  }
  task_delay(2);

  run_case(OP_MOVE, "move", hw_probe_move);
  run_case(OP_DEVMOVE, "devmove", hw_probe_devmove);
  run_case(OP_MOVE2DALL, "move2dall", hw_probe_move2dall);
  run_case(OP_MOVE2DNZ, "move2dnonzero", hw_probe_move2dnonzero);
  run_case(OP_MOVE2DZ, "move2dzero", hw_probe_move2dzero);

  probe_stop_sampler = 1;
  task_delay(2);
  serial_text("probe complete\r\n");
  asc_uninit(serial_device);
  return 0;
}
