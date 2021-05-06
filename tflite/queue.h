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
#ifndef TFLITE_QUEUE_H_
#define TFLITE_QUEUE_H_

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

template<typename T>
class Queue {
 public:
  Queue() {}

  void Push(T t) {
    {
      std::lock_guard<std::mutex> lock(m_);
      q_.push(t);
    }
    cv_.notify_one();
  }

  std::optional<T> Pop(int timeout_ms) {
    std::unique_lock<std::mutex> lock(m_);
    if (!cv_.wait_for(lock,
                      std::chrono::milliseconds(timeout_ms),
                      [this]{ return !q_.empty(); }))
      return std::nullopt;

    auto t = q_.front();
    q_.pop();
    return t;
  }

 private:
  std::mutex m_;
  std::condition_variable cv_;
  std::queue<T> q_;
};

#endif  // TFLITE_QUEUE_H_
