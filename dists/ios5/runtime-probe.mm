#import <Foundation/Foundation.h>
#include <limits>
#include <mach/mach_time.h>
#include <new>
#include <pthread.h>
#include <stdio.h>
#ifdef IOS5_FULL_DEPS
#include <curl/curl.h>
#include <fluidsynth.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <zlib.h>
static int fullDependencies() {
	int failed = 0;
	fluid_settings_t *settings = new_fluid_settings();
	fluid_synth_t *synth = nullptr;
	if (settings) {
		fluid_settings_setnum(settings, "synth.sample-rate", 22050);
		fluid_settings_setint(settings, "synth.polyphony", 8);
		synth = new_fluid_synth(settings);
	}
	short left[64] = {}, right[64] = {};
	int midiFailed = !synth || fluid_synth_write_s16(synth, 64, left, 0, 1, right, 0, 1) != FLUID_OK;
	for (int i = 0; i < 64; ++i)
		midiFailed |= left[i] != 0 || right[i] != 0;
	if (synth)
		delete_fluid_synth(synth);
	if (settings)
		delete_fluid_settings(settings);
	printf("FluidSynth %s allocation/render/cleanup: %s\n", fluid_version_str(), midiFailed ? "FAIL" : "PASS");
	failed |= midiFailed;
	unsigned char random[32];
	SSL_CTX *context = SSL_CTX_new(TLS_client_method());
	int tlsFailed = !context || RAND_bytes(random, sizeof(random)) != 1;
	if (context) {
		tlsFailed |= SSL_CTX_load_verify_locations(context, "/Applications/ScummVM-iOS5.app/cacert.pem", nullptr) != 1;
		SSL_CTX_free(context);
	}
	printf("%s context/entropy/bundled CA: %s\n", OpenSSL_version(OPENSSL_VERSION), tlsFailed ? "FAIL" : "PASS");
	failed |= tlsFailed;
	int curlFailed = curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK;
	if (!curlFailed) {
		const curl_version_info_data *version = curl_version_info(CURLVERSION_NOW);
		CURL *handle = curl_easy_init();
		curlFailed = !handle || !version || !(version->features & CURL_VERSION_SSL);
		if (handle)
			curl_easy_cleanup(handle);
		curl_global_cleanup();
	}
	printf("curl %s init/TLS capability/cleanup: %s\n", LIBCURL_VERSION, curlFailed ? "FAIL" : "PASS");
	printf("zlib runtime version: %s\n", zlibVersion());
	return failed | curlFailed;
}
#endif
struct Base {
	virtual ~Base() {}
	virtual int value() const = 0;
};
struct Value final : Base {
	int value() const override { return 42; }
};
static int result() {
	static Value value;
	return value.value();
}
static void *worker(void *) { return result() == 42 ? nullptr : (void *)1; }
int main() {
	NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
	Base *p = new Value;
	auto answer = [p]() { return p->value(); };
	int failed = answer() != 42 || std::numeric_limits<unsigned>::digits != 32;
	pthread_t thread;
	void *status = (void *)1;
	if (pthread_create(&thread, nullptr, worker, nullptr))
		failed = 1;
	else {
		pthread_join(thread, &status);
		failed |= status != nullptr;
	}
	mach_timebase_info_data_t timebase;
	failed |= mach_timebase_info(&timebase) != 0;
	printf("ScummVM iOS5 C++ runtime: %s\n", failed ? "FAIL" : "PASS");
#ifdef IOS5_FULL_DEPS
	failed |= fullDependencies();
#endif
	delete p;
	[pool drain];
	return failed;
}
