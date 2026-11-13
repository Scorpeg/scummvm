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

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <signal.h>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#ifndef IOS5_UPDATE_CONFIG
#define IOS5_UPDATE_CONFIG "/etc/scummvm-ios5-updater"
#endif
#ifndef IOS5_UPDATE_STATE
#define IOS5_UPDATE_STATE "/var/lib/scummvm-ios5-updater"
#endif

static const unsigned long long kMaxPackage = 512ULL * 1024 * 1024;
static char *const kEnv[] = {const_cast<char *>("PATH=/usr/bin:/bin:/usr/sbin:/sbin"), const_cast<char *>("HOME=/var/root"), const_cast<char *>("LANG=C"), nullptr};
static volatile sig_atomic_t pendingDownload = 0;

static void cleanupDownload() {
	if (pendingDownload)
		unlink("package.deb");
}

static void cancelDownload(int number) {
	cleanupDownload();
	signal(number, SIG_DFL);
	raise(number);
	_exit(128 + number);
}

static void fail(const char *message) {
	printf("ERROR %s\n", message);
	exit(1);
}

static std::string hex(const unsigned char *bytes, size_t size) {
	static const char digits[] = "0123456789abcdef";
	std::string result;
	for (size_t i = 0; i < size; ++i) {
		result += digits[bytes[i] >> 4];
		result += digits[bytes[i] & 15];
	}
	return result;
}

static bool isHex(const std::string &value, size_t length) {
	return value.size() == length && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}

static std::string sha256(const std::string &value) {
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int length = 0;
	if (!EVP_Digest(value.data(), value.size(), digest, &length, EVP_sha256(), nullptr))
		fail("SHA256 failed");
	return hex(digest, length);
}

static std::string readConfig(const char *name) {
	struct stat directory;
	if (lstat(IOS5_UPDATE_CONFIG, &directory) || !S_ISDIR(directory.st_mode) || directory.st_uid != 0 || (directory.st_mode & 022))
		fail("Updater configuration directory has unsafe permissions");
	std::string path = std::string(IOS5_UPDATE_CONFIG) + "/" + name;
	int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW);
	struct stat st;
	if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0 || (st.st_mode & 022) || st.st_size > 16384)
		fail("Updater configuration is missing or has unsafe permissions");
	std::string result;
	char buffer[1024];
	ssize_t n;
	while ((n = read(fd, buffer, sizeof(buffer))) > 0) {
		result.append(buffer, n);
		if (result.size() > 16384)
			fail("Configuration is too large");
	}
	close(fd);
	if (n < 0)
		fail("Cannot read updater configuration");
	return result;
}

struct Download {
	FILE *file;
	std::string data;
	unsigned long long received, limit;
	int percent;
	EVP_MD_CTX *hash;
	Download(FILE *f, unsigned long long size) : file(f), received(0), limit(size), percent(-1), hash(nullptr) {
		if (file) {
			hash = EVP_MD_CTX_new();
			if (!hash || !EVP_DigestInit_ex(hash, EVP_sha256(), nullptr))
				fail("Cannot initialize package hash");
		}
	}
	~Download() { EVP_MD_CTX_free(hash); }
};

static size_t receive(void *bytes, size_t size, size_t count, void *opaque) {
	Download &d = *static_cast<Download *>(opaque);
	if (size && count > d.limit / size)
		return 0;
	size_t n = size * count;
	if (n > d.limit - d.received)
		return 0;
	if (d.file) {
		if (fwrite(bytes, 1, n, d.file) != n || !EVP_DigestUpdate(d.hash, bytes, n))
			return 0;
	} else {
		d.data.append(static_cast<char *>(bytes), n);
	}
	d.received += n;
	if (d.file) {
		int percent = int(d.received * 100 / d.limit);
		if (percent != d.percent) {
			d.percent = percent;
			printf("PROGRESS %d\n", percent);
		}
	}
	return n;
}

static void download(const std::string &url, Download &data) {
	CURL *curl = curl_easy_init();
	if (!curl)
		fail("Cannot initialize HTTPS");
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt(curl, CURLOPT_PROXY, "");
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(curl, CURLOPT_CAINFO, IOS5_UPDATE_CONFIG "/cacert.pem");
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, data.file ? 1800L : 60L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
	CURLcode result = curl_easy_perform(curl);
	long status = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_cleanup(curl);
	if (result != CURLE_OK)
		fail(curl_easy_strerror(result));
	if (status != 200) {
		char message[80];
		snprintf(message, sizeof(message), "Update server returned HTTP %ld", status);
		fail(message);
	}
}

struct Manifest {
	std::string revision, version, digest, filename, identity;
	unsigned long long size;
};

static Manifest verify(const std::string &envelope, const std::string &key) {
	size_t split = envelope.find('\n');
	if (split == std::string::npos || split > 2048 || split < 512 || split % 2 || !isHex(envelope.substr(0, split), split))
		fail("Invalid update signature encoding");
	std::vector<unsigned char> signature(split / 2);
	for (size_t i = 0; i < signature.size(); ++i) {
		char byte[3] = {envelope[2 * i], envelope[2 * i + 1], 0};
		signature[i] = strtoul(byte, nullptr, 16);
	}
	std::string payload = envelope.substr(split + 1);
	BIO *bio = BIO_new_mem_buf(key.data(), int(key.size()));
	EVP_PKEY *publicKey = bio ? PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr) : nullptr;
	BIO_free(bio);
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	bool valid = publicKey && EVP_PKEY_base_id(publicKey) == EVP_PKEY_RSA && EVP_PKEY_bits(publicKey) >= 2048 && context &&
				 EVP_DigestVerifyInit(context, nullptr, EVP_sha256(), nullptr, publicKey) == 1 &&
				 EVP_DigestVerifyUpdate(context, payload.data(), payload.size()) == 1 &&
				 EVP_DigestVerifyFinal(context, signature.data(), signature.size()) == 1;
	EVP_MD_CTX_free(context);
	EVP_PKEY_free(publicKey);
	if (!valid)
		fail("Update signature verification failed");
	std::vector<std::string> fields;
	size_t at = 0;
	while ((split = payload.find('\n', at)) != std::string::npos) {
		fields.push_back(payload.substr(at, split - at));
		at = split + 1;
	}
	if (at != payload.size() || fields.size() != 7 || fields[0] != "SCUMMVM-IOS5-1" ||
		!isHex(fields[1], 40) || fields[2].empty() || fields[2].size() > 128 ||
		fields[2].find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.+:~-") != std::string::npos ||
		fields[3].empty() || fields[3].size() > 10 || fields[3].find_first_not_of("0123456789") != std::string::npos ||
		!isHex(fields[4], 64) || fields[5] != fields[4] + ".deb" || fields[6] != "org.scummvm.ios5")
		fail("Invalid update manifest");
	Manifest m = {fields[1], fields[2], fields[4], fields[5], sha256(payload), strtoull(fields[3].c_str(), nullptr, 10)};
	if (m.size == 0 || m.size > kMaxPackage)
		fail("Unsupported package size");
	return m;
}

static int command(const char *path, const std::vector<std::string> &arguments, std::string *output = nullptr) {
	int pipefd[2];
	if (output && pipe(pipefd))
		fail("Cannot create command pipe");
	std::vector<char *> argv;
	argv.push_back(const_cast<char *>(path));
	for (size_t i = 0; i < arguments.size(); ++i)
		argv.push_back(const_cast<char *>(arguments[i].c_str()));
	argv.push_back(nullptr);
	pid_t child = fork();
	if (child == -1)
		fail("Cannot start package manager");
	if (child == 0) {
		if (geteuid() == 0 && setuid(0))
			_exit(126);
		if (output) {
			close(pipefd[0]);
			dup2(pipefd[1], STDOUT_FILENO);
			close(pipefd[1]);
		}
		execve(path, argv.data(), kEnv);
		_exit(127);
	}
	if (output) {
		close(pipefd[1]);
		char buffer[1024];
		ssize_t n;
		while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
			if (output->size() + n <= 4096)
				output->append(buffer, n);
		}
		close(pipefd[0]);
	}
	int status = 0;
	while (waitpid(child, &status, 0) < 0)
		if (errno != EINTR)
			fail("Cannot wait for package manager");
	return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

static std::string installedVersion() {
	std::string version;
	const char *argv[] = {"-W", "-f=${Version}", "org.scummvm.ios5"};
	if (command("/usr/bin/dpkg-query", std::vector<std::string>(argv, argv + 3), &version) || version.empty())
		fail("Cannot read installed ScummVM version");
	return version;
}

#ifndef IOS5_UPDATE_TEST
int main(int argc, char **argv) {
	extern char **environ;
	environ = const_cast<char **>(kEnv);
	setvbuf(stdout, nullptr, _IONBF, 0);
	bool install = argc == 3 && !strcmp(argv[1], "--install") && isHex(argv[2], 64);
	if (!install && !(argc == 2 && !strcmp(argv[1], "--check")))
		fail("Invalid updater command");
	if (getuid() != 501 && getuid() != 0)
		fail("Only mobile or root may run the updater");
	pid_t application = getppid();
	if (install) {
		if (geteuid() != 0 || setgid(0))
			fail("Updater helper is not installed with root permissions");
	} else {
		if (setgid(getgid()) || setuid(getuid()))
			fail("Cannot drop installer privileges");
	}
	umask(077);
	chdir("/");
	if (curl_global_init(CURL_GLOBAL_DEFAULT))
		fail("Cannot initialize networking");
	std::string base = readConfig("feed.url");
	if (!base.empty() && base[base.size() - 1] == '\n')
		base.erase(base.size() - 1);
	if (base.compare(0, 8, "https://") || base[base.size() - 1] != '/' || base.find_first_of("\r\n?#@") != std::string::npos)
		fail("Invalid update feed URL");
	Download metadata(nullptr, 8192);
	download(base + "latest.update", metadata);
	Manifest m = verify(metadata.data, readConfig("public.pem"));
	std::string comparison[] = {"--compare-versions", m.version, "gt", installedVersion()};
	int newer = command("/usr/bin/dpkg", std::vector<std::string>(comparison, comparison + 4));
	if (newer != 0 && newer != 1)
		fail("Cannot compare installed and available versions");
	if (newer == 1) {
		puts("CURRENT");
		return 0;
	}
	if (!install) {
		printf("UPDATE %s %s %llu %s\n", m.version.c_str(), m.revision.c_str(), m.size, m.identity.c_str());
		return 0;
	}
	if (m.identity != argv[2])
		fail("A different update was published; check again");
	if (mkdir(IOS5_UPDATE_STATE, 0700) && errno != EEXIST)
		fail("Cannot create installer storage");
	struct stat state;
	if (lstat(IOS5_UPDATE_STATE, &state) || !S_ISDIR(state.st_mode) || state.st_uid != 0 || (state.st_mode & 077))
		fail("Unsafe installer storage permissions");
	if (chdir(IOS5_UPDATE_STATE))
		fail("Cannot open installer storage");
	int lock = open("lock", O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
	if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB))
		fail("Another update is running");
	if (fcntl(lock, F_SETFD, FD_CLOEXEC))
		fail("Cannot protect installer lock");
	struct statvfs space;
	if (statvfs(".", &space) || (unsigned long long)space.f_bavail * space.f_frsize < m.size + 768ULL * 1024 * 1024)
		fail("Not enough free storage for download and installation");
	int fd = open("package.deb", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
	FILE *file = fd < 0 ? nullptr : fdopen(fd, "wb");
	if (!file)
		fail("Cannot create package download");
	pendingDownload = 1;
	atexit(cleanupDownload);
	signal(SIGTERM, cancelDownload);
	signal(SIGPIPE, cancelDownload);
	Download package(file, m.size);
	download(base + m.filename, package);
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int length = 0;
	bool valid = EVP_DigestFinal_ex(package.hash, digest, &length) && package.received == m.size && hex(digest, length) == m.digest;
	if (fflush(file) || fsync(fd))
		valid = false;
	if (fclose(file))
		valid = false;
	if (!valid) {
		unlink("package.deb");
		fail("Package checksum or size mismatch");
	}
	const std::string fields[] = {"Package", "Version", "Architecture"};
	for (const auto &field : fields) {
		std::string value;
		std::string arguments[] = {"-f", "package.deb", field};
		if (command("/usr/bin/dpkg-deb", std::vector<std::string>(arguments, arguments + 3), &value))
			fail("Cannot inspect update package");
		if (!value.empty() && value[value.size() - 1] == '\n')
			value.erase(value.size() - 1);
		std::string expected = field == "Package" ? "org.scummvm.ios5" : field == "Version" ? m.version
																							: "iphoneos-arm";
		if (value != expected)
			fail("Wrong package identity, version, or architecture");
	}
	int log = open("last-install.log", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
	if (log < 0)
		fail("Cannot create installation log");
	if (setsid() < 0)
		fail("Cannot detach installer");
	signal(SIGHUP, SIG_IGN);
	puts("READY");
	pendingDownload = 0;
	dup2(log, STDOUT_FILENO);
	dup2(log, STDERR_FILENO);
	close(log);
	int input = open("/dev/null", O_RDONLY);
	if (input >= 0) {
		dup2(input, STDIN_FILENO);
		close(input);
	}
	for (int wait = 0; wait < 60 && kill(application, 0) == 0; ++wait)
		sleep(1);
	if (kill(application, 0) == 0)
		fail("ScummVM did not close; update was not installed");
	printf("Installing %s (%s)\n", m.version.c_str(), m.revision.c_str());
	const char *arguments[] = {"--install", "package.deb"};
	int result = command("/usr/bin/dpkg", std::vector<std::string>(arguments, arguments + 2));
	printf("Installation exit status: %d\n", result);
	if (!result)
		unlink("package.deb");
	return result;
}
#endif
