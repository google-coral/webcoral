/**
 * @license
 * Copyright 2021 Google LLC. All Rights Reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * =============================================================================
 */
const tflite = {};

(function() {
  'use strict';

  let nextId = 0;

  function rgbaArrayToRgbArray(rgbaArray) {
    const rgbArray = new Uint8Array(new ArrayBuffer(3 * rgbaArray.length / 4));
    for (let i = 0, j = 0; i < rgbaArray.length; i += 4, j += 3) {
      rgbArray[j + 0] = rgbaArray[i + 0];
      rgbArray[j + 1] = rgbaArray[i + 1];
      rgbArray[j + 2] = rgbaArray[i + 2];
    }
    return rgbArray;
  }

  function callbackKey(id) {
    return 'interpreter_' + id;
  }

  tflite.setRgbInput = function(interpreter, rgbArray, index=0) {
    const shape = interpreter.inputShape(index);
    if (rgbArray.length != shape.reduce((a, b) => a * b))
      throw new Error('Invalid input array size');

    writeArrayToMemory(rgbArray,  interpreter.inputBuffer(index));
  }

  tflite.setRgbaInput = function(interpreter, rgbaArray) {
    tflite.setRgbInput(interpreter, rgbaArrayToRgbArray(rgbaArray));
  }

  tflite.getClassificationOutput = function(interpreter, index=0) {
    const count = interpreter.outputShape(index).reduce((a, b) => a * b);
    const scoresPtr = interpreter.outputBuffer(index) / Module.HEAPU8.BYTES_PER_ELEMENT;
    const scores = Module.HEAPU8.slice(scoresPtr, scoresPtr + count);
    return scores.indexOf(Math.max(...scores));
  }

  tflite.getDetectionOutput = function(interpreter, threshold=0.0) {
    const bboxesPtr = interpreter.outputBuffer(0) / Module.HEAPF32.BYTES_PER_ELEMENT;
    const idsPtr = interpreter.outputBuffer(1) / Module.HEAPF32.BYTES_PER_ELEMENT;
    const scoresPtr = interpreter.outputBuffer(2) / Module.HEAPF32.BYTES_PER_ELEMENT;
    const countPtr = interpreter.outputBuffer(3) / Module.HEAPF32.BYTES_PER_ELEMENT;

    const count = Math.round(Module.HEAPF32[countPtr]);
    const bboxes = Module.HEAPF32.slice(bboxesPtr, bboxesPtr + 4 * count);
    const ids = Module.HEAPF32.slice(idsPtr, idsPtr + count);
    const scores = Module.HEAPF32.slice(scoresPtr, scoresPtr + count);

    const objects = [];
    for (let i = 0; i < count; ++i) {
      if (scores[i] < threshold)
        break;

      objects.push({
        'id': ids[i],
        'score': scores[i],
        'bbox' : {
          'ymin': Math.max(0.0, bboxes[4 * i]),
          'xmin': Math.max(0.0, bboxes[4 * i + 1]),
          'ymax': Math.min(1.0, bboxes[4 * i + 2]),
          'xmax': Math.min(1.0, bboxes[4 * i + 3]),
        },
      });
    }
    return objects;
  }

  tflite.Interpreter = function() {
    this.interpreter_create       = Module.cwrap('interpreter_create',  'number', ['number'], { async: true });
    this.interpreter_destroy      = Module.cwrap('interpreter_destroy', null,     ['number']);

    this.interpreter_num_inputs     = Module.cwrap('interpreter_num_inputs',     'number',   ['number']);
    this.interpreter_input_buffer   = Module.cwrap('interpreter_input_buffer',   'number',   ['number', 'number']);
    this.interpreter_num_input_dims = Module.cwrap('interpreter_num_input_dims', 'number',   ['number', 'number']);
    this.interpreter_input_dim      = Module.cwrap('interpreter_input_dim',      'number',   ['number', 'number', 'number']);

    this.interpreter_num_outputs     = Module.cwrap('interpreter_num_outputs',     'number',   ['number']);
    this.interpreter_output_buffer   = Module.cwrap('interpreter_output_buffer',   'number',   ['number', 'number']);
    this.interpreter_num_output_dims = Module.cwrap('interpreter_num_output_dims', 'number',   ['number', 'number']);
    this.interpreter_output_dim      = Module.cwrap('interpreter_output_dim',      'number',   ['number', 'number', 'number']);

    this.interpreter_invoke_async = Module.cwrap('interpreter_invoke_async', null, ['number', 'number']);

    this.id = nextId++;

    Module['invokeDone'] = function(id, result) {
      const key = callbackKey(id);
      const callback = Module[key];
      if (callback) callback(result);
      delete Module[key];
    };
  }

  tflite.Interpreter.prototype.createFromBuffer = async function(buffer) {
    const model = new Uint8Array(buffer);
    const modelBufferSize = model.length * model.BYTES_PER_ELEMENT;
    const modelBufferPtr = Module._malloc(modelBufferSize);
    Module.HEAPU8.set(model, modelBufferPtr);
    this.interpreter = await this.interpreter_create(modelBufferPtr, modelBufferSize, 0);

    if (this.interpreter == null)
      return false;

    this.input_shapes = [];
    this.input_buffers = [];
    const num_inputs = this.interpreter_num_inputs(this.interpreter);
    for (let ti = 0; ti < num_inputs; ++ti) {
      const shape = [];
      const dims = this.interpreter_num_input_dims(this.interpreter, ti);
      for (let i = 0; i < dims; ++i)
        shape.push(this.interpreter_input_dim(this.interpreter, ti, i));
      this.input_shapes.push(shape);

      const buffer = this.interpreter_input_buffer(this.interpreter, ti);
      this.input_buffers.push(buffer);
    }

    this.output_shapes = [];
    this.output_buffers = [];
    const num_outputs = this.interpreter_num_outputs(this.interpreter);
    for (let ti = 0; ti < num_outputs; ++ti) {
      const shape = [];
      const dims = this.interpreter_num_output_dims(this.interpreter, ti);
      for (let i = 0; i < dims; ++i)
        shape.push(this.interpreter_output_dim(this.interpreter, ti, i));
      this.output_shapes.push(shape);

      const buffer = this.interpreter_output_buffer(this.interpreter, ti);
      this.output_buffers.push(buffer);
    }

    return true;
  }

  tflite.Interpreter.prototype.destroy = function() {
    this.interpreter_destroy(this.interpreter);
  }

  tflite.Interpreter.prototype.numInputs = function() {
    return this.input_shapes.length;
  }

  tflite.Interpreter.prototype.inputBuffer = function(index) {
    return this.input_buffers[index];
  }

  tflite.Interpreter.prototype.inputShape = function(index) {
    return this.input_shapes[index];
  }

  tflite.Interpreter.prototype.numOutputs = function() {
    return this.output_shapes.length;
  }

  tflite.Interpreter.prototype.outputBuffer = function(index) {
    return this.output_buffers[index];
  }

  tflite.Interpreter.prototype.outputShape = function(index) {
    return this.output_shapes[index];
  }

  tflite.Interpreter.prototype.invoke = function() {
    const self = this;
    return new Promise((resolve, reject) => {
      Module[callbackKey(self.id)] = result => {
        (result ? resolve : reject)();
      };
      self.interpreter_invoke_async(self.interpreter, self.id);
    });
  }
})();
