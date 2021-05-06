# WebCoral demo

This is a project to demonstrate how to use the
[Coral USB Accelerator](https://coral.ai/products/accelerator/) from the Chrome
browser with [WebUSB](https://wicg.github.io/webusb/).

Currently, this demo is verified to work only on Linux and macOS.

## Web Server Setup

Clone this repo with submodules:
```
git clone --recurse-submodules https://github.com/google-coral/webcoral
```

Switch directory:
```
cd webcoral
```

Download `.tflite` model files to `site/models/`:
```
make download
```

Build WASM files inside a Docker container:
```
DOCKER_SHELL_COMMAND="make COMPILATION_MODE=opt wasm" make docker-shell
```

Run local web server using python:
```
make server
```

Server is listening on port `8000`.

## System Setup

On **macOS**, you don't need to install anything else.

On **Linux**, you need to install device rules to make the Coral USB Accelerator
visible in Chrome. You probably already have them installed if you are using
Coral products. If not, this repo has a bash script to install the device rules:
```
scripts/linux_device_rules.sh install
```
and corresponding uninstall command if needed:
```
scripts/linux_device_rules.sh uninstall
```

## Device Setup

To use the USB Accelerator from the web browser, you need to update the USB
Accelerator's firmware as follows. The firmware is usually automatically flashed
by [libedgetpu](https://github.com/google-coral/libedgetpu) library when using
C++ or Python programs, but that’s not the case from the browser.

If you run `lsusb` command right after plugging USB device in, you'll see:
```
Bus 001 Device 008: ID 1a6e:089a Global Unichip Corp.
```

This means firmware is not flashed yet. It is possible to flash firmware
directly from Chrome or using command line.

To flash firmware from command line:
```
make reset
```

To flash from Chrome browser, point it to http://localhost:8000/ and press
`Flash Device Firmware` button. WARNING: this only works on Linux now.
There is an [issue](https://crbug.com/1189418) on macOS. It is already fixed and
the fix will be available in Chrome 91.

Either way, you should now verify it's flashed by again running `lsusb` and you
should see:
```
Bus 001 Device 009: ID 18d1:9302 Google Inc.
```
which means that device is ready to use.


## Demo

Open Chrome at http://localhost:8000/. Choose the model you want to run and
press the **Initialize Interpreter** button. Selecting an Edge TPU model then
requires you to select the USB Accelerator in the dialog; selecting a CPU model
will be ready immediately. At this moment, you can run inference on any local
image file by pressing the **Choose Image File** button.
