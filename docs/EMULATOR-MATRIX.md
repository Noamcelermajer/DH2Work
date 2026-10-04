# Emulator matrix: the own host on real Android

The own host (`host/`) was pushed into Android emulators and run there, on five different device
profiles. This is the first time the own host has executed inside a real Android user space
rather than under qemu.

Everything is Android 11 (API 30), x86_64, with `-gpu swiftshader_indirect`, which gives the
guest a **real** GL implementation:

```
GLES: Google (Google Inc.), Android Emulator OpenGL ES Translator (Google SwiftShader),
OpenGL ES 3.0 (OpenGL ES 3.0 SwiftShader 4.0.0.1)
```

## Results

| Device profile | Screen | RAM | `nativeInit` | Frames | Instructions | GL calls | Exit |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Pixel-class (dh2test) | 1080x2340 @ 440 | 2 GB | 12,067,612 | 1 | 33,507,843 | 104 / 43 | 0 |
| Nexus 5 | 1080x1920 @ 480 | 2 GB | **9,195,900** | 1 | 30,634,777 | 104 / 43 | 0 |
| Nexus 10 (tablet) | 2560x1600 @ 320 | 3 GB | 12,070,634 | 1 | 33,508,683 | 104 / 43 | 0 |
| Nexus S (small) | 480x800 @ 240 | 1 GB | 12,068,751 | 1 | 33,506,849 | 104 / 43 | 0 |
| Pixel Fold (unfolded) | 2208x1768 @ 420 | 4 GB | 12,067,060 | 1 | 33,505,117 | 104 / 43 | 0 |

Every one: 545 initializers, `nativeInit` returns `0x00000001`, `nativeRender` returns, and the
process exits 0.

The Nexus 5 row is the interesting one. Its `nativeInit` does **2.9M fewer instructions** than the
others, which is the engine taking a different path because it read a different phone model,
manufacturer and screen size through the JNI surface this host answers. That is the device
adaptation the 25 Java method lookups provide, visible as a measurable difference.

## Reproducing

```
host/scripts/emulator-sweep.sh            # boot each AVD, push, run, report
host/scripts/emulator-setup.sh            # create the five AVDs from the installed image
```

The host binary is built for **x86_64 Android** (`host/scripts/build-android.sh` with
`ANDROID_ABI=x86_64`), so it runs natively under the emulator's KVM acceleration while Dynarmic
executes the ARM32 guest. The engine's own APK library and the game cache are pushed to
`/data/local/tmp/dh2`; they are not in this repository.

## What SIGPIPE was

The first three runs all died with exit 141 at the same point, and not because of adb's pipes: it
reproduced with the process fully detached (`setsid`, stdio to a file). The engine opens a socket
for Gameloft Live, `connect` was unimplemented and returned ENOSYS, so the socket stayed open and
the next write raised SIGPIPE, which killed the host.

Two fixes: SIGPIPE is ignored at startup, so a failed connection is an error code rather than a
signal; and `connect` reports ECONNREFUSED, as a device with no route does.

## Still not established

The emulator provides a real GL **implementation**; this host still does not call it. The GL
entry points are answered by the host's own table (correct return values, no rendering), so the
engine builds its pipeline believing it rendered and **nothing is drawn**. Connecting the host's
GL bridge to the platform's `libGLESv2.so` is the next step, and it is what would put pixels on
the screen.

No audio, one guest thread, no timing model, and no real-device run.
