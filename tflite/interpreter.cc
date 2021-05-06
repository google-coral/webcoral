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
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

#include <emscripten.h>

#include "tflite/public/edgetpu_c.h"

#include "tensorflow/lite/model_builder.h"
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/interpreter_builder.h"
#include "tensorflow/lite/kernels/register.h"  // BuiltinOpResolver

#include "tflite/queue.h"

namespace {

constexpr char kEdgeTpuCustomOp[] = "edgetpu-custom-op";
constexpr int kExit = -1;

using DelegatePtr = std::unique_ptr<TfLiteDelegate,
                                    decltype(&edgetpu_free_delegate)>;

bool HasCustomOp(const tflite::FlatBufferModel& model, const char* name) {
  const auto* opcodes = model->operator_codes();
  if (!opcodes) return false;

  for (const auto* opcode : *opcodes) {
    auto* custom_op = opcode->custom_code();
    if (custom_op && std::strcmp(name, custom_op->c_str()) == 0) return true;
  }

  return false;
}

class Interpreter {
 public:
  Interpreter(): thread_([this]() {
    while (true) {
      if (auto cmd = queue_.Pop(250/*ms*/)) {
        if (cmd.value() == kExit) break;
        auto result = Invoke();
        MAIN_THREAD_ASYNC_EM_ASM({Module['invokeDone']($0, $1);},
                                 cmd.value(), result);
      }
    }
  }) {}

  ~Interpreter() {
    queue_.Push(kExit);
    thread_.join();
  }

 public:
  bool Init(const char* filename, int verbosity) {
    auto model = tflite::FlatBufferModel::BuildFromFile(filename);
    if (!model) {
      std::cerr << "[ERROR] Cannot load model" << std::endl;
      return false;
    }
    return Init(std::move(model), verbosity);
  }

  bool Init(const char* model_buffer, size_t model_buffer_size, int verbosity) {
    auto model = tflite::FlatBufferModel::BuildFromBuffer(model_buffer,
                                                          model_buffer_size);
    if (!model) {
      std::cerr << "[ERROR] Cannot load model" << std::endl;
      return false;
    }
    return Init(std::move(model), verbosity);
  }

  bool Init(std::unique_ptr<tflite::FlatBufferModel> model, int verbosity) {
    // Model
    model_ = std::move(model);

    tflite::ops::builtin::BuiltinOpResolver resolver;
    if (tflite::InterpreterBuilder(*model_, resolver)(&interpreter_) != kTfLiteOk) {
      std::cerr << "[ERROR] Cannot create interpreter" << std::endl;
      return false;
    }

    if (HasCustomOp(*model_, kEdgeTpuCustomOp)) {
      edgetpu_verbosity(verbosity);

      size_t num_devices;
      std::unique_ptr<edgetpu_device, decltype(&edgetpu_free_devices)>
        devices(edgetpu_list_devices(&num_devices), &edgetpu_free_devices);
      if (num_devices < 1) {
        std::cerr << "[ERROR] Edge TPU is not connected" << std::endl;
        return false;
      }

      auto& device = devices.get()[0];
      edgetpu_option option = {"Usb.AlwaysDfu", "False"};
      DelegatePtr delegate(edgetpu_create_delegate(device.type, device.path,
                                                   &option, 1),
                           edgetpu_free_delegate);
      if (interpreter_->ModifyGraphWithDelegate(std::move(delegate)) != kTfLiteOk) {
        std::cerr << "[ERROR] Cannot apply EdgeTPU delegate" << std::endl;
        return false;
      }
    }

    if (interpreter_->AllocateTensors() != kTfLiteOk) {
      std::cerr << "[ERROR] Cannot allocated tensors" << std::endl;
      return false;
    }

    return true;
  }

 public:
  size_t NumInputs() const {
    return interpreter_->inputs().size();
  }

  void* InputBuffer(size_t tensor_index) const {
    return interpreter_->input_tensor(tensor_index)->data.data;
  }

  const size_t NumInputDims(size_t tensor_index) const {
    return interpreter_->input_tensor(tensor_index)->dims->size;
  }

  const size_t InputDim(size_t tensor_index, size_t dim) const {
    return interpreter_->input_tensor(tensor_index)->dims->data[dim];
  }

 public:
  size_t NumOutputs() const {
    return interpreter_->outputs().size();
  }

  const void* OutputBuffer(size_t tensor_index) const {
    return interpreter_->output_tensor(tensor_index)->data.data;
  }

  const size_t NumOutputDims(size_t tensor_index) const {
    return interpreter_->output_tensor(tensor_index)->dims->size;
  }

  const int OutputDim(size_t tensor_index, size_t dim) const {
    return interpreter_->output_tensor(tensor_index)->dims->data[dim];
  }

 public:
  bool Invoke() {
    if (interpreter_->Invoke() != kTfLiteOk) {
      std::cerr << "[ERROR] Cannot invoke interpreter" << std::endl;
      return false;
    }
    return true;
  }

  void InvokeAsync(size_t id) {
    queue_.Push(id);
  }

 private:
  std::unique_ptr<tflite::FlatBufferModel> model_;
  std::unique_ptr<tflite::Interpreter> interpreter_;
  Queue<int> queue_;
  std::thread thread_;
};

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
void* interpreter_create(const char* model_buffer, size_t model_buffer_size,
                         int verbosity) {
  auto* interpreter = new Interpreter();
  if (!interpreter->Init(model_buffer, model_buffer_size, verbosity)) {
    delete interpreter;
    return nullptr;
  }
  return interpreter;
}

EMSCRIPTEN_KEEPALIVE
void interpreter_destroy(void* p) {
  delete reinterpret_cast<Interpreter*>(p);
}

// Inputs
EMSCRIPTEN_KEEPALIVE
size_t interpreter_num_inputs(void* interpreter) {
  return reinterpret_cast<Interpreter*>(interpreter)->NumInputs();
}

EMSCRIPTEN_KEEPALIVE
void* interpreter_input_buffer(void* interpreter, size_t tensor_index) {
  return reinterpret_cast<Interpreter*>(interpreter)->InputBuffer(tensor_index);
}

EMSCRIPTEN_KEEPALIVE
size_t interpreter_num_input_dims(void *interpreter, size_t tensor_index) {
  return reinterpret_cast<Interpreter*>(interpreter)->NumInputDims(tensor_index);
}

EMSCRIPTEN_KEEPALIVE
size_t interpreter_input_dim(void *interpreter, size_t tensor_index, size_t dim) {
  return reinterpret_cast<Interpreter*>(interpreter)->InputDim(tensor_index, dim);
}

// Outputs
EMSCRIPTEN_KEEPALIVE
size_t interpreter_num_outputs(void* interpreter) {
  return reinterpret_cast<Interpreter*>(interpreter)->NumOutputs();
}

EMSCRIPTEN_KEEPALIVE
const void* interpreter_output_buffer(void* interpreter, size_t tensor_index) {
  return reinterpret_cast<Interpreter*>(interpreter)->OutputBuffer(tensor_index);
}

EMSCRIPTEN_KEEPALIVE
size_t interpreter_num_output_dims(void *interpreter, size_t tensor_index) {
  return reinterpret_cast<Interpreter*>(interpreter)->NumOutputDims(tensor_index);
}

EMSCRIPTEN_KEEPALIVE
size_t interpreter_output_dim(void *interpreter, size_t tensor_index, size_t dim) {
  return reinterpret_cast<Interpreter*>(interpreter)->OutputDim(tensor_index, dim);
}

EMSCRIPTEN_KEEPALIVE
void interpreter_invoke_async(void *interpreter, size_t id) {
  return reinterpret_cast<Interpreter*>(interpreter)->InvokeAsync(id);
}

}  // extern "C"
