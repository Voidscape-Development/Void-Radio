#pragma once

/* The minimal assertion helpers shared by the test programs. Each program
 * prints one line per check and exits non-zero if any failed. */

#include <cstdio>
#include <string>

namespace test {

inline int failures = 0;

inline void check(const char *what, long long got, long long want)
{
	if (got != want) {
		printf("FAIL %-44s got %lld want %lld\n", what, got, want);
		failures++;
	} else {
		printf("ok   %-44s %lld\n", what, got);
	}
}

inline void check_true(const char *what, bool ok)
{
	if (!ok) {
		printf("FAIL %s\n", what);
		failures++;
	} else {
		printf("ok   %s\n", what);
	}
}

inline void check_str(const char *what, const std::string &got, const std::string &want)
{
	if (got != want) {
		printf("FAIL %-44s got '%s' want '%s'\n", what, got.c_str(), want.c_str());
		failures++;
	} else {
		printf("ok   %-44s '%s'\n", what, got.c_str());
	}
}

inline void check_near(const char *what, double got, double want, double tolerance)
{
	const double diff = got > want ? got - want : want - got;
	if (diff > tolerance) {
		printf("FAIL %-44s got %g want %g (+/- %g)\n", what, got, want, tolerance);
		failures++;
	} else {
		printf("ok   %-44s %g\n", what, got);
	}
}

inline int finish()
{
	if (failures)
		printf("\n%d check(s) failed\n", failures);
	else
		printf("\nall checks passed\n");
	return failures ? 1 : 0;
}

} // namespace test
