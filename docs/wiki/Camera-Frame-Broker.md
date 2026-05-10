<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Camera Frame Broker

A simple in-process pub/sub channel for sharing single-channel 8-bit
luminance camera frames across modules. Lives entirely inside the
runtime service process. Modules call into it through the host API.

Code paths:

- Producer/subscribe table: inside `src/xrt/augins/augins_host_api.cpp`.
- API surface: `aug_host_api::publish_camera_frame_y8` and
  `aug_host_api::subscribe_camera_frame` in
  `src/xrt/augins/augins_module_abi.h`.

## Why this exists

Only one ARCore session can exist per process (Google ARCore SDK
limitation). When more than one module wants ARCore camera frames --
for example, the head-pose module owns the session and the
hand-tracking module wants the same frames -- they cannot each
spin up their own ArSession. The broker lets the head-pose module
publish frames and lets the hand-tracking module subscribe.

For non-ARCore camera sources (USB UVC, etc.) the same pattern
applies: one module owns the camera, others consume.

## Frame format

```c
void publish_camera_frame_y8(const uint8_t *y_data,
                             uint32_t width,
                             uint32_t height,
                             uint32_t stride_bytes,
                             int64_t timestamp_ns,
                             const struct aug_camera_intrinsics *intr);
```

- `y_data` -- pointer to width*height pixels, single-channel 8-bit
  luminance (Y plane of YUV). Sourced from ARCore's
  `arcore_min_acquire_camera_image()` Y plane today.
- `stride_bytes` -- bytes between row starts. Typically equal to
  `width` for tightly-packed frames; ARCore sometimes returns
  larger strides for alignment.
- `timestamp_ns` -- monotonic nanosecond timestamp the frame was
  captured at. Used by subscribers to correlate frames with head
  pose.
- `intr` -- camera intrinsics struct, optional. May be `NULL`
  during the first few frames before the producer has read the
  intrinsics from the source.

## Threading

- `publish_camera_frame_y8` is called on the producer's thread.
- The runtime walks the subscriber list synchronously and invokes
  each subscriber's callback ON THE SAME THREAD. Callbacks block the
  producer.
- Subscribers must therefore be FAST. The right pattern is to
  copy the Y plane into a local buffer, push to the module's own
  worker thread for processing, and return. See
  `samples/augins-mercury-handtracking-arcore/mercury_arcore_module.cpp::on_camera_frame`
  for the canonical implementation.
- The pointer arguments are borrowed for the duration of the
  callback; subscribers must memcpy if they need to retain.

## Subscriber lifecycle

```c
void subscribe_camera_frame(aug_camera_frame_cb cb, void *userdata);
```

- Pass `cb = NULL` (with the same `userdata`) to unsubscribe.
- Subscribers are matched by `(cb, userdata)` tuple; you can
  register the same `cb` with different `userdata` to drive
  multiple subscriber slots.
- Cap: 8 simultaneous subscribers. Calls beyond that are rejected
  with a log warning. (No real product needs more than 2-3.)

## Producer policies (suggested)

- Always publish at the camera's native frame rate. Decimation, if
  needed, is the subscriber's job (see Mercury's
  `kDecimateEveryN`).
- Always pass `intr` once it is known. Subscribers can drop frames
  with `NULL intr` without misbehaving.
- Stop publishing when the source camera shuts down. Do not
  publish stale frames.

## Subscriber policies (required)

- Treat the callback as ISR-like. Copy what you need; return.
- Tolerate `NULL intr` by either skipping the frame or processing
  it without the intrinsics-sensitive parts.
- Tolerate frame rate changes (the producer might decimate, or the
  camera source might drop).
- Unsubscribe in `aug_runtimeFinished`.

## Lifecycle and ordering

The dispatch table for the broker is built lazily; the first
`subscribe_camera_frame` call allocates state. The producer's
`publish_camera_frame_y8` is a no-op until at least one subscriber
exists.

It is safe for a subscriber to register from `aug_onConnect` (the
client has just connected; the producer's worker is presumably
already running). It is also safe for the subscriber to register
later, e.g. from a hand-tracker callback the first time the client
queries hand joints (the lazy-subscribe pattern).

## What the broker is NOT

- **It is not a frame bus.** No queueing, no ring buffer, no
  back-pressure. The producer publishes; subscribers consume
  synchronously or drop.
- **It is not a video pipeline.** No format conversion, no
  resampling, no colour space handling. Y8 in, Y8 out.
- **It is not safe across processes.** Modules currently all live
  in the runtime service process; the broker uses in-process
  function pointers. If multi-process module isolation lands later,
  the broker will need a redesign.

## See also

- [Host API Reference](Host-API-Reference.md) for the function
  signatures.
- [ARCore Module Reference](ARCore-Module-Reference.md) for the
  current sole producer.
- [Mercury Module Reference](Mercury-Module-Reference.md) for the
  current sole subscriber.
