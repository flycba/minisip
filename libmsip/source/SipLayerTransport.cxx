/*
  Copyright (C) 2005, 2004 Erik Eliasson, Johan Bilien
  Copyright (C) 2005-2006  Mikael Magnusson
  
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
 * Authors: Erik Eliasson <eliasson@it.kth.se>
 *          Johan Bilien <jobi@via.ecp.fr>
 *          Mikael Magnusson <mikma@users.sourceforge.net>
*/


#include<config.h>

#include<libmsip/SipLayerTransport.h>

#include<errno.h>
#include<stdio.h>

#ifdef WIN32
#include<winsock2.h>
//#include<io.h>
#endif

#include<libmsip/SipResponse.h>
#include<libmsip/SipRequest.h>
#include<libmsip/SipException.h>
#include<libmsip/SipHeaderVia.h>
#include<libmsip/SipHeaderRoute.h>
#include<libmsip/SipHeaderContact.h>
#include<libmsip/SipHeaderTo.h>

#include<libmcrypto/TLSSocket.h>
#include<libmnetutil/ServerSocket.h>
#include<libmnetutil/NetworkException.h>
#include<libmnetutil/NetworkFunctions.h>
#include<libmutil/Timestamp.h>
#include<libmutil/MemObject.h>
#include<libmutil/mtime.h>
#include<libmutil/dbg.h>
#include<libmutil/stringutils.h>
//#include<libmsip/SipDialogContainer.h>
#include<libmsip/SipCommandString.h>
#include<libmsip/SipCommandDispatcher.h>
#include<libmsip/SipHeaderFrom.h>

#include<cctype>
#include<string>
#include<algorithm>

using namespace std;

#define TIMEOUT 600000
#define NB_THREADS 5
#define BUFFER_UNIT 1024

#if !defined(_MSC_VER) && !defined(__MINGW32__)
# define ENABLE_TS
#endif

#ifndef SOCKET
# ifdef WIN32
#  define SOCKET uint32_t
# else
#  define SOCKET int32_t
# endif
#endif

#ifdef DEBUG_UDPPACKETDROPEMUL
Mutex dropStringLock;
string dropStringOut;
string dropStringIn;

void setDropFilterOut(string s){
	dropStringLock.lock();
	dropStringOut=s;
	dropStringLock.unlock();
}

void setDropFilterIn(string s){
	dropStringLock.lock();
	dropStringIn=s;
	dropStringLock.unlock();
}

static bool dropOut(){
	char c='1';
	dropStringLock.lock();
	if (dropStringOut.size()>0){
		c = dropStringOut[0];
		dropStringOut = dropStringOut.substr(1);
	}
	dropStringLock.unlock();
	if (c=='0'){
		return true;
	}else{
		return false;
	}
}

static bool dropIn(){
	char c='1';
	dropStringLock.lock();
	if (dropStringIn.size()>0){
		c = dropStringIn[0];
		dropStringIn = dropStringIn.substr(1);
	}
	dropStringLock.unlock();
	if (c=='0'){
		return true;
	}else{
		return false;
	}
}

#endif


// 
// SipMessageParser
// 
class SipMessageParser{
	public:
		SipMessageParser();
		~SipMessageParser();

		MRef<SipMessage *> feed( uint8_t data );
		void init();
	private:
		void expandBuffer();
		uint32_t findContentLength();
		uint8_t * buffer;
		uint32_t length;
		uint32_t index;
		uint8_t state;
		uint32_t contentIndex;
		uint32_t contentLength;
};

SipMessageParser::SipMessageParser(){
	buffer = (uint8_t *)malloc( BUFFER_UNIT * sizeof( uint8_t ) );
	for (unsigned int i=0; i< BUFFER_UNIT * sizeof( uint8_t ); i++)
		buffer[i]=0;

	length = BUFFER_UNIT;
	index = 0;
	contentIndex = 0;
	state = 0;

}

void SipMessageParser::init(){
	buffer = (uint8_t *)realloc(buffer, BUFFER_UNIT * sizeof( uint8_t ) );
	for (unsigned int i=0; i< BUFFER_UNIT * sizeof( uint8_t ); i++)
		buffer[i]=0;
	length = BUFFER_UNIT;
	state = 0;
	index = 0;
	contentIndex = 0;
}

SipMessageParser::~SipMessageParser(){
	free( buffer );
}

MRef<SipMessage*> SipMessageParser::feed( uint8_t udata ){
	char data = (char)udata;
	if( index >= length ){
		expandBuffer();
	}

	buffer[index++] = udata;

	switch( state ){
		case 0:
			if( data == '\n' )
				state = 1;
			break;
		case 1:
			if( data == '\n' ){
				/* Reached the end of the Header */
				state = 2;
				contentLength = findContentLength();
				if( contentLength == 0 ){
					char tmp[12];
					tmp[11]=0;
					memcpy(&tmp[0], buffer , 11);
					string messageString( (char *)buffer, index );
					init();
#ifdef ENABLE_TS
					ts.save(tmp);
#endif
					MRef<SipMessage*> msg = SipMessage::createMessage( messageString );
#ifdef ENABLE_TS
					ts.save("createMessage end");
#endif
					return msg;
					
					//return SipMessage::createMessage( messageString );
				}
				contentIndex = 0;
			}
			else if( data != '\r' )
				state = 0;
			break;
		case 2:
			if( ++contentIndex == contentLength ){
				char tmp[12];
				tmp[11]=0;
				memcpy(&tmp[0], buffer , 11);
				string messageString( (char*)buffer, index );
				init();
#ifdef ENABLE_TS
				ts.save(tmp);
#endif
				MRef<SipMessage*> msg = SipMessage::createMessage( messageString );
#ifdef ENABLE_TS
				ts.save("createMessage end");
#endif
				return msg;
				//return SipMessage::createMessage( messageString );
			}
	}
	return NULL;
}


void SipMessageParser::expandBuffer(){
	buffer = (uint8_t *)realloc( buffer, BUFFER_UNIT * ( length / BUFFER_UNIT + 1 ) );
	length += BUFFER_UNIT;
}

uint32_t SipMessageParser::findContentLength(){
	uint32_t i = 0;
	const char * contentLengthString = "\nContent-Length: ";

	for( i = 0; i + 17 < index; i++ ){
		if( strNCaseCmp( contentLengthString, (char *)(buffer + i) , 17  ) == 0 ){
			uint32_t j = 0;
			string num;
			
			while( i + j + 17 < index && (buffer[i+j+17] == ' ' || buffer[i+j+17] == '\t') ){
				j++;
			}
			
			for( ; i + 17 + j < index ; j++ ){
				if( buffer[i+j+17] >= '0' && buffer[i+j+18] <= '9' ){
					num += buffer[i+j+17];
				}
				else break;
			}
			return atoi( num.c_str() );
		}
	}
	return 0;
}

class StreamThreadData{
	public:
		StreamThreadData( MRef<SipLayerTransport *> );
		SipMessageParser parser;
		MRef<SipLayerTransport  *> transport;
		void run();
		void streamSocketRead( MRef<StreamSocket *> socket );
};

StreamThreadData::StreamThreadData( MRef<SipLayerTransport *> transport){
	this->transport = transport;
}


bool sipdebug_print_packets=false;

void set_debug_print_packets(bool f){
	sipdebug_print_packets=f;
}

bool get_debug_print_packets(){
	return sipdebug_print_packets;
}

uint64_t startTime = 0;

void printMessage(string header, string packet){
	if (startTime==0)
		startTime = mtime();
	uint64_t t;
	t=mtime();
	int64_t sec = t / 1000 - startTime / 1000;
	int64_t msec = t - startTime;
	msec = msec%1000;

	header = (sec<100?string("0"):string("")) + 
		(sec<10?"0":"") + 
		itoa((int)sec)+
		":"+
		(msec<10?"0":"")+
		(msec<100?"0":"")+
		itoa((int)msec)+ 
		" " + 
		header;

	size_t strlen=packet.size();
	mout << header<<": ";
	for (size_t i=0; i<strlen; i++){
		mout << packet[i];
		if (packet[i]=='\n')
			mout << header<<": ";
	}
	mout << end;
}

static void * streamThread( void * arg );

SipLayerTransport::SipLayerTransport(MRef<certificate_chain *> cchain,
				     MRef<ca_db *> cert_db):
		cert_chain(cchain), cert_db(cert_db), tls_ctx(NULL)
{
	int i;

	for( i=0; i < NB_THREADS ; i++ ){
            Thread::createThread(streamThread, new StreamThreadData(this));
	}
}


bool SipLayerTransport::handleCommand(const SipSMCommand& command ){
	if( command.getType()==SipSMCommand::COMMAND_PACKET ){
		MRef<SipMessage*> pack = command.getCommandPacket();

		string branch = pack->getDestinationBranch();
		bool addVia = pack->getType() != SipResponse::type;

		if (branch==""){
			branch = "z9hG4bK" + itoa(rand());		//magic cookie plus random number
		}

		sendMessage(pack, branch, addVia);
		return true;
	}

	return 0;
}

SipLayerTransport::~SipLayerTransport() {
}
  
void SipLayerTransport::stop(){
	serversLock.lock();
	list<MRef<SipSocketServer *> >::iterator i;

	for( i=servers.begin(); i != servers.end(); i++ ){
		MRef<SipSocketServer *> server = *i;

		server->stop();
	}
	serversLock.unlock();
}

void SipLayerTransport::addServer( MRef<SipSocketServer *> server )
{
	serversLock.lock();
	server->start();
	servers.push_back( server );
	serversLock.unlock();
}
  
MRef<SipSocketServer *> SipLayerTransport::findServer(int32_t type, bool ipv6)
{
	list<MRef<SipSocketServer *> >::iterator i;

	for( i=servers.begin(); i != servers.end(); i++ ){
		MRef<SipSocketServer *> server = *i;

		if( server->isIpv6() == ipv6 &&
		    server->getType() == type ){
			return server;
		}
	}
#ifdef DEBUG_OUTPUT
	cerr << "SipLayerTransport::findServer not found type=" << type << " ipv6=" << ipv6 << endl;
#endif
	return NULL;
}

MRef<Socket *> SipLayerTransport::findServerSocket(int32_t type, bool ipv6)
{
	MRef<SipSocketServer *> server;
	serversLock.lock();
	server = findServer(type, ipv6);
	serversLock.unlock();

	if( !server ){
		return NULL;
	}

	MRef<Socket *> sock = server->getSocket();

	return sock;
}


string getSocketTransport( MRef<Socket*> socket )
{
	switch( socket->getType() ){
		case SOCKET_TYPE_TLS:
			return "TLS";

		case SOCKET_TYPE_TCP:
			return "TCP";
			
		case SOCKET_TYPE_UDP:
			return "UDP";

		default:
			mdbg<< "SipLayerTransport: Unknown transport protocol " + socket->getType() <<end;
			// TODO more describing exception and message
			throw NetworkException();
	}
}

void getIpPort( MRef<SipSocketServer*> server, MRef<Socket*> socket,
		string &ip, uint16_t &port )
{
	if( server ){
		port = server->getExternalPort();
		ip = server->getExternalIp();
	}
	else {
		port = socket->getPort();
		ip = socket->getLocalAddress()->getString();
	}
}


void SipLayerTransport::addViaHeader( MRef<SipMessage*> pack,
									MRef<SipSocketServer *> server,
									MRef<Socket *> socket,
									string branch ){
	string transport;
	uint16_t port;
	string ip;

	if( !socket )
		return;

	transport = getSocketTransport( socket );

	getIpPort( server, socket, ip, port );
	
	MRef<SipHeaderValue*> hdrVal = 
		new SipHeaderValueVia(transport, ip, port);

	// Add rport parameter, defined in RFC 3581
	hdrVal->addParameter(new SipHeaderParameter("rport", "", false));
	hdrVal->setParameter("branch",branch);
	
	MRef<SipHeader*> hdr = new SipHeader( hdrVal );

	pack->addBefore( hdr );
}

static int32_t getDefaultPort(const string &transport)
{
	if( transport == "TLS" ){
		return 5061;
	}
	else{
		return 5060;
	}
}


static bool lookupDestSrv(const string &domain, const string &transport,
			  string &destAddr, int32_t &destPort)
{
	//Do a SRV lookup according to the transport ...
	string srv;
	uint16_t port = 0;

	if( transport == "TLS" || transport == "tls") { srv = "_sips._tls"; }
	else if( transport == "TCP" || transport == "tcp") { srv = "_sip._tcp"; }
	else { //if( trans == "UDP" || trans == "udp") { 	
		srv = "_sip._udp"; 
	}

	string addr = NetworkFunctions::getHostHandlingService(srv, domain, port);
#ifdef DEBUG_OUTPUT
	cerr << "getDestIpPort : srv=" << srv << "; domain=" << domain << "; port=" << port << "; target=" << addr << endl;
#endif

	if( addr.size() > 0 ){
		destAddr = addr;
		destPort = port;
		return true;
	}

	return false;
}

// RFC 3263 4.2 Determining Port and IP Address
static bool lookupDestIpPort(const SipUri &uri, const string &transport,
			     string &destAddr, int32_t &destPort)
{
	bool res = false;

	if( !uri.isValid() )
		return false;

	string addr = uri.getIp();
	int32_t port = uri.getPort();

	if( addr.size()>0 ){
		// TODO: Check if numeric
#if 0
		if( isNumericAddr( addr ) ){
			if( !port ){
				port = getDefaultPort( transport );
				res = true;
			}
		}
		// Not numeric	
		else
#endif
		if( port ){
			// Lookup A or AAAA
			res = true;
		}
		// Lookup SRV
		else if( lookupDestSrv( uri.getIp(), transport,
					addr, port )){
			res = true;
		}
		else{
			// Lookup A or AAAA
			port = getDefaultPort( transport );
			res = true;
		}
	}

	if( res ){
		destAddr = addr;
		destPort = port;
	}

	return res;
}


// Impl RFC 3263 (partly)
bool SipLayerTransport::getDestination(MRef<SipMessage*> pack, string &destAddr,
			   int32_t &destPort, string &destTransport)
{
	if( pack->getType() == SipResponse::type ){
		// RFC 3263, 5 Server Usage
		// Send responses to sent by address in top via.

		MRef<SipHeaderValueVia*> via = pack->getFirstVia();

		if ( via ){
			string peer = via->getParameter( "received" );
			if( peer == "" ){
				peer = via->getIp();
			}

			//destAddr = IPAddress::create( via->getIp() );
			destAddr = via->getIp();
			if( destAddr.size()>0 ){
				string rport = via->getParameter( "rport" );
				if( rport != "" ){
					destPort = atoi( rport.c_str() );
				}
				else{
					destPort = via->getPort();
				}
				if( !destPort ){
					destPort = 5060;
				}
				destTransport = via->getProtocol();
				return true;
			}
		}
	}
	else{
		// RFC 3263, 4 Client Usage

		// Send requests to address in first route if the route set
		// is non-empty, or directly to the reqeuest uri if the 
		// route set is empty.

		MRef<SipHeaderValue*> routeHeader =
			pack->getHeaderValueNo(SIP_HEADER_TYPE_ROUTE, 0);

		SipUri uri;

		if( routeHeader ){
			MRef<SipHeaderValueRoute*> route =
				(SipHeaderValueRoute*)*routeHeader;
			string str = route->getString();
			uri.setUri( str );
		}
		else {
			MRef<SipRequest*> req = (SipRequest*)*pack;
			uri = req->getUri();
		}

		if( uri.isValid() ){
			// RFC 3263, 4.1 Selecting a Transport Protocol
			// TODO: Support NAPTR

			//destAddr = IPAddress::create( uri.getIp() );
			destAddr = uri.getIp();
			
			if( /*destAddr*/ destAddr.size()>0 ){
				destTransport = uri.getTransport();
				if( destTransport.length() == 0 ){
					if( uri.getProtocolId() == "sips" )
						destTransport = "TLS";
					else{
						if (findServerSocket(SOCKET_TYPE_UDP, false)){
							destTransport="UDP";
						}else
						if (findServerSocket(SOCKET_TYPE_TCP, false)){
							destTransport="TCP";
						}else
						if (findServerSocket(SOCKET_TYPE_TLS, false)){
							destTransport = "TLS";
						}else{
							merr << "SipMessateTransport: Warning: could not find any supported transport - trying UDP"<<endl;
							destTransport = "UDP"; // this should not happen
						}
					}
				}
				return lookupDestIpPort(uri, destTransport, 
							destAddr, destPort);
			}
		}
		else{
			mdbg << "SipLayerTransport: URI invalid " << end;
		}
	}

	return false;
}

void SipLayerTransport::sendMessage(MRef<SipMessage*> pack, 
				      const string &branch,
				      bool addVia)
{
	//MRef<IPAddress*> destAddr;
	string destAddr;
	int32_t destPort = 0;
	string destTransport;

	if( !getDestination( pack, destAddr, destPort, destTransport) ){
#ifdef DEBUG_OUTPUT
		cerr << "SipLayerTransport: WARNING: Could not find destination. Packet dropped."<<endl;
#endif
		return;
	}

	transform( destTransport.begin(), destTransport.end(),
		   destTransport.begin(), (int(*)(int))toupper );

	sendMessage( pack, /* **destAddr */ destAddr, destPort,
		     branch, destTransport, addVia );
}


bool SipLayerTransport::findSocket(const string &transport,
				   IPAddress &destAddr,
				   uint16_t port,
				   MRef<SipSocketServer*> &server,
				   MRef<Socket*> &socket)
{
	bool ipv6 = false;
	int32_t type = 0;

	ipv6 = (destAddr.getType() == IP_ADDRESS_TYPE_V6);

	if( transport == "UDP" ){
		type = SOCKET_TYPE_UDP;
	}
	else if( transport == "TCP" ){
		type = SOCKET_TYPE_TCP;
	}
	else if( transport == "TLS" ){
		type = SOCKET_TYPE_TLS;
	}

	serversLock.lock();
	server = findServer(type, ipv6);
	serversLock.unlock();

	if( type & SOCKET_TYPE_STREAM ){
		MRef<StreamSocket*> ssocket = findStreamSocket(destAddr, port);
		if( ssocket.isNull() ) {
			/* No existing StreamSocket to that host,
			 * create one */
			cerr << "SipLayerTransport: sendMessage: creating new socket" << endl;
			if( transport == "TLS" ){
				ssocket = TLSSocket::connect( destAddr, 
							port, getMyCertificate(),
							cert_db );
			}
			else{ /* TCP */
				ssocket = new TCPSocket( destAddr, port );
			}

			addSocket( ssocket );
		} else cerr << "SipLayerTransport: sendMessage: reusing old socket" << endl;
		socket = *ssocket;
	}
	else{
		if( server ){
			socket = server->getSocket();
		}
	}

	if( !socket ){
		throw NetworkException();
	}

	return !socket.isNull();
}

bool SipLayerTransport::validateIncoming(MRef<SipMessage *> msg){
	bool isRequest = (msg->getType() != SipResponse::type);
	bool isInvite = (msg->getType() == "INVITE");
	// check that required headers are present
	

/*	if (!msg->getHeaderValueFrom()){
		//too severely damaged to answer (could try, but why bother?)
		return false;
	}
*/

	if (!msg->getHeaderValueFrom() 
			|| !msg->getHeaderValueTo()
			|| (isInvite && !msg->getHeaderValueNo(SIP_HEADER_TYPE_CONTACT,0))){
		if (isRequest){
			MRef<SipMessage*> resp = new SipResponse(msg->getFirstViaBranch(),
				   400, "Required header missing", msg );
			resp->setSocket(msg->getSocket());
			sendMessage(resp, "TL", false);
		}

		return false;
	}

	return true;
}

// Set contact uri host and port to external ip and port configured
// on the server or local address and port of the socket
void updateContact(MRef<SipMessage*> pack,
		   MRef<SipSocketServer *> server,
		   MRef<Socket *> socket)
{
	MRef<SipHeaderValueContact*> contactp = pack->getHeaderValueContact();
	uint16_t port;
	string ip;
	string transport;

	if( !contactp )
		return;

	transport = getSocketTransport( socket );
	getIpPort( server, socket, ip, port );

	SipUri contactUri = contactp->getUri();

	contactUri.setIp( ip );
	contactUri.setPort( port );
	contactUri.setTransport( transport );
	contactp->setUri( contactUri );
}

void SipLayerTransport::sendMessage(MRef<SipMessage*> pack, 
				    /* IPAddress &*/ const string &ip_addr,
				      int32_t port, 
				      string branch,
				      string preferredTransport,
				      bool addVia)
{
	MRef<Socket *> socket;
	MRef<IPAddress *> tempAddr;
	MRef<IPAddress *> destAddr;
	MRef<SipSocketServer *> server;
#ifdef DEBUG_OUTPUT
	cerr << "SipLayerTransport:  sendMessage addr=" << ip_addr << ", port=" << port << endl;
#endif

				
	try{
		socket = pack->getSocket();
		MRef<IPAddress *>destAddr;

		if( !socket ){
			// Lookup IPv4 or IPv6 address
			destAddr = IPAddress::create(ip_addr);
		}
		else{
			// Lookup IPv4 or IPv6 depending on open socket
			int32_t type = socket->getLocalAddress()->getType();
			destAddr = IPAddress::create(ip_addr, type == IP_ADDRESS_TYPE_V6);
		}

		if( !destAddr ){
			throw HostNotFound( ip_addr );
		}

		if( !socket ){
			findSocket(preferredTransport, **destAddr, (uint16_t)port, server, socket);
			pack->setSocket( socket );

			if( !socket ){
				// TODO add sensible message
				throw NetworkException();
			}

			updateContact( pack, server, socket );
		}

		if (addVia){
			addViaHeader( pack, server, socket, branch );
		}

		string packetString = pack->getString();

		MRef<DatagramSocket *> dsocket = dynamic_cast<DatagramSocket*>(*socket);
		MRef<StreamSocket *> ssocket = dynamic_cast<StreamSocket*>(*socket);
		
		if( ssocket ){
			/* At this point if socket != we send on a 
			 * streamsocket */
			if (sipdebug_print_packets){
				printMessage("OUT (STREAM)", packetString);
			}
#ifdef ENABLE_TS
			//ts.save( PACKET_OUT );
			char tmp[12];
			tmp[11]=0;
			memcpy(&tmp[0], packetString.c_str() , 11);
#endif
			if( ssocket->write( packetString ) == -1 ){
				throw SendFailed( errno );
			}
		}
		else if( dsocket ){
			/* otherwise use the UDP socket */
			if (sipdebug_print_packets){
				printMessage("OUT (UDP)", packetString);
			}
#ifdef ENABLE_TS
			//ts.save( PACKET_OUT );
			char tmp[12];
			tmp[11]=0;
			memcpy(&tmp[0], packetString.c_str() , 11);
			ts.save( tmp );

#endif
// 			MRef<IPAddress *>destAddr = IPAddress::create(ip_addr);

			
#ifdef DEBUG_UDPPACKETDROPEMUL
				if (!dropOut())
#endif
				if( dsocket->sendTo( **destAddr, port, 
							(const void*)packetString.c_str(),
							(int32_t)packetString.length() ) == -1 ){

					throw SendFailed( errno );

				}
		}
		else{
			cerr << "No valid socket!" << endl;
		}
	}
	catch( NetworkException & exc ){
		string message = exc.what();
		string callId = pack->getCallId();
#ifdef DEBUG_OUTPUT
		mdbg << "Transport error in SipLayerTransport: " << message << end;
		cerr << "SipLayerTransport: sendMessage: exception thrown!" << endl;
#endif
		CommandString transportError( callId, 
					      SipCommandString::transport_error,
					      "SipLayerTransport: "+message );
		SipSMCommand transportErrorCommand(
				transportError, 
				SipSMCommand::transport_layer, 
				SipSMCommand::transaction_layer);

		if (dispatcher)
			dispatcher->enqueueCommand( transportErrorCommand, LOW_PRIO_QUEUE );
		else
			mdbg<< "SipLayerTransport: ERROR: NO SIP COMMAND RECEIVER - DROPPING COMMAND"<<end;
	}
	
}

void SipLayerTransport::setDispatcher(MRef<SipCommandDispatcher*> d){
	dispatcher=d;
}

void SipLayerTransport::addSocket(MRef<StreamSocket *> sock){
	socksLock.lock();
	this->socks.push_back(sock);
	socksLock.unlock();
	socksPendingLock.lock();
	this->socksPending.push_back(sock);
	socksPendingLock.unlock();
        semaphore.inc();
}

MRef<StreamSocket *> SipLayerTransport::findStreamSocket( IPAddress &address, uint16_t port ){
	list<MRef<StreamSocket *> >::iterator i;

	socksLock.lock();
	for( i=socks.begin(); i != socks.end(); i++ ){
		if( (*i)->matchesPeer(address, port) ){
			socksLock.unlock();
			return *i;
		}
	}
	socksLock.unlock();
	return NULL;
}

static void updateVia(MRef<SipMessage*> pack, MRef<IPAddress *>from,
		      uint16_t port)
{
	MRef<SipHeaderValueVia*> via = pack->getFirstVia();
	string peerAddr = from->getString();

	if( !via ){
		merr << "No Via header in incoming message!" << end;
		return;
	}

	if( via->hasParameter( "rport" ) ){
		char buf[20] = "";
		sprintf(buf, "%d", port);
		via->setParameter( "rport", buf);
	}
	
	string addr = via->getIp();
	if( addr != peerAddr ){
		via->setParameter( "received", peerAddr );
	}
}

MRef<certificate_chain *> SipLayerTransport::getCertificateChain(){ 
	return cert_chain; 
}

MRef<certificate*> SipLayerTransport::getMyCertificate(){ 
	return cert_chain->get_first();
}

MRef<ca_db *> SipLayerTransport::getCA_db () {
	return cert_db;
}



#define UDP_MAX_SIZE 65536

void SipLayerTransport::datagramSocketRead(MRef<DatagramSocket *> sock){
	char buffer[UDP_MAX_SIZE];

	MRef<SipMessage*> pack;
	int32_t nread;
	
	if( sock ){
			MRef<IPAddress *> from;
			int32_t port = 0;

			nread = sock->recvFrom((void *)buffer, UDP_MAX_SIZE, from, port);
			
			if (nread == -1){
				mdbg << "Some error occured while reading from UdpSocket"<<end;
				return;
			}

			if ( nread == 0){
				// Connection was closed
				return; // FIXME
			}

#ifdef DEBUG_UDPPACKETDROPEMUL
			if (dropIn()){
				return;
			}
#endif

			if (nread < (int)strlen("SIP/2.0")){
				return;
			}


			try{
#ifdef ENABLE_TS
				//ts.save( PACKET_IN );
				char tmp[12];
				tmp[11]=0;
				memcpy(&tmp[0], buffer, 11); 
				ts.save( tmp );

#endif
				string data = string(buffer, nread);
				if (sipdebug_print_packets){
					printMessage("IN (UDP)", data);
				}
				pack = SipMessage::createMessage( data );
				
				pack->setSocket( *sock );
				updateVia(pack, from, (uint16_t)port);
				
				if (validateIncoming(pack)){ // drop here if it does not look ok
					SipSMCommand cmd(pack, 
							SipSMCommand::transport_layer, 
							SipSMCommand::transaction_layer);
					if (dispatcher)
						dispatcher->enqueueCommand( cmd, LOW_PRIO_QUEUE );
					else
						mdbg<< "SipLayerTransport: ERROR: NO SIP MESSAGE RECEIVER - DROPPING MESSAGE"<<end;
				}
				pack=NULL;
			}
			
			catch(SipExceptionInvalidMessage & ){
				/* Probably we don't have enough data
				 * so go back to reading */
#ifdef DEBUG_OUTPUT
				mdbg << "Invalid data on UDP socket, discarded" << end;
#endif
				return;
			}
			
			catch(SipExceptionInvalidStart & ){
				// This does not look like a SIP
				// packet, close the connection
				
#ifdef DEBUG_OUTPUT
				mdbg << "Invalid data on UDP socket, discarded" << end;
#endif
				return;
			}
		} // if event
}

void StreamThreadData::run(){
	while(true){

		MRef<StreamSocket *> socket;
                transport->semaphore.dec();
                
		/* Take the last socket pending to be read */
		transport->socksPendingLock.lock();
		socket = transport->socksPending.front();
		transport->socksPending.pop_front();
		transport->socksPendingLock.unlock();
                
		/* Read from it until it gets closed */
		streamSocketRead( socket );

		/* The socket was closed */
		transport->socksLock.lock();
		transport->socks.remove( socket );
		transport->socksLock.unlock();
#ifdef DEBUG_OUTPUT
		mdbg << "StreamSocket closed" << end;
#endif

		parser.init();
	}
}

#define STREAM_MAX_PKT_SIZE 65536

void StreamThreadData::streamSocketRead( MRef<StreamSocket *> socket ){
	char buffer[STREAM_MAX_PKT_SIZE+1];
	for (int i=0; i< STREAM_MAX_PKT_SIZE+1; i++){
		buffer[i]=0;
	}
	int avail;
	MRef<SipMessage*> pack;

	while( true ){
		fd_set set;

		do{
			struct timeval tv;
			// Timeout needs to be set before each call to select since
			// it should be consider undefined after select() returns.
			tv.tv_sec = 600;
			tv.tv_usec = 0;

			// We need update the fd set before call to select()
			FD_ZERO(&set);
			FD_SET((SOCKET)socket->getFd(), &set);

			avail = select(socket->getFd()+1,&set,NULL,NULL,&tv );
		} while( avail <= 0 );

		if( avail == 0 ){
#ifdef DEBUG_OUTPUT
			mdbg << "Closing Stream socket due to inactivity" << end;
#endif
			break;
		}

		if( FD_ISSET( socket->getFd(), &set )){
			int32_t nread;
			nread = socket->read( buffer, STREAM_MAX_PKT_SIZE);

			if (nread == -1){
				mdbg << "Some error occured while reading from StreamSocket" << end;
				continue;
			}

			if ( nread == 0){
				// Connection was closed
				mdbg << "Connection was closed" << end;
				break;
			}
#ifdef ENABLE_TS
			//ts.save( PACKET_IN );


#endif

			try{
				uint32_t i;
				for( i = 0; i < (uint32_t)nread; i++ ){
					pack = parser.feed( buffer[i] );
					
					if( pack ){
						if (sipdebug_print_packets){
						printMessage("IN (STREAM)", buffer);
						}
						//cerr << "Packet string:\n"<< pack->getString()<< "(end)"<<endl;

						MRef<IPAddress *> peer = socket->getPeerAddress();
						pack->setSocket( *socket );
						updateVia( pack, peer, (int16_t)socket->getPeerPort() );
						
						if (transport->validateIncoming(pack)){ // drop here if it does not look ok
							SipSMCommand cmd(pack, SipSMCommand::transport_layer, SipSMCommand::transaction_layer);
							if (transport->dispatcher){
								transport->dispatcher->enqueueCommand( cmd, LOW_PRIO_QUEUE );
							}else
								mdbg<< "SipLayerTransport: ERROR: NO SIP MESSAGE RECEIVER - DROPPING MESSAGE"<<end;
						}
						pack=NULL;
					}

				}
			}
			
			catch(SipExceptionInvalidMessage &e ){
				mdbg << "INFO: SipLayerTransport::streamSocketRead: dropping malformed packet: "<<e.what()<<endl;

#if 0
				// Check that we received data
				// is not too big, in which case close
				// the socket, to avoid DoS
				if(socket->received.size() > 8192){
					break;
				}
#endif
				/* Probably we don't have enough data
				 * so go back to reading */
				continue;
			}
			
			catch(SipExceptionInvalidStart & ){
				// This does not look like a SIP
				// packet, close the connection
				
				mdbg << "This does not look like a SIP packet, close the connection" << endl;
				break;
			}
		} // if event
	}// while true
}

static void * streamThread( void * arg ){
	StreamThreadData * data;
	data = (StreamThreadData *)arg;

	data->run();
	return NULL;
}

