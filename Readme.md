# Multi Audio Broadcast

A small [miniaudio](https://miniaud.io/) based application that captures the default output device and broadcasts its audio to the other available output devices.

## Overview

The application discovers the available playback devices, captures the default output, and writes the captured audio to each other output device.

## Features

- Enumerates available audio output devices.
- Captures the default output rather than a microphone.
- Sends the captured stream to every other initialized output.
- Uses miniaudio for device enumeration and playback.
- Uses 32-bit floating-point stereo audio at 48 kHz.
- Uses a separate ring buffer for each output device.

## Requirements

- A C++ compiler.
- The miniaudio single-header library (`miniaudio.h`).
- At least two available audio output devices.
- On Linux, a running PulseAudio or `pipewire-pulse` service.

## Building

Compile `main.cpp` together with `miniaudio.h`, using the compiler and linker options required by the platform (use the Makefile for convenience ).

```bash
make all
```

## Running

Run the executable:

```bash
./multi-audio-broadcast
```

The program lists the available output devices, skips the default device, and begins forwarding the default device's loopback or monitor audio to the other devices. Press Enter to stop.

## How it works

1. Initializes a miniaudio context.
2. Enumerates playback devices.
3. Creates a playback stream and ring buffer for each non-default device.
4. Captures the default device's output using Windows WASAPI loopback or, on Linux, its PulseAudio/PipeWire monitor source.
5. Writes captured frames to the output ring buffers and drains them independently.
6. Stops and releases all devices on exit.

## Notes

- The application requests 48 kHz, stereo, floating-point streams; device initialization may fail if a device cannot provide the requested configuration.
- The operating system may deny access to unavailable, disconnected, or exclusively locked devices.
- If an output falls too far behind, the application skips buffered audio to limit latency.

## License

This project’s license is not specified. See the project source and the miniaudio license before redistribution.

