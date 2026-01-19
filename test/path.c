#include "assert.h"
#include "test.h"

#define USE_POSIX_PATHS
#include "dged/path.h"
#include "dged/s8.h"

#include <stdlib.h>
#include <unistd.h>

static void test_expanduser() {
  const char *home = getenv("HOME");

  setenv("HOME", "/home/sune", 1);
  struct s8 expanded = expanduser(s8("~/repos/apa/bapa"));
  ASSERT(s8eq(expanded, s8("/home/sune/repos/apa/bapa")),
         "Expected home folder to be resolved");

  s8delete(expanded);

  struct s8 expanded2 = expanduser(s8("/in/the~/middle"));
  ASSERT(s8eq(expanded2, s8("/in/the/home/sune/middle")),
         "Expected home folder to be resolved in the middle of a path");
  s8delete(expanded2);

  struct s8 not_expanded = expanduser(s8("/home/sune/apa/bapa"));
  ASSERT(s8eq(not_expanded, s8("/home/sune/apa/bapa")),
         "Expected nothing to happen if path is already expanded");
  s8delete(not_expanded);

  setenv("HOME", home, 1);
}

static void test_path_segments() {
  struct s8 path = s8("/an/absolute/path/../with/.//some-stuff");

  struct s8 *segments = NULL;
  struct s8 expected_segments[] = {s8("/"),         s8("an"), s8("absolute"),
                                   s8("path"),      s8(".."), s8("with"),
                                   s8("some-stuff")};
  size_t nsegments = path_segments(path, &segments);
  ASSERT(nsegments == 7, "Expected 7 path segments to be returned, got %d",
         nsegments);

  for (size_t i = 0; i < nsegments; ++i) {
    ASSERT(s8eq(segments[i], expected_segments[i]),
           "Expected returned segment at index %d to be \"%s\", was \"%s\"", i,
           s8ascstr(expected_segments[i]), s8ascstr(segments[i]));
    s8delete(segments[i]);
  }
  free(segments);

  // test a relative path with trailing slash
  struct s8 *segments2 = NULL;
  struct s8 expected_segments2[] = {s8("a"),  s8("relative"), s8("path"),
                                    s8(".."), s8("with"),     s8("some-stuff")};
  size_t nsegments2 =
      path_segments(s8("a/relative/path/../with/.//some-stuff/"), &segments2);
  ASSERT(nsegments2 == 6, "Expected 6 path segments to be returned, got %d",
         nsegments2);

  for (size_t i = 0; i < nsegments2; ++i) {
    ASSERT(s8eq(segments2[i], expected_segments2[i]),
           "Expected returned segment at index %d to be \"%s\", was \"%s\"", i,
           s8ascstr(expected_segments2[i]), s8ascstr(segments2[i]));
    s8delete(segments2[i]);
  }
  free(segments2);

  // test path without slashes
  struct s8 *segments3 = NULL;
  struct s8 expected_segments3[] = {s8("a-path-without-slashes")};
  size_t nsegments3 = path_segments(s8("a-path-without-slashes"), &segments3);
  ASSERT(nsegments3 == 1, "Expected 1 path segment to be returned, got %d",
         nsegments3);

  for (size_t i = 0; i < nsegments3; ++i) {
    ASSERT(s8eq(segments3[i], expected_segments3[i]),
           "Expected returned segment at index %d to be \"%s\", was \"%s\"", i,
           s8ascstr(expected_segments3[i]), s8ascstr(segments3[i]));
    s8delete(segments3[i]);
  }
  free(segments3);

  // test path without slashes
  struct s8 *segments4 = NULL;
  struct s8 expected_segments4[] = {s8("/"),
                                    s8("absolute-path-without-slashes")};
  size_t nsegments4 =
      path_segments(s8("/absolute-path-without-slashes"), &segments4);
  ASSERT(nsegments4 == 2, "Expected 2 path segments to be returned, got %d",
         nsegments4);

  for (size_t i = 0; i < nsegments4; ++i) {
    ASSERT(s8eq(segments4[i], expected_segments4[i]),
           "Expected returned segment at index %d to be \"%s\", was \"%s\"", i,
           s8ascstr(expected_segments4[i]), s8ascstr(segments4[i]));
    s8delete(segments4[i]);
  }
  free(segments4);
}

static void test_join_path(void) {
  struct s8 p = join_path(s8("apa"), s8("bapa"));
  struct s8 expected = s8from_fmt("%s%s%s", "apa", s8ascstr(PATHSEP), "bapa");
  ASSERT(s8eq(p, expected), "Expected path join to produce \"%s\"",
         s8ascstr(expected));

  s8delete(p);
  s8delete(expected);
}

static void test_join_path_segments(void) {
  struct s8 segments[] = {s8("a"), s8("b"), s8("c"), s8("de")};
  struct s8 joined =
      join_path_segments(segments, sizeof(segments) / sizeof(segments[0]));

  struct s8 expected =
      s8from_fmt("%s%s%s%s%s%s%s", "a", s8ascstr(PATHSEP), "b",
                 s8ascstr(PATHSEP), "c", s8ascstr(PATHSEP), "de");

  ASSERT(s8eq(joined, expected),
         "Expected joining of path segments to produce \"%s\", got \"%s\"",
         s8ascstr(expected), s8ascstr(joined));

  s8delete(joined);
  s8delete(expected);

  struct s8 segments2[] = {s8("/"), s8("a"), s8("b")};
  struct s8 joined2 =
      join_path_segments(segments2, sizeof(segments2) / sizeof(segments2[0]));

  struct s8 expected2 =
      s8from_fmt("%s%s%s%s", s8ascstr(PATHSEP), "a", s8ascstr(PATHSEP), "b");

  ASSERT(s8eq(joined2, expected2),
         "Expected joining of path segments to produce \"%s\", got \"%s\"",
         s8ascstr(expected2), s8ascstr(joined2));

  s8delete(joined2);
  s8delete(expected2);
}

static void test_parent(void) {
  struct s8 par = parent(s8("/parent/child"));
  ASSERT(s8eq(par, s8("/parent")), "Expected parent to be \"parent\", got: %s",
         s8ascstr(par));

  s8delete(par);

  par = parent(s8("/"));
  ASSERT(s8eq(par, s8("/")), "Expected parent of root to be itself, was: %s",
         s8ascstr(par));
  s8delete(par);
}

static void test_filename(void) {
  struct s8 fn = filename(s8("/path/to/archive.tar.xz"));
  ASSERT(s8eq(fn, s8("archive.tar.xz")),
         "Expected filename to be \"archive.tar.xz\", got \"%s\"",
         s8ascstr(fn));
  s8delete(fn);
}

static void test_filestem_and_extension(void) {
  struct s8 stem = filestem(s8("/path/to/archive.tar.xz"));
  struct s8 ext = extension(s8("/path/to/archive.tar.xz"));
  ASSERT(s8eq(stem, s8("archive")),
         "Expected filestem to be \"archive\", got \"%s\"", s8ascstr(stem));

  ASSERT(s8eq(ext, s8(".tar.xz")),
         "Expected filename to be \".tar.xz\", got \"%s\"", s8ascstr(ext));

  s8delete(stem);
  s8delete(ext);
}

static void test_relative_to(void) {
  struct s8 rel = relative_to(s8("/path/to/my/thing"), s8("/path/to/"));
  ASSERT(s8eq(rel, s8("my/thing")),
         "Expected the relative path to be \"my/thing\", got \"%s\"",
         s8ascstr(rel));
  s8delete(rel);
}

void run_path_tests(void) {
  run_test(test_expanduser);
  run_test(test_path_segments);
  run_test(test_join_path);
  run_test(test_join_path_segments);
  run_test(test_parent);
  run_test(test_filename);
  run_test(test_filestem_and_extension);
  run_test(test_relative_to);
}
