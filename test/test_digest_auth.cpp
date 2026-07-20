// Unit tests for DigestAuthParams.hh -- tolerant WWW-Authenticate Digest parameter extraction.
//
// The defect these pin: RTSPClient::handleAuthenticationFailure recognised `stale` through exactly
// one sscanf shape (realm, then nonce, then bare unquoted stale, nothing else present). Real
// cameras emit `stale="TRUE"` quoted, reorder parameters, and interleave qop=/algorithm=/opaque= --
// all legal per RFC 2617 sec 3.2.1 -- so a routine nonce rotation on a long-lived session read as a
// rejected credential and cost the session. GStreamer parses the parameters generically
// (gstrtspsrc.c:7436-7443) and treats stale as "retry with the new nonce, credentials are fine"
// (:7495-7496).

#include "DigestAuthParams.hh"
#include "check.hh"

#include <string.h>

static void checkParam(char const* params, char const* key, char const* want, char const* what) {
  char buf[256];
  bool const found = digestAuthParam(params, key, buf, sizeof buf);
  if (want == NULL) {
    check(!found, what);
    return;
  }
  if (!found) {
    fprintf(stderr, "FAIL: %s (key %s not found)\n", what, key);
    ++g_checkFailures;
    return;
  }
  if (strcmp(buf, want) != 0) {
    fprintf(stderr, "FAIL: %s (got \"%s\", want \"%s\")\n", what, buf, want);
    ++g_checkFailures;
  }
}

static void testCanonicalShapeStillParses() {
  // The exact string the old sscanf handled; the tolerant parser must not regress it.
  char const* h = "Digest realm=\"IP Camera\", nonce=\"abc123\", stale=TRUE";
  checkParam(h, "realm", "IP Camera", "canonical: realm extracted");
  checkParam(h, "nonce", "abc123", "canonical: nonce extracted");
  check(digestAuthIndicatesStaleNonce(h), "canonical: bare stale=TRUE is stale");
}

static void testRealWorldShapesTheOldParserMissed() {
  // Quoted stale -- the Hikvision shape. The old parser read this as realm+nonce with NO stale.
  check(digestAuthIndicatesStaleNonce("Digest realm=\"cam\", nonce=\"n2\", stale=\"TRUE\""),
        "quoted stale=\"TRUE\" is recognised");
  check(digestAuthIndicatesStaleNonce("Digest realm=\"cam\", nonce=\"n2\", stale=\"true\""),
        "lowercase quoted stale is recognised");

  // Reordered and interleaved with the other legal RFC 2617 parameters.
  char const* h = "Digest qop=\"auth\", nonce=\"n3\", algorithm=MD5, stale=true, "
                  "opaque=\"5ccc069c\", realm=\"axis\"";
  checkParam(h, "realm", "axis", "realm found regardless of position");
  checkParam(h, "nonce", "n3", "nonce found with qop/algorithm in between");
  checkParam(h, "algorithm", "MD5", "unquoted value extracted");
  check(digestAuthIndicatesStaleNonce(h), "stale found mid-list");
}

static void testNotStaleIsNeverGuessedStale() {
  check(!digestAuthIndicatesStaleNonce("Digest realm=\"cam\", nonce=\"n\", stale=FALSE"),
        "stale=FALSE is not stale");
  check(!digestAuthIndicatesStaleNonce("Digest realm=\"cam\", nonce=\"n\""),
        "absent stale is not stale (absence is never treated as a value)");
  check(!digestAuthIndicatesStaleNonce("Digest realm=\"cam\", nonce=\"n\", stale=truthy"),
        "a value merely STARTING with 'true' is not stale -- the value is 'true', not a prefix");
  check(!digestAuthIndicatesStaleNonce(NULL), "NULL params is not stale");
}

static void testKeyMatchingIsBounded() {
  // "stale" must not match inside another token, and a key is only a key when '=' follows.
  check(!digestAuthIndicatesStaleNonce("Digest realm=\"stale=true\", nonce=\"n\""),
        "a 'stale=true' INSIDE a quoted realm is data, not a parameter"); // boundary: after '"' -- no separator
  checkParam("Digest nonstale=true, realm=\"r\"", "stale", NULL,
             "'nonstale' does not match 'stale' (key needs a boundary before it)");
  checkParam("Digest stale, realm=\"r\"", "stale", NULL,
             "a bare token with no '=' is not a parameter");
  checkParam("Digest opaque=\"abc\"", "opaq", NULL,
             "a key that is a PREFIX of a real key does not match it ('opaq' vs 'opaque=')");
}

int main() {
  printf("== Digest WWW-Authenticate parameter extraction ==\n");
  testCanonicalShapeStillParses();
  testRealWorldShapesTheOldParserMissed();
  testNotStaleIsNeverGuessedStale();
  testKeyMatchingIsBounded();

  return checkSummary("test_digest_auth");
}
