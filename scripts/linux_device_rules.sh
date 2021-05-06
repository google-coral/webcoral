#!/bin/bash
#
# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
set -e

readonly RULES_FILE=/etc/udev/rules.d/99-edgetpu-accelerator.rules

if [[ ! -x "$(command -v udevadm)" ]]; then
  echo "Your system does not support device rules"
  exit 1
fi

if [[ "${EUID}" != 0 ]]; then
  echo "Please use sudo to run as root"
  exit 1
fi

if [[ "$1" == "install" ]]; then
  cat << EOF > "${RULES_FILE}"
SUBSYSTEM=="usb",ATTRS{idVendor}=="1a6e",ATTRS{idProduct}=="089a",GROUP="plugdev"
SUBSYSTEM=="usb",ATTRS{idVendor}=="18d1",ATTRS{idProduct}=="9302",GROUP="plugdev"
EOF
  udevadm control --reload-rules && udevadm trigger
  cat "${RULES_FILE}"
elif [[ "$1" == "uninstall" ]]; then
  rm -f "${RULES_FILE}"
  udevadm control --reload-rules && udevadm trigger
else
  echo "$0 install|uninstall"
  exit 1
fi

