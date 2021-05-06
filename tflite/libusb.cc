// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <cstdio>
#include <iostream>

#include <emscripten.h>
#include <libusb-1.0/libusb.h>

#include "tflite/queue.h"

#define LIBUSB_MAJOR 1
#define LIBUSB_MINOR 0
#define LIBUSB_MICRO 24
#define LIBUSB_NANO 0
#define LIBUSB_RC ""

// #define LIBUSB_ENABLE_LOG
#ifdef LIBUSB_ENABLE_LOG
#define LIBUSB_LOG(...)           \
  do {                            \
    fprintf(stdout, __VA_ARGS__); \
    fprintf(stdout, "\n");        \
    fflush(stdout);               \
  } while (false)
#else  // LIBUSB_ENABLE_LOG
#define LIBUSB_LOG(...)
#endif  // LIBUSB_ENABLE_LOG

struct libusb_device {
  uint8_t bus_number;
  uint8_t port_number;
  struct libusb_context *ctx;
  struct libusb_device_descriptor descriptor;
};

struct libusb_context {
  Queue<libusb_transfer*> completed_transfers;
  libusb_device dev;
};

struct libusb_device_handle {
  struct libusb_device *dev;
};

static const struct libusb_version kVersion = {
  LIBUSB_MAJOR,
  LIBUSB_MINOR,
  LIBUSB_MICRO,
  LIBUSB_NANO,
  LIBUSB_RC,
  "http://libusb.info"
};

static int js_request_device(struct libusb_device *dev) {
  return MAIN_THREAD_EM_ASM_INT({
    return Asyncify.handleAsync(async () => {
      // Bus 001 Device 005: ID 1a6e:089a Global Unichip Corp.
      // Bus 002 Device 007: ID 18d1:9302 Google Inc.
      let options = {'filters': [{'vendorId': 0x18d1, 'productId': 0x9302}]};
      let devices = await navigator.usb.getDevices();
      if (!devices.length) {
        try {
          let device = await navigator.usb.requestDevice(options);
          devices = [device];
        } catch (error) {
          devices = []
        }
      }

      if (devices.length === 1) {
        let d = devices[0];
        _fill_device($0,
                   /*bcdUSB=*/(d.usbVersionMajor << 8) | d.usbVersionMinor,
                   /*bDeviceClass=*/d.deviceClass,
                   /*bDeviceSubClass=*/d.deviceSubClass,
                   /*bDeviceProtocol=*/d.deviceProtocol,
                   /*idVendor=*/d.vendorId,
                   /*idProduct=*/d.productId,
                   /*bcdDevice=*/(d.deviceVersionMajor << 8) | ((d.deviceVersionMinor << 4) | d.deviceVersionSubminor),
                   /*bNumConfigurations=*/d.configurations.length);
        this.libusb_device = devices[0];
        return 1;
      }
      return 0;
    });
  }, dev);
}

static int js_control_transfer(uint8_t bmRequestType, uint8_t bRequest,
                               uint16_t wValue, uint16_t wIndex, uint8_t *data,
                               uint16_t wLength, unsigned int timeout) {
  return MAIN_THREAD_EM_ASM_INT({
    return Asyncify.handleAsync(async () => {
      let bmRequestType = $0;
      let bRequest = $1;
      let wValue = $2;
      let wIndex = $3;
      let data = $4;
      let wLength = $5;
      let timeout = $6;

      let setup = {
        'requestType': ['standard', 'class', 'vendor'][(bmRequestType & 0x60) >> 5],
        'recipient': ['device', 'interface', 'endpoint', 'other'][(bmRequestType & 0x1f)],
        'request': bRequest,
        'value': wValue,
        'index': wIndex,
      };

      let dir_in = (bmRequestType & 0x80) == 0x80;
      if (dir_in) {
        let result = await this.libusb_device.controlTransferIn(setup, wLength);
        if (result.status != 'ok') {
          console.error('controlTransferIn', result);
          return 0;
        }

        let view = new Uint8Array(result.data.buffer);
        writeArrayToMemory(view, data);
        return result.data.buffer.byteLength;
      } else {
        let buffer = new Uint8Array(wLength);
        for (let i = 0; i < wLength; ++i)
          buffer[i] = getValue(data + i, 'i8');

        let result = await this.libusb_device.controlTransferOut(setup, buffer);
        if (result.status != 'ok') {
          console.error('controlTransferOut', result);
          return 0;
        }
        return result.bytesWritten;
      }
    });
  }, bmRequestType, bRequest, wValue, wIndex, data, wLength, timeout);
}

static void print(const char* line) { std::cout << line << std::endl; }

static void print(const char* line, int value, bool hex=false) {
  if (hex)
    std::cout << line << std::hex << value << std::dec << std::endl;
  else
    std::cout << line << value << std::endl;
}

static void print_device(const struct libusb_device* dev) {
  print("USB Device");
  print("  Bus: ",  dev->bus_number);
  print("  Port: ", dev->port_number);
  print("  Descriptor");
  print("    bLength: ",            dev->descriptor.bLength);
  print("    bDescriptorType: ",    dev->descriptor.bDescriptorType);
  print("    bcdUSB: 0x",           dev->descriptor.bcdUSB, /*hex=*/true);
  print("    bDeviceClass: ",       dev->descriptor.bDeviceClass);
  print("    bDeviceSubClass: ",    dev->descriptor.bDeviceSubClass);
  print("    bDeviceProtocol: ",    dev->descriptor.bDeviceProtocol);
  print("    bMaxPacketSize0: ",    dev->descriptor.bMaxPacketSize0);
  print("    idVendor: 0x",         dev->descriptor.idVendor, /*hex=*/true);
  print("    idProduct: 0x",        dev->descriptor.idProduct, /*hex=*/true);
  print("    bcdDevice: 0x",        dev->descriptor.bcdDevice, /*hex=*/true);
  print("    iManufacturer: ",      dev->descriptor.iManufacturer);
  print("    iProduct: ",           dev->descriptor.iProduct);
  print("    iSerialNumber: ",      dev->descriptor.iSerialNumber);
  print("    bNumConfigurations: ", dev->descriptor.bNumConfigurations);
}

extern "C" {

int libusb_init(libusb_context **ctx) {
  LIBUSB_LOG("libusb_init");

  if (!EM_ASM_INT(return navigator.usb !== undefined))
    return LIBUSB_ERROR_NOT_SUPPORTED;

  *ctx = new libusb_context;
  return LIBUSB_SUCCESS;
}

void LIBUSB_CALL libusb_exit(libusb_context *ctx) {
  LIBUSB_LOG("libusb_exit");
  delete ctx;
}

void LIBUSB_CALL libusb_set_debug(libusb_context *ctx, int level) {
  LIBUSB_LOG("libusb_set_debug [NOT IMPLEMENTED]");
}

const struct libusb_version* LIBUSB_CALL libusb_get_version() {
  LIBUSB_LOG("libusb_get_version");
  return &kVersion;
}

struct libusb_transfer* LIBUSB_CALL libusb_alloc_transfer(int iso_packets) {
  LIBUSB_LOG("libusb_alloc_transfer");

  size_t size = sizeof(struct libusb_transfer) +
                sizeof(struct libusb_iso_packet_descriptor) * iso_packets;

  return reinterpret_cast<libusb_transfer*>(calloc(1, size));
}

int LIBUSB_CALL libusb_submit_transfer(struct libusb_transfer *transfer) {
  LIBUSB_LOG("libusb_submit_transfer");

  bool dir_in = (transfer->endpoint & 0x80) == 0x80;
  uint8_t endpoint = transfer->endpoint & 0x7f;

  switch (transfer->type) {
    case LIBUSB_TRANSFER_TYPE_BULK:
    case LIBUSB_TRANSFER_TYPE_INTERRUPT:
      if (dir_in) {
        MAIN_THREAD_ASYNC_EM_ASM({
          this.libusb_device.transferIn($0, $2).then(function(result) {
            var data = new Uint8Array(result.data.buffer);
            writeArrayToMemory(data, $1);
            _set_transfer_completed($3, data.length);
          }).catch(function(error) {
            console.error('transferIn', error);
            _set_transfer_error($3);
          });
        }, endpoint, transfer->buffer, transfer->length, transfer);
      } else {
        MAIN_THREAD_ASYNC_EM_ASM({
          var data = new Uint8Array($2);
          for(let i = 0; i < $2; ++i)
            data[i] = getValue($1 + i, 'i8');

          this.libusb_device.transferOut($0, data).then(function(result) {
            _set_transfer_completed($3, result.bytesWritten);
          }).catch(function(error) {
            console.error('transferOut', error);
            _set_transfer_error($3);
          });
        }, endpoint, transfer->buffer, transfer->length, transfer);
      }
      break;
    default:
      LIBUSB_LOG("Transfer type not implemented: %u\n", transfer->type);
      return LIBUSB_ERROR_IO;
  }
  return LIBUSB_SUCCESS;
}

int LIBUSB_CALL libusb_cancel_transfer(struct libusb_transfer *transfer) {
  LIBUSB_LOG("libusb_cancel_transfer [NOT IMPLEMENTED]");
  return 0;
}

void LIBUSB_CALL libusb_free_transfer(struct libusb_transfer *transfer) {
  LIBUSB_LOG("libusb_free_transfer");
  free(transfer);
}

uint8_t libusb_get_port_number(libusb_device * dev) {
  LIBUSB_LOG("libusb_get_port_number");
  return dev->port_number;
}

int libusb_get_port_numbers(libusb_device *dev,uint8_t *port_numbers, int port_numbers_len) {
  LIBUSB_LOG("libusb_get_port_numbers");

  if (port_numbers_len <= 0)
    return LIBUSB_ERROR_INVALID_PARAM;

  if (port_numbers_len < 1)
    return LIBUSB_ERROR_OVERFLOW;

  port_numbers[0] = dev->port_number;
  return 1;
}

int LIBUSB_CALL libusb_handle_events(libusb_context *ctx) {
  while (true) {
    if (auto item = ctx->completed_transfers.Pop(25/*ms*/)) {
      auto* transfer = item.value();
      transfer->callback(transfer);
    } else {
      break;
    }
  }
  return LIBUSB_SUCCESS;
}

int LIBUSB_CALL libusb_reset_device(libusb_device_handle *dev) {
  LIBUSB_LOG("libusb_reset_device");

  return MAIN_THREAD_EM_ASM_INT({
    return Asyncify.handleAsync(async () => {
      try {
        await this.libusb_device.reset();
        return 0;  // LIBUSB_SUCCESS
      } catch (error) {
        console.error('reset', error);
        // TODO: return -1;  // LIBUSB_ERROR_IO
        return 0;  // LIBUSB_SUCCESS
      }
    });
  });
}

int LIBUSB_CALL libusb_control_transfer(libusb_device_handle *dev_handle,
    uint8_t request_type, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
    unsigned char *data, uint16_t wLength, unsigned int timeout) {
  LIBUSB_LOG("libusb_control_transfer");
  return js_control_transfer(request_type, bRequest, wValue, wIndex, data,
                             wLength, timeout);
}

int LIBUSB_CALL libusb_bulk_transfer(libusb_device_handle *dev_handle,
    unsigned char endpoint, unsigned char *data, int length,
    int *actual_length, unsigned int timeout) {
  LIBUSB_LOG("libusb_bulk_transfer [NOT IMPLEMENTED]");
  return 0;
}

int LIBUSB_CALL libusb_interrupt_transfer(libusb_device_handle *dev_handle,
    unsigned char endpoint, unsigned char *data, int length,
    int *actual_length, unsigned int timeout) {
  LIBUSB_LOG("libusb_interrupt_transfer [NOT IMPLEMENTED]");
  return 0;
}

int LIBUSB_CALL libusb_open(libusb_device *dev, libusb_device_handle **handle) {
  LIBUSB_LOG("libusb_open");
  // TODO: check dev->descriptor.idVendor and dev->descriptor.idProduct
  MAIN_THREAD_EM_ASM_INT({
    return Asyncify.handleAsync(async () => {
      await this.libusb_device.open();
      try {
        // TODO: Avoid resetting on open.
        await this.libusb_device.reset();
      } catch (error) {
        console.error('reset', error);
      }
      return 1;
    });
  });

  if (handle) {
    *handle = new libusb_device_handle;
    (*handle)->dev = dev;
  }

  return LIBUSB_SUCCESS;
}

void LIBUSB_CALL libusb_close(libusb_device_handle *dev_handle) {
  LIBUSB_LOG("libusb_close");

  MAIN_THREAD_EM_ASM_INT({
    Asyncify.handleAsync(async () => {
      return await this.libusb_device.close();
    });
  });

  delete dev_handle;
}

libusb_device * LIBUSB_CALL libusb_get_device(libusb_device_handle *dev_handle) {
  LIBUSB_LOG("libusb_get_device");
  return dev_handle->dev;
}

ssize_t LIBUSB_CALL libusb_get_device_list(libusb_context *ctx, libusb_device ***list) {
  LIBUSB_LOG("libusb_get_device_list");

  auto* dev = &ctx->dev;
  dev->ctx = ctx;

  if (js_request_device(dev)) {
    print_device(dev);
    *list = new libusb_device*[]{dev, nullptr};
    return 1;
  } else {
    *list = new libusb_device*[]{nullptr};
    return 0;
  }
}

void LIBUSB_CALL libusb_free_device_list(libusb_device* *list, int unref_devices) {
  LIBUSB_LOG("libusb_free_device_list: unref_devices=%d", unref_devices);

  int i = 0;
  while (true) {
    libusb_device *device = list[i++];
    if (device == nullptr) break;
    delete device;
  }

  delete [] list;
}

int LIBUSB_CALL libusb_get_device_descriptor(libusb_device *dev, struct libusb_device_descriptor *desc) {
  LIBUSB_LOG("libusb_get_device_descriptor");
  *desc = dev->descriptor;
  return 0;
}

int LIBUSB_CALL libusb_get_device_speed(libusb_device *dev) {
  LIBUSB_LOG("libusb_get_device_speed");
  return LIBUSB_SPEED_SUPER;  // TODO: Implement.
}

uint8_t LIBUSB_CALL libusb_get_bus_number(libusb_device *dev) {
  LIBUSB_LOG("libusb_get_bus_number");
  return dev->bus_number;
}

int LIBUSB_CALL libusb_set_configuration(libusb_device_handle *dev,
                                         int configuration) {
  LIBUSB_LOG("libusb_set_configuration [NOT IMPLEMENTED]");
  return 0;
}

int LIBUSB_CALL libusb_claim_interface(libusb_device_handle *dev,
                                       int interface_number) {
  LIBUSB_LOG("libusb_claim_interface: interface_number=%d", interface_number);
  return MAIN_THREAD_EM_ASM_INT({
    return Asyncify.handleAsync(async () => {
      try {
        await this.libusb_device.claimInterface($0);
        return 0;  // LIBUSB_SUCCESS
      } catch (error) {
        console.error('claimInterface:', error);
        return -1;  // LIBUSB_ERROR_IO
      }
    });
  }, interface_number);
}

int LIBUSB_CALL libusb_release_interface(libusb_device_handle *dev,
                                         int interface_number) {
  LIBUSB_LOG("libusb_release_interface: interface_number=%d", interface_number);
  return MAIN_THREAD_EM_ASM_INT({
    return Asyncify.handleAsync(async () => {
      try {
        await this.libusb_device.releaseInterface($0);
        return 0;  // LIBUSB_SUCCESS
      } catch (error) {
        console.error('releaseInterface:', error);
        return -1;  // LIBUSB_ERROR_IO
      }
    });
  }, interface_number);
}


EMSCRIPTEN_KEEPALIVE
void set_transfer_error(struct libusb_transfer* transfer) {
  LIBUSB_LOG("set_tansfer_error: transfer=%p", transfer);
  libusb_context* ctx = transfer->dev_handle->dev->ctx;

  transfer->status = LIBUSB_TRANSFER_CANCELLED;
  transfer->actual_length = 0;

  ctx->completed_transfers.Push(transfer);
}

EMSCRIPTEN_KEEPALIVE
void set_transfer_completed(struct libusb_transfer* transfer, int actual_length) {
  LIBUSB_LOG("set_tansfer_completed: transfer=%p, actual_length=%d",
             transfer, actual_length);
  libusb_context* ctx = transfer->dev_handle->dev->ctx;

  transfer->status = LIBUSB_TRANSFER_COMPLETED;
  transfer->actual_length = actual_length;

  ctx->completed_transfers.Push(transfer);
}

EMSCRIPTEN_KEEPALIVE
void fill_device(struct libusb_device* dev,
    uint16_t bcdUSB,
    uint8_t bDeviceClass,
    uint8_t bDeviceSubClass,
    uint8_t bDeviceProtocol,
    uint16_t idVendor,
    uint16_t idProduct,
    uint16_t  bcdDevice,
    uint8_t bNumConfigurations) {
  dev->bus_number = 0;
  dev->port_number = 1;

  struct libusb_device_descriptor* d = &dev->descriptor;
  d->bLength = LIBUSB_DT_DEVICE_SIZE;
  d->bDescriptorType = LIBUSB_DT_DEVICE;
  d->bcdUSB = bcdUSB;
  d->bDeviceClass = bDeviceClass;
  d->bDeviceSubClass = bDeviceSubClass;
  d->bDeviceProtocol = bDeviceProtocol;
  d->bMaxPacketSize0 = 64;
  d->idVendor = idVendor;
  d->idProduct = idProduct;
  d->bcdDevice = bcdDevice;
  d->iManufacturer = 1;
  d->iProduct = 2;
  d->iSerialNumber = 3;
  d->bNumConfigurations = bNumConfigurations;
}

}  // extern "C"
