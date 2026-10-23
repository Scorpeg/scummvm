// SPDX-License-Identifier: GPL-3.0-or-later
#include "backends/platform/ios5/metrics.h"
#include <pthread.h>
#include <stdio.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

namespace IOS5Metrics {
#if defined(IPHONE_IOS5)
static const char *const build = "ios5";
#elif defined(__APPLE__)
static const char *const build = "may2023-r5-79c29df349a69b95376506fffaa790f233074af2";
#else
static const char *const build = "host-test";
#endif
#ifdef __APPLE__
static pthread_once_t clockOnce = PTHREAD_ONCE_INIT;
static mach_timebase_info_data_t timebase;
static void initClock() { mach_timebase_info(&timebase); }
#endif
uint64_t nowUS() {
#ifdef __APPLE__
	pthread_once(&clockOnce, initClock);
	const uint64_t ticks = mach_absolute_time();

	return (ticks / timebase.denom * timebase.numer +
			ticks % timebase.denom * timebase.numer / timebase.denom) /
		   1000;
#else
	timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return uint64_t(t.tv_sec) * 1000000 + t.tv_nsec / 1000;
#endif
}
struct FrameTrace {
	uint64_t lastEnd, readUS, seekUS, openUS, metadataUS, bytes, reads, seeks, opens, maxUS;
	int64_t lastCPU;
	char slowest[96], lastOpened[96];
	FrameTrace() {
		memset(this, 0, sizeof(*this));
		lastCPU = -1;
	}
};
static pthread_key_t traceKey;
static pthread_once_t traceOnce = PTHREAD_ONCE_INIT;
static bool traceKeyValid;
static void deleteTrace(void *value) { delete static_cast<FrameTrace *>(value); }
static void initTraceKey() { traceKeyValid = pthread_key_create(&traceKey, deleteTrace) == 0; }
static FrameTrace *currentTrace() {
	pthread_once(&traceOnce, initTraceKey);
	return traceKeyValid ? static_cast<FrameTrace *>(pthread_getspecific(traceKey)) : nullptr;
}
static void copyName(char *out, const char *name, size_t size) {
	const char *base = name ? strrchr(name, '/') : nullptr;
	strncpy(out, base ? base + 1 : name ? name
										: "-",
			size - 1);
	out[size - 1] = 0;
}
void startFrameTrace() {
	FrameTrace *trace = currentTrace();
	if (!traceKeyValid)
		return;
	if (!trace) {
		trace = new FrameTrace();
		if (pthread_setspecific(traceKey, trace)) {
			delete trace;
			return;
		}
	}
	*trace = FrameTrace();
}
FileScope::FileScope(const char *operation, const char *name)
	: _operation(operation), _name(name), _trace(currentTrace()), _start(_trace ? nowUS() : 0), _bytes(0) {}
FileScope::~FileScope() {
	FrameTrace *trace = static_cast<FrameTrace *>(_trace);
	if (!trace)
		return;
	const uint64_t elapsed = nowUS() - _start;
	if (!strcmp(_operation, "read")) {
		trace->readUS += elapsed;
		trace->bytes += _bytes;
		++trace->reads;
	} else if (!strcmp(_operation, "seek")) {
		trace->seekUS += elapsed;
		++trace->seeks;
	} else if (!strcmp(_operation, "open")) {
		trace->openUS += elapsed;
		++trace->opens;
		copyName(trace->lastOpened, _name, sizeof(trace->lastOpened));
	} else
		trace->metadataUS += elapsed;
	if (elapsed >= trace->maxUS) {
		trace->maxUS = elapsed;
		copyName(trace->slowest, _name, sizeof(trace->slowest));
	}
}
void tracePresent(uint64_t start, uint64_t end) {
	FrameTrace *trace = currentTrace();
	if (!trace)
		return;
	const int64_t cpu = threadCPUUS();
	if (trace->lastEnd && end >= trace->lastEnd && end - trace->lastEnd >= 250000) {
		fprintf(stderr, "ios5-gap end_us=%llu gap_ms=%.3f before_present_ms=%.3f present_ms=%.3f thread_cpu_ms=%.3f read_ms=%.3f seek_ms=%.3f open_ms=%.3f metadata_ms=%.3f bytes=%llu reads=%llu seeks=%llu opens=%llu slowest_io_ms=%.3f slowest_file=%s last_open=%s\n",
				(unsigned long long)end, (end - trace->lastEnd) / 1000.0,
				(start >= trace->lastEnd ? start - trace->lastEnd : 0) / 1000.0,
				(end - start) / 1000.0, cpu >= trace->lastCPU && trace->lastCPU >= 0 ? (cpu - trace->lastCPU) / 1000.0 : -1.0,
				trace->readUS / 1000.0, trace->seekUS / 1000.0, trace->openUS / 1000.0, trace->metadataUS / 1000.0,
				(unsigned long long)trace->bytes, (unsigned long long)trace->reads,
				(unsigned long long)trace->seeks, (unsigned long long)trace->opens, trace->maxUS / 1000.0,
				trace->slowest[0] ? trace->slowest : "-", trace->lastOpened[0] ? trace->lastOpened : "-");
	}
	*trace = FrameTrace();
	trace->lastEnd = end;
	trace->lastCPU = cpu;
}
void Metric::begin(const char *name, const char *subject) {
	flush();
	_name = name;
	strncpy(_subject, subject ? subject : "", sizeof(_subject) - 1);
	_subject[sizeof(_subject) - 1] = 0;
	_stats = Stats();
	_startUS = 0;
}
int64_t threadCPUUS() {
#ifdef __APPLE__
	thread_basic_info_data_t info;
	mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
	if (thread_info(pthread_mach_thread_np(pthread_self()), THREAD_BASIC_INFO,
					(thread_info_t)&info, &count) != KERN_SUCCESS)
		return -1;
	return int64_t(info.user_time.seconds + info.system_time.seconds) * 1000000 +
		   info.user_time.microseconds + info.system_time.microseconds;
#else
	timespec t;
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t))
		return -1;
	return int64_t(t.tv_sec) * 1000000 + t.tv_nsec / 1000;
#endif
}
void Metric::record(uint64_t start, uint64_t end, uint64_t bytes, bool success) {
	if (!_startUS)
		_startUS = start;
	_stats.add(end >= start ? end - start : 0, end, bytes, success);
	if (end - _startUS >= 5000000)
		flush(end);
}
void Metric::flush(uint64_t end) {
	if (!_stats.count)
		return;
	if (!end)
		end = nowUS();
	const double seconds = double(end - _startUS) / 1000000.0;
	const double rate = seconds > 0 ? _stats.success / seconds : 0;
	long long residentKB = -1;
#ifdef __APPLE__
	if (!strcmp(_name, "present")) {
		task_basic_info_data_t info;
		mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
		if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS)
			residentKB = info.resident_size / 1024;
	}
#endif

	const bool sampleCPU = !strcmp(_name, "texture_copy") || !strcmp(_name, "present");
	const int64_t cpuUS = sampleCPU ? threadCPUUS() : -1;
	const uint64_t cpuSampleUS = sampleCPU ? nowUS() : 0;
	fprintf(stderr, "ios5-perf schema=1 build=%s metric=%s subject=%s start_us=%llu end_us=%llu seconds=%.3f count=%llu ok=%llu rate=%.3f avg_ms=%.3f max_ms=%.3f bytes=%llu gap_n=%llu gap_avg_ms=%.3f gap_p95_upper_ms=%u gap_max_ms=%.3f gap_gt100=%llu gap_gt200=%llu resident_kb=%lld thread_cpu_us=%lld cpu_sample_us=%llu\n",
			build, _name, _subject[0] ? _subject : "-", (unsigned long long)_startUS,
			(unsigned long long)end, seconds, (unsigned long long)_stats.count,
			(unsigned long long)_stats.success, rate, double(_stats.sumUS) / _stats.count / 1000.0,
			double(_stats.maxUS) / 1000.0, (unsigned long long)_stats.bytes,
			(unsigned long long)_stats.gaps, _stats.gaps ? double(_stats.gapSumUS) / _stats.gaps / 1000.0 : 0,
			_stats.p95GapUpperMS(), double(_stats.gapMaxUS) / 1000.0,
			(unsigned long long)_stats.over100, (unsigned long long)_stats.over200, residentKB,
			(long long)cpuUS, (unsigned long long)cpuSampleUS);
	_stats.clearWindow();
	_startUS = end;
}
} // namespace IOS5Metrics
