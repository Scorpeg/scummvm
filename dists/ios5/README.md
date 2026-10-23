# ScummVM for iOS 5.1

This platform targets jailbroken ARMv7 devices with iPhoneOS SDK 5.1. It reuses the current iOS backend through checked compatibility overrides in `backends/platform/ios5/compat.patch`. The source files for other platforms and engines stay unchanged. Conflicting upstream changes stop the build and identify the override to review.

Use an out-of-source build directory, outside the source checkout:

```sh
mkdir ../build-ios5
cd ../build-ios5
../scummvm/configure --host=ios5 --with-staticlib-prefix="$PREFIX"
make -j6
make ios5deb
```

The iOS5 Docker worker supplies `CC`, `CXX`, `AR`, `RANLIB`, `NM`, `PREFIX`, `IOS_TOOLCHAIN_BIN`, `IOS_SDK_ROOT`, and the matching dependency headers and libraries. `IOS5_REQUIRE_FULL=1` makes configuration and packaging reject missing engines or dependencies, apart from the explicitly reported Macs2 and Watchmaker backend restrictions. `make ios5bundle` produces the app bundle alone. The packaging targets need the complete dependency prefix supplied by that worker.

Install the `.deb` using `dpkg -i`. The package uses gzip archives and a SHA1 jailbreak pseudo-signature; it needs no Apple signing certificate. Install hooks refresh the icon cache if `uicache` is available.

The display name is ScummVM; the app lives at `/Applications/ScummVM-iOS5.app` with bundle/package identifier `org.scummvm.ios5`. Settings and saves live in `/var/mobile/Library/ScummVM.iOS5`, games default to `/var/mobile/ScummVM`, and the rotating log is `/var/mobile/.scummvm-ios5.log`. The package does not own or remove user data.

The package includes `/usr/bin/scummvm-ios5-runtime-probe`. Run it as `mobile` to check the C++ runtime, threads, FluidSynth, TLS entropy, certificates and compression libraries on the actual device. Host compilation and the SDK import audit do not establish device compatibility. Launch, rendering, audio and memory pressure still require a physical iPad test.
