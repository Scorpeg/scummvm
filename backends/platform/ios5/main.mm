/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// Disable symbol overrides so that we can use system headers.
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include <Foundation/NSThread.h>
#include <UIKit/UIKit.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "backends/platform/ios7/ios7_video.h"
#include "base/version.h"

int iOS7_argc;
char **iOS7_argv;

int main(int argc, char **argv) {
	int returnCode;
#ifdef IPHONE_IOS5
	const char *path = "/var/mobile/.scummvm-ios5.log";
	struct stat st;
	if (stat(path, &st) == 0 && st.st_size > 1024 * 1024)
		rename(path, "/var/mobile/.scummvm-ios5.log.previous");
	int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd >= 0) {
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		close(fd);
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	fprintf(stderr, "\nios5: ScummVM %s backend=ios5/gles2 time=%ld draw=changed-rows full-upload=cv-surface fallback=teximage compare=neon64 release-gl-checks=explicit-only\n", gScummVMVersion, (long)time(NULL));
#endif

	@autoreleasepool {
		iOS7_argc = argc;
		iOS7_argv = argv;

		returnCode = UIApplicationMain(argc, argv, @"UIApplication", @"iOS7AppDelegate");
	}

	return returnCode;
}
