// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IOS5_METRICS_H
#define IOS5_METRICS_H
#include <stdint.h>
#include <string.h>

namespace IOS5Metrics {
uint64_t nowUS();
int64_t threadCPUUS();
void startFrameTrace();
void tracePresent(uint64_t start, uint64_t end);
class FileScope {
	const char *_operation, *_name;
	void *_trace;
	uint64_t _start, _bytes;

public:
	FileScope(const char *operation, const char *name);
	~FileScope();
	void bytes(uint64_t value) { _bytes = value; }

private:
	FileScope(const FileScope &);
	FileScope &operator=(const FileScope &);
};

struct Stats {
	uint64_t count, success, bytes, sumUS, maxUS, gaps, gapSumUS, gapMaxUS, over100, over200;
	uint64_t lastSuccessUS;
	uint32_t gapBins[256];
	Stats() { memset(this, 0, sizeof(*this)); }
	void add(uint64_t duration, uint64_t end, uint64_t volume, bool ok) {
		++count;
		sumUS += duration;
		bytes += volume;
		if (duration > maxUS)
			maxUS = duration;
		if (!ok)
			return;
		++success;
		if (lastSuccessUS && end >= lastSuccessUS) {
			const uint64_t gap = end - lastSuccessUS;
			++gaps;
			gapSumUS += gap;
			if (gap > gapMaxUS)
				gapMaxUS = gap;
			over100 += gap > 100000;
			over200 += gap > 200000;
			++gapBins[gap / 2000 < 255 ? gap / 2000 : 255];
		}
		lastSuccessUS = end;
	}
	unsigned p95GapUpperMS() const {
		if (!gaps)
			return 0;
		uint64_t total = 0, target = (gaps * 95 + 99) / 100;
		for (unsigned i = 0; i < 256; ++i) {
			total += gapBins[i];
			if (total >= target)
				return i == 255 ? 0 : (i + 1) * 2;
		}
		return 0;
	}
	void clearWindow() {
		const uint64_t previous = lastSuccessUS;
		memset(this, 0, sizeof(*this));
		lastSuccessUS = previous;
	}
};

class Metric {
	const char *_name;
	char _subject[80];
	Stats _stats;
	uint64_t _startUS;

public:
	explicit Metric(const char *name = "unused") : _name(name), _startUS(0) { _subject[0] = 0; }
	void begin(const char *name, const char *subject);
	void record(uint64_t start, uint64_t end, uint64_t bytes = 0, bool success = true);
	void flush(uint64_t end = 0);
};

class Scope {
	Metric &_metric;
	uint64_t _start, _bytes;

public:
	Scope(Metric &metric, uint64_t bytes = 0) : _metric(metric), _start(nowUS()), _bytes(bytes) {}
	~Scope() { _metric.record(_start, nowUS(), _bytes); }

private:
	Scope(const Scope &);
	Scope &operator=(const Scope &);
};
} // namespace IOS5Metrics
#endif
