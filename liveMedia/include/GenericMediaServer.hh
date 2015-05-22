/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
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
// Copyright (c) 1996-2015 Live Networks, Inc.  All rights reserved.
// A generic media server class, used to implement a RTSP server, and any other server that uses
//  "ServerMediaSession" objects to describe media to be served.
// C++ header

#ifndef _GENERIC_MEDIA_SERVER_HH
#define _GENERIC_MEDIA_SERVER_HH

#ifndef _SERVER_MEDIA_SESSION_HH
#include "ServerMediaSession.hh"
#endif
#if 0
#ifndef _NET_ADDRESS_HH
#include <NetAddress.hh>
#endif
#endif

class GenericMediaServer: public Medium {
public:
  void addServerMediaSession(ServerMediaSession* serverMediaSession);

  virtual ServerMediaSession*
  lookupServerMediaSession(char const* streamName, Boolean isFirstLookupInSession = True);

  void removeServerMediaSession(ServerMediaSession* serverMediaSession);
      // Removes the "ServerMediaSession" object from our lookup table, so it will no longer be accessible by new RTSP clients.
      // (However, any *existing* RTSP client sessions that use this "ServerMediaSession" object will continue streaming.
      //  The "ServerMediaSession" object will not get deleted until all of these RTSP client sessions have closed.)
      // (To both delete the "ServerMediaSession" object *and* close all RTSP client sessions that use it,
      //  call "deleteServerMediaSession(serverMediaSession)" instead.)
  void removeServerMediaSession(char const* streamName);
     // ditto

  void closeAllClientSessionsForServerMediaSession(ServerMediaSession* serverMediaSession);
      // Closes (from the server) all RTSP client sessions that are currently using this "ServerMediaSession" object.
      // Note, however, that the "ServerMediaSession" object remains accessible by new RTSP clients.
  void closeAllClientSessionsForServerMediaSession(char const* streamName);
     // ditto

  void deleteServerMediaSession(ServerMediaSession* serverMediaSession);
      // Equivalent to:
      //     "closeAllClientSessionsForServerMediaSession(serverMediaSession); removeServerMediaSession(serverMediaSession);"
  void deleteServerMediaSession(char const* streamName);
      // Equivalent to:
      //     "closeAllClientSessionsForServerMediaSession(streamName); removeServerMediaSession(streamName);

protected:
  GenericMediaServer(UsageEnvironment& env, int ourSocket, Port ourPort,
		     TaskScheduler::BackgroundHandlerProc* incomingConnectionHandler);
      // we're an abstract base class
  virtual ~GenericMediaServer();

#if 0
  static int setUpOurSocket(UsageEnvironment& env, Port& ourPort);

  virtual char const* allowedCommandNames(); // used to implement "RTSPClientConnection::handleCmd_OPTIONS()"
  virtual Boolean weImplementREGISTER(char const* proxyURLSuffix, char*& responseStr);
      // used to implement "RTSPClientConnection::handleCmd_REGISTER()"
      // Note: "responseStr" is dynamically allocated (or NULL), and should be delete[]d after the call
  virtual void implementCmd_REGISTER(char const* url, char const* urlSuffix, int socketToRemoteServer,
				     Boolean deliverViaTCP, char const* proxyURLSuffix);
      // used to implement "RTSPClientConnection::handleCmd_REGISTER()"

  virtual UserAuthenticationDatabase* getAuthenticationDatabaseForCommand(char const* cmdName);
  virtual Boolean specialClientAccessCheck(int clientSocket, struct sockaddr_in& clientAddr,
					   char const* urlSuffix);
      // a hook that allows subclassed servers to do server-specific access checking
      // on each client (e.g., based on client IP address), without using digest authentication.
  virtual Boolean specialClientUserAccessCheck(int clientSocket, struct sockaddr_in& clientAddr,
					       char const* urlSuffix, char const *username);
      // another hook that allows subclassed servers to do server-specific access checking
      // - this time after normal digest authentication has already taken place (and would otherwise allow access).
      // (This test can only be used to further restrict access, not to grant additional access.)

private: // redefined virtual functions
  virtual Boolean isGenericMediaServer() const;

public: // should be protected, but some old compilers complain otherwise
  class RTSPClientSession; // forward
  // The state of a TCP connection used by a RTSP client:
  class RTSPClientConnection {
  public:
    // A data structure that's used to implement the "REGISTER" command:
    class ParamsForREGISTER {
    public:
      ParamsForREGISTER(RTSPClientConnection* ourConnection, char const* url, char const* urlSuffix,
			Boolean reuseConnection, Boolean deliverViaTCP, char const* proxyURLSuffix);
      virtual ~ParamsForREGISTER();
    private:
      friend class RTSPClientConnection;
      RTSPClientConnection* fOurConnection;
      char* fURL;
      char* fURLSuffix;
      Boolean fReuseConnection, fDeliverViaTCP;
      char* fProxyURLSuffix;
    };
  protected:
    RTSPClientConnection(GenericMediaServer& ourServer, int clientSocket, struct sockaddr_in clientAddr);
    virtual ~RTSPClientConnection();

    friend class GenericMediaServer;
    friend class RTSPClientSession;
    // Make the handler functions for each command virtual, to allow subclasses to reimplement them, if necessary:
    virtual void handleCmd_OPTIONS();
        // You probably won't need to subclass/reimplement this function; reimplement "GenericMediaServer::allowedCommandNames()" instead.
    virtual void handleCmd_GET_PARAMETER(char const* fullRequestStr); // when operating on the entire server
    virtual void handleCmd_SET_PARAMETER(char const* fullRequestStr); // when operating on the entire server
    virtual void handleCmd_DESCRIBE(char const* urlPreSuffix, char const* urlSuffix, char const* fullRequestStr);
    virtual void handleCmd_REGISTER(char const* url, char const* urlSuffix, char const* fullRequestStr,
				    Boolean reuseConnection, Boolean deliverViaTCP, char const* proxyURLSuffix);
        // You probably won't need to subclass/reimplement this function;
        //     reimplement "GenericMediaServer::weImplementREGISTER()" and "GenericMediaServer::implementCmd_REGISTER()" instead.
    virtual void handleCmd_bad();
    virtual void handleCmd_notSupported();
    virtual void handleCmd_notFound();
    virtual void handleCmd_sessionNotFound();
    virtual void handleCmd_unsupportedTransport();
    // Support for optional RTSP-over-HTTP tunneling:
    virtual Boolean parseHTTPRequestString(char* resultCmdName, unsigned resultCmdNameMaxSize,
					   char* urlSuffix, unsigned urlSuffixMaxSize,
					   char* sessionCookie, unsigned sessionCookieMaxSize,
					   char* acceptStr, unsigned acceptStrMaxSize);
    virtual void handleHTTPCmd_notSupported();
    virtual void handleHTTPCmd_notFound();
    virtual void handleHTTPCmd_OPTIONS();
    virtual void handleHTTPCmd_TunnelingGET(char const* sessionCookie);
    virtual Boolean handleHTTPCmd_TunnelingPOST(char const* sessionCookie, unsigned char const* extraData, unsigned extraDataSize);
    virtual void handleHTTPCmd_StreamingGET(char const* urlSuffix, char const* fullRequestStr);
  protected:
    UsageEnvironment& envir() { return fOurServer.envir(); }
    void resetRequestBuffer();
    void closeSockets();
    static void incomingRequestHandler(void*, int /*mask*/);
    void incomingRequestHandler1();
    static void handleAlternativeRequestByte(void*, u_int8_t requestByte);
    void handleAlternativeRequestByte1(u_int8_t requestByte);
    void handleRequestBytes(int newBytesRead);
    Boolean authenticationOK(char const* cmdName, char const* urlSuffix, char const* fullRequestStr);
    void changeClientInputSocket(int newSocketNum, unsigned char const* extraData, unsigned extraDataSize);
      // used to implement RTSP-over-HTTP tunneling
    static void continueHandlingREGISTER(ParamsForREGISTER* params);
    virtual void continueHandlingREGISTER1(ParamsForREGISTER* params);

    // Shortcuts for setting up a RTSP response (prior to sending it):
    void setRTSPResponse(char const* responseStr);
    void setRTSPResponse(char const* responseStr, u_int32_t sessionId);
    void setRTSPResponse(char const* responseStr, char const* contentStr);
    void setRTSPResponse(char const* responseStr, u_int32_t sessionId, char const* contentStr);

    GenericMediaServer& fOurServer;
    Boolean fIsActive;
    int fClientInputSocket, fClientOutputSocket;
    struct sockaddr_in fClientAddr;
    unsigned char fRequestBuffer[RTSP_BUFFER_SIZE];
    unsigned fRequestBytesAlreadySeen, fRequestBufferBytesLeft;
    unsigned char* fLastCRLF;
    unsigned char fResponseBuffer[RTSP_BUFFER_SIZE];
    unsigned fRecursionCount;
    char const* fCurrentCSeq;
    Authenticator fCurrentAuthenticator; // used if access control is needed
    char* fOurSessionCookie; // used for optional RTSP-over-HTTP tunneling
    unsigned fBase64RemainderCount; // used for optional RTSP-over-HTTP tunneling (possible values: 0,1,2,3)
  };

  // The state of an individual client session (using one or more sequential TCP connections) handled by a RTSP server:
  class RTSPClientSession {
  protected:
    RTSPClientSession(GenericMediaServer& ourServer, u_int32_t sessionId);
    virtual ~RTSPClientSession();

    friend class GenericMediaServer;
    friend class RTSPClientConnection;
    // Make the handler functions for each command virtual, to allow subclasses to redefine them:
    virtual void handleCmd_SETUP(RTSPClientConnection* ourClientConnection,
				 char const* urlPreSuffix, char const* urlSuffix, char const* fullRequestStr);
    virtual void handleCmd_withinSession(RTSPClientConnection* ourClientConnection,
					 char const* cmdName,
					 char const* urlPreSuffix, char const* urlSuffix,
					 char const* fullRequestStr);
    virtual void handleCmd_TEARDOWN(RTSPClientConnection* ourClientConnection,
				    ServerMediaSubsession* subsession);
    virtual void handleCmd_PLAY(RTSPClientConnection* ourClientConnection,
				ServerMediaSubsession* subsession, char const* fullRequestStr);
    virtual void handleCmd_PAUSE(RTSPClientConnection* ourClientConnection,
				 ServerMediaSubsession* subsession);
    virtual void handleCmd_GET_PARAMETER(RTSPClientConnection* ourClientConnection,
					 ServerMediaSubsession* subsession, char const* fullRequestStr);
    virtual void handleCmd_SET_PARAMETER(RTSPClientConnection* ourClientConnection,
					 ServerMediaSubsession* subsession, char const* fullRequestStr);
  protected:
    UsageEnvironment& envir() { return fOurServer.envir(); }
    void deleteStreamByTrack(unsigned trackNum);
    void reclaimStreamStates();
    Boolean isMulticast() const { return fIsMulticast; }
    void noteLiveness();
    static void noteClientLiveness(RTSPClientSession* clientSession);
    static void livenessTimeoutTask(RTSPClientSession* clientSession);

    // Shortcuts for setting up a RTSP response (prior to sending it):
    void setRTSPResponse(RTSPClientConnection* ourClientConnection, char const* responseStr) { ourClientConnection->setRTSPResponse(responseStr); }
    void setRTSPResponse(RTSPClientConnection* ourClientConnection, char const* responseStr, u_int32_t sessionId) { ourClientConnection->setRTSPResponse(responseStr, sessionId); }
    void setRTSPResponse(RTSPClientConnection* ourClientConnection, char const* responseStr, char const* contentStr) { ourClientConnection->setRTSPResponse(responseStr, contentStr); }
    void setRTSPResponse(RTSPClientConnection* ourClientConnection, char const* responseStr, u_int32_t sessionId, char const* contentStr) { ourClientConnection->setRTSPResponse(responseStr, sessionId, contentStr); }

  protected:
    GenericMediaServer& fOurServer;
    u_int32_t fOurSessionId;
    ServerMediaSession* fOurServerMediaSession;
    Boolean fIsMulticast, fStreamAfterSETUP;
    unsigned char fTCPStreamIdCount; // used for (optional) RTP/TCP
    Boolean usesTCPTransport() const { return fTCPStreamIdCount > 0; }
    TaskToken fLivenessCheckTask;
    unsigned fNumStreamStates;
    struct streamState {
      ServerMediaSubsession* subsession;
      int tcpSocketNum;
      void* streamToken;
    } * fStreamStates;
  };

protected:
  // If you subclass "RTSPClientConnection", then you must also redefine this virtual function in order
  // to create new objects of your subclass:
  virtual RTSPClientConnection*
  createNewClientConnection(int clientSocket, struct sockaddr_in clientAddr);

  // If you subclass "RTSPClientSession", then you must also redefine this virtual function in order
  // to create new objects of your subclass:
  virtual RTSPClientSession*
  createNewClientSession(u_int32_t sessionId);

  // An iterator over our "ServerMediaSession" objects:
  class ServerMediaSessionIterator {
  public:
    ServerMediaSessionIterator(GenericMediaServer& server);
    virtual ~ServerMediaSessionIterator();
    ServerMediaSession* next();
  private:
    HashTable::Iterator* fOurIterator;
  };

private:
  static void incomingConnectionHandlerRTSP(void*, int /*mask*/);
  void incomingConnectionHandlerRTSP1();

  static void incomingConnectionHandlerHTTP(void*, int /*mask*/);
  void incomingConnectionHandlerHTTP1();

  void incomingConnectionHandler(int serverSocket);

  void noteTCPStreamingOnSocket(int socketNum, RTSPClientSession* clientSession, unsigned trackNum);
  void unnoteTCPStreamingOnSocket(int socketNum, RTSPClientSession* clientSession, unsigned trackNum);
  void stopTCPStreamingOnSocket(int socketNum);
#endif

protected:
  int fServerSocket;
  Port fServerPort;
  HashTable* fServerMediaSessions; // maps 'stream name' strings to "ServerMediaSession" objects

#if 0
private:
  friend class RTSPClientConnection;
  friend class RTSPClientSession;
  friend class ServerMediaSessionIterator;
  friend class RegisterRequestRecord;
  int fHTTPServerSocket; // for optional RTSP-over-HTTP tunneling
  Port fHTTPServerPort; // ditto
  HashTable* fClientConnections; // the "ClientConnection" objects that we're using
  HashTable* fClientConnectionsForHTTPTunneling; // maps client-supplied 'session cookie' strings to "RTSPClientConnection"s
    // (used only for optional RTSP-over-HTTP tunneling)
  HashTable* fClientSessions; // maps 'session id' strings to "RTSPClientSession" objects
  HashTable* fTCPStreamingDatabase;
    // maps TCP socket numbers to ids of sessions that are streaming over it (RTP/RTCP-over-TCP)
  HashTable* fPendingRegisterRequests;
  unsigned fRegisterRequestCounter;
  UserAuthenticationDatabase* fAuthDB;
  unsigned fReclamationTestSeconds;
  Boolean fAllowStreamingRTPOverTCP; // by default, True
};


////////// A subclass of "GenericMediaServer" that implements the "REGISTER" command to set up proxying on the specified URL //////////

class GenericMediaServerWithREGISTERProxying: public GenericMediaServer {
public:
  static GenericMediaServerWithREGISTERProxying* createNew(UsageEnvironment& env, Port ourPort = 554,
						   UserAuthenticationDatabase* authDatabase = NULL,
						   UserAuthenticationDatabase* authDatabaseForREGISTER = NULL,
						   unsigned reclamationTestSeconds = 65,
						   Boolean streamRTPOverTCP = False,
						   int verbosityLevelForProxying = 0);

protected:
  GenericMediaServerWithREGISTERProxying(UsageEnvironment& env, int ourSocket, Port ourPort,
				 UserAuthenticationDatabase* authDatabase, UserAuthenticationDatabase* authDatabaseForREGISTER,
				 unsigned reclamationTestSeconds,
				 Boolean streamRTPOverTCP, int verbosityLevelForProxying);
  // called only by createNew();
  virtual ~GenericMediaServerWithREGISTERProxying();

protected: // redefined virtual functions
  virtual char const* allowedCommandNames();
  virtual Boolean weImplementREGISTER(char const* proxyURLSuffix, char*& responseStr);
  virtual void implementCmd_REGISTER(char const* url, char const* urlSuffix, int socketToRemoteServer,
				     Boolean deliverViaTCP, char const* proxyURLSuffix);
  virtual UserAuthenticationDatabase* getAuthenticationDatabaseForCommand(char const* cmdName);

private:
  Boolean fStreamRTPOverTCP;
  int fVerbosityLevelForProxying;
  unsigned fRegisteredProxyCounter;
  char* fAllowedCommandNames;
  UserAuthenticationDatabase* fAuthDBForREGISTER;
#endif
}; 

#endif
