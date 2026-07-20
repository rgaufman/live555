/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// Tolerant extraction of WWW-Authenticate Digest parameters (RFC 2617 sec 3.2.1).
//
// WHY THIS EXISTS. RTSPClient::handleAuthenticationFailure parsed the header with ONE sscanf shape:
//
//     Digest realm="%[^"]", nonce="%[^"]", stale=%[a-zA-Z]
//
// i.e. exactly this order, exactly this spacing, `stale` unquoted, and nothing else present. A
// camera emitting `stale="TRUE"` (quoted -- Hikvision does), or `nonce` before `realm`, or any of
// the perfectly legal `qop=`/`algorithm=`/`opaque=` parameters between them, fell through to the
// realm+nonce-only pattern -- so `stale` was silently read as ABSENT. The consequence is not
// cosmetic: a stale nonce on an unchanged realm then looked like a rejected credential
// ("same realm, not stale -> retrying cannot help"), and a long-lived digest session -- which is
// exactly what a proxy fleet runs, sending authenticated keep-alives for days -- lost its session
// to a routine nonce rotation.
//
// GStreamer parses every parameter generically and, on `stale`, resets its auth state so the retry
// is not counted as a credential failure (gstrtspsrc.c:7436-7443, :7495-7496; verified against
// current main 2026-07-21). This header is that idea in live555's house style: pure functions,
// header-only, unit-tested standalone (test/test_digest_auth.cpp), consumed by
// RTSPClient::handleAuthenticationFailure.
//
// Deliberately NOT a full RFC 2617 quoted-string parser: backslash escapes inside values are copied
// through verbatim, matching what the sscanf did. The job is finding parameters wherever they sit,
// not re-implementing HTTP grammar.

#ifndef _DIGEST_AUTH_PARAMS_HH
#define _DIGEST_AUTH_PARAMS_HH

#include <string.h>

// Case-insensitive ASCII compare of `n` chars. Local rather than _strncasecmp so this header stays
// dependency-free (the same reason RequestRetryPolicy.hh includes <errno.h> itself).
inline bool digestAuthKeyEq(char const* a, char const* b, unsigned n) {
  for (unsigned i = 0; i < n; ++i) {
    char ca = a[i], cb = b[i];
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
    if (ca != cb) return false;
    if (ca == '\0') return true;
  }
  return true;
}

// Extract the value of `key` from a parameter list like
//   realm="x", qop="auth", nonce="y", stale=TRUE
// wherever the key sits and whether or not the value is quoted. Returns true and NUL-terminates
// `out` on success; false when the key is absent (an absent parameter and an empty one are
// different answers, and collapsing them is how `stale` went unread for years).
//
// The scan is QUOTE-AWARE: text inside a quoted value is data, never a parameter. Without that, a
// realm containing `, stale=true` (legal in an RFC 2617 quoted-string, and peer-controlled) would
// be read as the stale parameter itself -- a header built to stop misreading auth headers must not
// introduce a way for a value to impersonate a key.
inline bool digestAuthParam(char const* params, char const* key, char* out, unsigned outSize) {
  if (params == NULL || key == NULL || out == NULL || outSize == 0) return false;
  unsigned const keyLen = (unsigned)strlen(key);

  bool inQuotes = false;
  for (char const* p = params; *p != '\0'; ++p) {
    if (*p == '"') { inQuotes = !inQuotes; continue; }
    if (inQuotes) continue; // value text: skip, whatever it resembles

    if (!digestAuthKeyEq(p, key, keyLen)) continue;
    // Key must start at a boundary (start of string, or after a separator) and be followed by '=',
    // so "opaque" cannot match inside "some-opaque-token" and "stale" cannot match "staleness".
    if (p != params) {
      char const before = p[-1];
      if (before != ' ' && before != '\t' && before != ',') continue;
    }
    char const* q = p + keyLen;
    while (*q == ' ' || *q == '\t') ++q;
    if (*q != '=') continue;
    ++q;
    while (*q == ' ' || *q == '\t') ++q;

    bool const quoted = (*q == '"');
    if (quoted) ++q;
    unsigned n = 0;
    while (*q != '\0' && n + 1 < outSize) {
      if (quoted ? (*q == '"') : (*q == ',' || *q == ' ' || *q == '\t' || *q == '\r' || *q == '\n'))
        break;
      out[n++] = *q++;
    }
    out[n] = '\0';
    return true;
  }
  return false;
}

// Does the header say the nonce is STALE -- i.e. the credentials were fine and only the nonce
// expired? RFC 2617: `stale=true` means "retry with the new nonce, do NOT re-prompt for
// credentials"; it is emitted routinely by cameras that rotate nonces on long-lived sessions.
// Accepts quoted or bare, any case. An absent or non-true value is False -- never a guess.
inline bool digestAuthIndicatesStaleNonce(char const* params) {
  char value[16];
  if (!digestAuthParam(params, "stale", value, sizeof value)) return false;
  // Compare THROUGH the NUL (n=5): full case-insensitive equality, so "truthy" and "tru" both fail
  // without a separate length check.
  return digestAuthKeyEq(value, "true", 5);
}

#endif
