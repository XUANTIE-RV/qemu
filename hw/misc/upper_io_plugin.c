// This file implements MMIO communications with the upper PC.
// Parameters read from params.txt are stored in the storage space
// and can be read by the executable. The data sent by the executable
// will be displayed in the console.

#include "qemu/osdep.h"
#include "qapi/error.h" // provide error_fatal() handler
#include "hw/sysbus.h" // provide all sysbus registering func
#include "hw/misc/upper_io_plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TYPE_UPPER_IO_PLUGIN "upper_io_plugin"
typedef struct UpperIOPluginState UpperIOPluginState;
DECLARE_INSTANCE_CHECKER(UpperIOPluginState,
                            UPPER_IO_PLUGIN, TYPE_UPPER_IO_PLUGIN)

#define STORAGE_SIZE 0x10000
#define ARG_SIZE sizeof(double)
#define MAX_NUM_ARGS 0x2000
#define PCIE_OFFSET 0x20000
#define RESET_OFFSET 0xFFFC

// internal state of the plugin
struct UpperIOPluginState {
  SysBusDevice parent_obj;
  MemoryRegion iomem;
  int num_args;
  uint8_t storage[STORAGE_SIZE];
  FILE * fout;
};

// RISC-V executable reading data from upper IO via MMIO
static uint64_t upper_io_plugin_read(void *opaque, hwaddr addr,
                                       unsigned int size) {
  UpperIOPluginState *s = opaque;

  // load from pre-loaded parameter address space
  if (addr + size > STORAGE_SIZE) {
    fprintf(stderr, "Load address 0x%lx and length %u out of bound\n",
               (long)addr, size);
    return 0;
  }
  if (addr + size > s->num_args * ARG_SIZE) {
    fprintf(stderr,
        "Load address 0x%lx and length %u not written. Expecting undefined "
        "behavior\n",
        (long)addr, size);
  }
  switch (size) {
    case 1:
      return s->storage[addr];
      break;
    case 2:
      return *((uint16_t *) (s->storage + addr));
      break;
    case 4:
      return *((uint32_t *) (s->storage + addr));
      break;
    case 8:
      return *((uint64_t *) (s->storage + addr));
      break;

    default:
      fprintf(stderr, "Unsupported length of loading %lu\n", (long) size);
      return 0;
  }
  return 0;
}

// RISC-V executable writing data to upper IO via MMIO
static void upper_io_plugin_write(void *opaque, hwaddr addr,
               uint64_t data, unsigned int size) {
  UpperIOPluginState *s = opaque;

  if ((addr + size > STORAGE_SIZE && addr < STORAGE_SIZE) ||
      (addr >= STORAGE_SIZE && addr < PCIE_OFFSET)) {
    fprintf(stderr, "Store address 0x%lx and length %u out of bound\n",
            (long)addr, size);
    return;
  }

  // write to pre-allocated parameter address space
  if (addr + size <= STORAGE_SIZE) {
    switch (size) {
      case 1:
        s->storage[addr] = data;
        break;
      case 2:
        *((uint16_t *) (s->storage + addr)) = data;
        break;
      case 4:
        *((uint32_t *) (s->storage + addr)) = data;
        break;
      case 8:
        *((uint64_t *) (s->storage + addr)) = data;
        break;
      default:
        fprintf(stderr, "Unsupported length of storing %lu\n", (long) size);
    }
    return;
  }

  // write to upper PC
  addr -= PCIE_OFFSET;
  if (addr == RESET_OFFSET) {
    fprintf(stderr, "Simulation finished. Calling upper PC for reset.\n");
    fclose(s->fout);
    s->fout = fopen("/yaqcs-arch/simulator/exit_code.txt", "w");
  }
  fprintf(stderr, "Output to upper PC: Address = 0x%lx, length = %u",
           (long)addr, size);
  fprintf(s->fout, "%lu %u", (long)addr, size);
  if (size == 8) {
    fprintf(stderr, ", value = %lf\n", *(const double*) &data);
    fprintf(s->fout, " %lf\n", *(const double*) &data);
  } else if (size == 4) {
    fprintf(stderr, ", value = %d\n", (uint32_t) data);
    fprintf(s->fout, " %d\n", (uint32_t) data);
  } else if (size == 1) {
    fprintf(stderr, ", value = %d\n", (uint8_t) data);
    fprintf(s->fout, " %d\n", (uint8_t) data);
  } else {
    fprintf(stderr, "\n");
    fprintf(s->fout, "\n");
  }
  if (addr == RESET_OFFSET)
    fclose(s->fout);
}

static const MemoryRegionOps upper_io_plugin_ops = {
  .read = upper_io_plugin_read,
  .write = upper_io_plugin_write,
  .endianness = DEVICE_NATIVE_ENDIAN,
  .impl.max_access_size = 8,
};

static void upper_io_plugin_instance_init(Object *obj) {
  UpperIOPluginState *s = UPPER_IO_PLUGIN(obj);

  // allocate memory map region
  memory_region_init_io(&s->iomem, obj, &upper_io_plugin_ops, s,
                           TYPE_UPPER_IO_PLUGIN, 0x40000);
  sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

  // read parameters from .txt file
  FILE * fin = fopen("params.txt", "r");
  if (fin) {
    int x = fscanf(fin, "%d", &s->num_args);
    if (x == EOF)
      fprintf(stderr, "Cannot read the number of parameters.\n");
    fprintf(stderr, "Number of arguments: %d\n", s->num_args);
    if (s->num_args > MAX_NUM_ARGS) {
      fprintf(stderr, "Number of arguments %d exceeds capacity\n", s->num_args);
    }
    for (int i = 0; i < s->num_args; i++) {
      double arg;
      int x = fscanf(fin, "%lf", &arg);
      if (x == EOF)
        fprintf(stderr, "Cannot read parameter %d.\n", i);
      fprintf(stderr, "New argument loaded: %lf\n", arg);
      *(double*)(s->storage + ARG_SIZE * i) = arg;
    }
    fclose(fin);
  }
  s->fout = fopen("pcie.txt", "w");
}

// create a new type to define the info related to our device
static const TypeInfo upper_io_plugin_info = {
  .name = TYPE_UPPER_IO_PLUGIN,
  .parent = TYPE_SYS_BUS_DEVICE,
  .instance_size = sizeof(UpperIOPluginState),
  .instance_init = upper_io_plugin_instance_init,
};

static void upper_io_plugin_register_types(void) {
    type_register_static(&upper_io_plugin_info);
}

type_init(upper_io_plugin_register_types)

// create the Upper IO Plugin device
DeviceState *upper_io_plugin_create(hwaddr addr) {
  DeviceState *dev = qdev_new(TYPE_UPPER_IO_PLUGIN);
  sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
  sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);
  return dev;
}
