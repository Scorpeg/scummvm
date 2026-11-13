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

#define FORBIDDEN_SYMBOL_ALLOW_ALL
#include "backends/platform/ios5/updates.h"
#include "common/config-manager.h"
#include "common/system.h"
#include "engines/engine.h"
#include "gui/message.h"
#include "gui/widget.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

Common::UpdateManager *createIOS5UpdateManager() {
	if (access("/usr/libexec/scummvm-ios5-update", X_OK))
		return nullptr;
	if (!ConfMan.hasKey("updates_check")) {
		ConfMan.setInt("updates_check", 0);
		ConfMan.flushToDisk();
	}
	return new IOS5UpdateManager();
}

class IOS5UpdateProgress : public GUI::MessageDialog {
	int _fd;
	pid_t _child;
	Common::String _pending;

public:
	Common::String response;
	bool ready;
	IOS5UpdateProgress(const char *identity) : GUI::MessageDialog(
												   identity ? "Downloading update..." : "Checking for updates...", "Cancel"),
											   _fd(-1), _child(-1), ready(false) {
		int channel[2];
		if (pipe(channel)) {
			response = "ERROR Cannot create updater pipe";
			return;
		}
		posix_spawn_file_actions_t actions;
		posix_spawn_file_actions_init(&actions);
		posix_spawn_file_actions_addclose(&actions, channel[0]);
		posix_spawn_file_actions_adddup2(&actions, channel[1], STDOUT_FILENO);
		posix_spawn_file_actions_addclose(&actions, channel[1]);
		char *args[] = {const_cast<char *>("/usr/libexec/scummvm-ios5-update"),
						const_cast<char *>(identity ? "--install" : "--check"), const_cast<char *>(identity), nullptr};
		char *environment[] = {const_cast<char *>("PATH=/usr/bin:/bin:/usr/sbin:/sbin"), nullptr};
		int result = posix_spawn(&_child, args[0], &actions, nullptr, args, environment);
		posix_spawn_file_actions_destroy(&actions);
		::close(channel[1]);
		if (result) {
			::close(channel[0]);
			_child = -1;
			response = "ERROR Cannot start updater helper";
			return;
		}
		_fd = channel[0];
		fcntl(_fd, F_SETFL, O_NONBLOCK);
	}
	~IOS5UpdateProgress() override {
		if (_fd >= 0)
			::close(_fd);
		if (_child > 0 && !ready) {
			kill(_child, SIGTERM);
			while (waitpid(_child, nullptr, 0) < 0 && errno == EINTR) {
			}
		}
	}
	void handleTickle() override {
		if (_fd < 0) {
			close();
			return;
		}
		char data[512];
		ssize_t count;
		while ((count = read(_fd, data, sizeof(data))) > 0) {
			_pending += Common::String(data, count);
			if (_pending.size() > 4096) {
				response = "ERROR Invalid updater output";
				close();
				return;
			}
			for (;;) {
				size_t end = _pending.find('\n');
				if (end == Common::String::npos)
					break;
				Common::String line = _pending.substr(0, end);
				_pending = _pending.substr(end + 1);
				if (line.hasPrefix("PROGRESS ")) {
					if (!_lines.empty())
						_lines[0]->setLabel(Common::U32String("Downloading: " + line.substr(9) + "%"));
				} else {
					response = line;
					if (line == "READY") {
						int status;
						pid_t finished = waitpid(_child, &status, WNOHANG);
						ready = finished == 0;
						if (!ready) {
							_child = -1;
							response = "ERROR Installer stopped while waiting for ScummVM. Please retry.";
						}
						close();
						return;
					}
				}
			}
		}
		if (count == 0 || (count < 0 && errno != EAGAIN && errno != EINTR)) {
			if (response.empty())
				response = "ERROR Updater stopped unexpectedly";
			close();
		}
	}
};

void IOS5UpdateManager::checkForUpdates() {
	if (g_engine) {
		GUI::MessageDialog("Return to the launcher before updating ScummVM.").runModal();
		return;
	}
	Common::String response;
	{
		IOS5UpdateProgress check(nullptr);
		check.runModal();
		response = check.response;
	}
	if (response.empty())
		return;
	if (response == "CURRENT") {
		GUI::MessageDialog("ScummVM is up to date.").runModal();
		return;
	}
	char version[129], revision[41], identity[65];
	unsigned long long size;
	if (sscanf(response.c_str(), "UPDATE %128s %40s %llu %64s", version, revision, &size, identity) != 4) {
		GUI::MessageDialog(response).runModal();
		return;
	}
	Common::String message = Common::String::format("Install ScummVM %.8s (%llu MB)?\nScummVM will close after downloading.\nWait for installation to finish before reopening it.", revision, size / (1024 * 1024));
	if (GUI::MessageDialog(message, "Install", "Cancel").runModal() != GUI::kMessageOK)
		return;
	IOS5UpdateProgress install(identity);
	install.runModal();
	if (install.ready) {
		ConfMan.flushToDisk();
		exit(0);
	}
	if (!install.response.empty())
		GUI::MessageDialog(install.response).runModal();
}
