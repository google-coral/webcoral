load("@emsdk//emscripten_toolchain:wasm_rules.bzl", "wasm_cc_binary")

cc_binary(
    name = "interpreter",
    srcs = ["interpreter.cc", "libusb.cc", "queue.h"],
    deps = [
      "@libedgetpu//tflite/public:edgetpu_c",
      "@libedgetpu//tflite/public:oss_edgetpu_direct_usb",
      "@org_tensorflow//tensorflow/lite:framework",
      "@org_tensorflow//tensorflow/lite/kernels:builtin_ops",  # BuiltinOpResolver
    ]
)

wasm_cc_binary(
    name = "interpreter-wasm",
    cc_target = ":interpreter",
)
