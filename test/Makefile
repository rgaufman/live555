# live555 patch unit tests. Standalone — depends only on the header-only helpers in
# liveMedia/include (no liveMedia.a link), so it builds anywhere g++/clang++ is: target host or dev Mac.
#   make            build the tests
#   make run        build + run (non-zero exit on any failure — usable in CI / a pre-deploy gate)
#   make clean

CXX      ?= c++
CXXFLAGS ?= -O0 -g -Wall -Wextra
INCLUDES  = -I../liveMedia/include -I../UsageEnvironment/include

# DERIVED from the sources, never hand-listed. A hardcoded list silently skips any test file added
# without also editing this line — the suite then reports "all passed" while never having built the
# one test that mattered. (build_clean.sh's pre-deploy gate derives its list the same way, after
# exactly that happened to test_stream_role.) Every test_*.cpp here is a suite by construction.
TESTS = $(patsubst %.cpp,%,$(wildcard test_*.cpp))

# A wildcard matching nothing would make `make run` a no-op that exits 0 — a green light for an
# empty suite, which is the same silent-pass failure the wildcard was introduced to remove.
ifeq ($(strip $(TESTS)),)
$(error no test_*.cpp found — the unit-test gate would silently pass with zero suites)
endif

all: $(TESTS)

# One pattern rule for every suite. Each test is one translation unit including headers from
# ../liveMedia/include, so the dependency is "this source + every header it can see": listing
# individual headers per target invited them to drift out of date and stop triggering rebuilds.
HEADERS = $(wildcard ../liveMedia/include/*.hh ../UsageEnvironment/include/*.hh *.hh)

test_%: test_%.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $<

run: $(TESTS)
	@for t in $(TESTS); do echo "== ./$$t =="; ./$$t || exit 1; done

clean:
	rm -f $(TESTS)

.PHONY: all run clean
