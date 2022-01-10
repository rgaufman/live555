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
// "liveMedia"
// Copyright (c) 1996-2022 Live Networks, Inc.  All rights reserved.
// State encapsulating a TLS connection
// Implementation

#include "TLSState.hh"
#include "RTSPClient.hh"
#ifndef NO_OPENSSL
#include <openssl/err.h>
#endif

////////// TLSState implementation //////////

TLSState::TLSState()
  : isNeeded(False)
#ifndef NO_OPENSSL
  , fHasBeenSetup(False), fCtx(NULL), fCon(NULL)
#endif
{
}

TLSState::~TLSState() {
#ifndef NO_OPENSSL
  reset();
#endif
}

int TLSState::write(const char* data, unsigned count) {
#ifndef NO_OPENSSL
  return SSL_write(fCon, data, count);
#else
  return -1;
#endif
}

int TLSState::read(u_int8_t* buffer, unsigned bufferSize) {
#ifndef NO_OPENSSL
  int result = SSL_read(fCon, buffer, bufferSize);
  if (result <= 0) {
    if (SSL_get_error(fCon, result) == SSL_ERROR_WANT_READ) {
      // The data can't be delivered yet.  Return 0 (bytes read); we'll try again later
      return 0;
    }
    return -1; // assume that the connection has closed
  }
  return result;
#else
  return 0;
#endif
}

#ifndef NO_OPENSSL
void TLSState::initLibrary() {
  static Boolean SSLLibraryHasBeenInitialized = False;
  if (!SSLLibraryHasBeenInitialized) {
    (void)SSL_library_init();
    SSLLibraryHasBeenInitialized = True;
  }
}

void TLSState::reset() {
  if (fHasBeenSetup) SSL_shutdown(fCon);

  if (fCon != NULL) { SSL_free(fCon); fCon = NULL; }
  if (fCtx != NULL) { SSL_CTX_free(fCtx); fCtx = NULL; }
}
#endif


////////// ClientTLSState implementation //////////

ClientTLSState::ClientTLSState(RTSPClient& client)
#ifndef NO_OPENSSL
  : fClient(client)
#endif
{
}

ClientTLSState::~ClientTLSState() {
}

int ClientTLSState::connect(int socketNum) {
#ifndef NO_OPENSSL
  if (!fHasBeenSetup && !setup(socketNum)) return -1; // error
  
  // Complete the SSL-level connection to the server:
  int sslConnectResult = SSL_connect(fCon);
  int sslGetErrorResult = SSL_get_error(fCon, sslConnectResult);

  if (sslConnectResult > 0) {
    return sslConnectResult; // connection has completed
  } else if (sslConnectResult < 0
	      && (sslGetErrorResult == SSL_ERROR_WANT_READ ||
		  sslGetErrorResult == SSL_ERROR_WANT_WRITE)) {
    // We need to wait until the socket is readable or writable:
    fClient.envir().taskScheduler()
      .setBackgroundHandling(socketNum,
			     sslGetErrorResult == SSL_ERROR_WANT_READ ? SOCKET_READABLE : SOCKET_WRITABLE,
			     (TaskScheduler::BackgroundHandlerProc*)&RTSPClient::connectionHandler,
			     &fClient);
    return 0; // connection is pending
  } else {
    fClient.envir().setResultErrMsg("TLS connection to server failed: ", sslGetErrorResult);
    return -1; // error
  }
#else
  return -1;	   
#endif
}

#ifndef NO_OPENSSL
Boolean ClientTLSState::setup(int socketNum) {
  do {
    initLibrary();

    SSL_METHOD const* meth = SSLv23_client_method();
    if (meth == NULL) break;

    fCtx = SSL_CTX_new(meth);
    if (fCtx == NULL) break;

    fCon = SSL_new(fCtx);
    if (fCon == NULL) break;

    BIO* bio = BIO_new_socket(socketNum, BIO_NOCLOSE);
    SSL_set_bio(fCon, bio, bio);

    SSL_set_connect_state(fCon);

    fHasBeenSetup = True;
    return True;
  } while (0);

  // An error occurred:
  reset();
  return False;
}
#endif


////////// ServerTLSState implementation //////////

ServerTLSState::ServerTLSState(UsageEnvironment& env)
  : tlsAcceptIsNeeded(False)
#ifndef NO_OPENSSL
  , fEnv(env), fCertificateFileName(NULL), fPrivateKeyFileName(NULL)
#endif
{
}

ServerTLSState::~ServerTLSState() {
}

void ServerTLSState
::setCertificateAndPrivateKeyFileNames(char const* certFileName, char const* privKeyFileName) {
#ifndef NO_OPENSSL
  fCertificateFileName = certFileName;
  fPrivateKeyFileName = privKeyFileName;
#endif
}

int ServerTLSState::accept(int socketNum) {
#ifndef NO_OPENSSL
  if (!fHasBeenSetup && !setup(socketNum)) return -1; // error
  
  int sslAcceptResult = SSL_accept(fCon);
  int sslGetErrorResult = SSL_get_error(fCon, sslAcceptResult);

  if (sslAcceptResult > 0) {
    return sslAcceptResult; // success
  } else if (sslAcceptResult < 0 && sslGetErrorResult == SSL_ERROR_WANT_READ) {
    // We need to wait until the socket is readable:
    return 0; // connection is pending
  } else {
    fEnv.setResultErrMsg("SSL_accept() call failed: ", sslGetErrorResult);
    return -1; // error
  }
#else
  return -1;	   
#endif
}

#ifndef NO_OPENSSL
Boolean ServerTLSState::setup(int socketNum) {
  do {
    initLibrary();

    SSL_METHOD const* meth = SSLv23_server_method();
    if (meth == NULL) break;

    fCtx = SSL_CTX_new(meth);
    if (fCtx == NULL) break;

    if (SSL_CTX_set_ecdh_auto(fCtx, 1) != 1) break;

    if (SSL_CTX_use_certificate_file(fCtx, fCertificateFileName, SSL_FILETYPE_PEM) != 1) break;

    if (SSL_CTX_use_PrivateKey_file(fCtx, fPrivateKeyFileName, SSL_FILETYPE_PEM) != 1) break;

    fCon = SSL_new(fCtx);
    if (fCon == NULL) break;

    BIO* bio = BIO_new_socket(socketNum, BIO_NOCLOSE);
    SSL_set_bio(fCon, bio, bio);

    fHasBeenSetup = True;
    return True;
  } while (0);

  // An error occurred:
  ERR_print_errors_fp(stderr);
  reset();
  return False;
}
#endif
