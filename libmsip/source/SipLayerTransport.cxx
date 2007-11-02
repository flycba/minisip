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

#include"SipLayerTransport.h"

#include<errno.h>
#include<stdio.h>

#ifdef WIN32
#include<winsock2.h>
#endif

#include<libmsip/SipResponse.h>
#include<libmsip/SipRequest.h>
#include<libmsip/SipException.h>
#include<libmsip/SipHeaderVia.h>
#include<libmsip/SipHeaderRoute.h>
#include<libmsip/SipHeaderContact.h>
#include<libmsip/SipHeaderTo.h>

#include<libmcrypto/TlsServerSocket.h>
#include<libmnetutil/ServerSocket.h>
#include<libmnetutil/NetworkException.h>
#include<libmnetutil/NetworkFunctions.h>
#include<libmnetutil/UDPSocket.h>
#include<libmcrypto/TlsSocket.h>
#include<libmnetutil/NetworkException.h>
#include<libmutil/Timestamp.h>
#include<libmutil/MemObject.h>
#include<libmutil/mtime.h>
#include<libmutil/dbg.h>
#include<libmutil/stringutils.h>
#include<libmsip/SipCommandString.h>
#include"SipCommandDispatcher.h"
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
					string messageString( (char *)buffer, index );
#ifdef ENABLE_TS
					char tmp[12];
					tmp[11]=0;
					memcpy(&tmp[0], buffer , 11);
					ts.save(tmp);
#endif
					init();
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
#ifdef ENABLE_TS
				char tmp[12];
				tmp[11]=0;
				memcpy(&tmp[0], buffer , 11);
				ts.save(tmp);
#endif
				string messageString( (char*)buffer, index );
				MRef<SipMessage*> msg = SipMessage::createMessage( messageString );
#ifdef ENABLE_TS
				ts.save("createMessage end");
#endif
				init();
				return msg;
				//return SipMessage::createMessage( messageString );
			}
		default:
			/*never reached*/
			massert(0);
			break;
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

class StreamThreadData : public InputReadyHandler{
	public:
		StreamThreadData( MRef<StreamSocket*>,
				  MRef<SipLayerTransport *> );
		void inputReady( MRef<Socket*> socket );

	protected:
		void streamSocketRead( MRef<StreamSocket *> socket );

	private:
		SipMessageParser parser;
		MRef<SipLayerTransport  *> transport;
		MRef<StreamSocket *> ssocket;
};


StreamThreadData::StreamThreadData( MRef<StreamSocket *> theSocket,
				    MRef<SipLayerTransport *> t)
		:ssocket( theSocket ){
	this->transport = t;
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
	mout << endl;
}

SipLayerTransport::SipLayerTransport(MRef<CertificateChain *> cchain,
				     MRef<CertificateSet *> cert_db_):
		cert_chain(cchain), cert_db(cert_db_), tls_ctx(NULL)
{
	contactUdpPort=0;
	contactTcpPort=0;
	contactTlsPort=0;
	manager = new SocketServer();
	manager->start();
}


bool SipLayerTransport::handleCommand(const SipSMCommand& command ){
	if( command.getType()==SipSMCommand::COMMAND_PACKET ){
		MRef<SipMessage*> pack = command.getCommandPacket();

		string branch = pack->getBranch();
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
	list<MRef<StreamThreadData*> >::iterator j;

	manager->stop();

		//tell the threads to stop. Don't "join" just yet
		//since that might take some time and we want that
		//time to be spent in parallel for all servers.
	list<MRef<SipSocketServer *> >::iterator i;
	for( i=servers.begin(); i != servers.end(); i++ ){
		MRef<SipSocketServer *> server = *i;
		server->stop();
	}

	manager->join();
	manager->closeSockets();
	manager = NULL;

		//wait for the threads in the servers.
		//NOTE: this can take about five seconds
		//(that is the read timeout in the socket
		//server). To improve this, check out
		//SipSocketServer.cxx
	for( i=servers.begin(); i != servers.end(); i++ ){
		MRef<SipSocketServer *> server = *i;
		server->join();
		server->closeSockets();
		server->free();
		server->setReceiver(NULL);
	}

	servers.clear();
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
			mdbg("signaling/sip") << "SipLayerTransport: Unknown transport protocol " + socket->getType() <<endl;
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

	//The Via header for CANCEL requests
	//is added when the packet is created
	if (pack->getType()=="CANCEL")
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
		if( IPAddress::isNumeric( addr ) ){
			res = true;
			if( !port ){
				port = getDefaultPort( transport );
			}
		}
		// Not numeric	
		else if( port ){
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
			mdbg("signaling/sip") << "SipLayerTransport: URI invalid " << endl;
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
			MRef<SipMessage*> resp = new SipResponse( 400, "Required header missing", (SipRequest*)*msg );
			resp->setSocket(msg->getSocket());
			sendMessage(resp, "TL", false);
		}

		return false;
	}

	return true;
}

void SipLayerTransport::updateContact(MRef<SipMessage*> pack,
				      MRef<SipSocketServer *> server,
				      MRef<Socket *> socket)
{
	MRef<SipHeaderValueContact*> contactp = pack->getHeaderValueContact();
	uint16_t port;
	string ip;
	string transport;

	if( !contactp )
		return;

	SipUri contactUri = contactp->getUri();

	if( contactUri.hasParameter("minisip") ){

		bool ipv6 = socket->getLocalAddress()->getType() == IP_ADDRESS_TYPE_V6;

		if( !server ){
			server = findServer(socket->getType(), ipv6);
		}

		transport = getSocketTransport( socket );
		getIpPort( server, socket, ip, port );

		contactUri.setIp( ip );
		contactUri.setTransport( transport );

		if(ipv6 || socket->getType() != SOCKET_TYPE_UDP){

			// Update port if not UDP and IPv4
			contactUri.setPort( port );
		}

		contactUri.removeParameter("minisip");
		contactp->setUri( contactUri );
	}
}

void SipLayerTransport::sendMessage(MRef<SipMessage*> pack, 
				      const string &ip_addr,
				      int32_t port, 
				      string branch,
				      string preferredTransport,
				      bool addVia)
{
	MRef<Socket *> socket;
	MRef<IPAddress *> tempAddr;
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
				cerr << "No socket!!" << endl;
				throw NetworkException();
			}
		}

 		updateContact( pack, server, socket );

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
			ts.save( tmp );
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
							(int32_t)packetString.length() ) == -1 )
				{

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
		mdbg("signaling/sip") << "Transport error in SipLayerTransport: " << message << endl;
		cerr << "SipLayerTransport: sendMessage: exception thrown! " << message << endl;
#endif
		CommandString transportError( pack->getBranch()+pack->getCSeqMethod(), 
					      SipCommandString::transport_error,
					      "SipLayerTransport: "+message );
		SipSMCommand transportErrorCommand(
				transportError, 
				SipSMCommand::transport_layer, 
				SipSMCommand::transaction_layer);

		if (dispatcher)
			dispatcher->enqueueCommand( transportErrorCommand, LOW_PRIO_QUEUE );
		else
			mdbg("signaling/sip")<< "SipLayerTransport: ERROR: NO SIP COMMAND RECEIVER - DROPPING COMMAND"<<endl;
	}
	
}

void SipLayerTransport::setDispatcher(MRef<SipCommandDispatcher*> d){
	dispatcher=d;
}

void SipLayerTransport::addSocket(MRef<StreamSocket *> sock){
	MRef<StreamThreadData*> worker = new StreamThreadData( sock, this );

	manager->addSocket( *sock, dynamic_cast<InputReadyHandler*>(*worker) );
}

void SipLayerTransport::removeSocket( MRef<StreamSocket *> sock ){
	manager->removeSocket( *sock );
}


MRef<StreamSocket *> SipLayerTransport::findStreamSocket( IPAddress &address, uint16_t port ){
	MRef<Socket *> sock =
		manager->findStreamSocketPeer( address, port );

	if( !sock ){
		return NULL;
	}

	return dynamic_cast<StreamSocket*>(*sock);
}

static void updateVia(MRef<SipMessage*> pack, MRef<IPAddress *>from,
		      uint16_t port)
{
	MRef<SipHeaderValueVia*> via = pack->getFirstVia();
	string peerAddr = from->getString();

	if( !via ){
		merr << "No Via header in incoming message!" << endl;
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

MRef<CertificateChain *> SipLayerTransport::getCertificateChain(){ 
	return cert_chain; 
}

MRef<Certificate*> SipLayerTransport::getMyCertificate(){ 
	return cert_chain->getFirst();
}

MRef<CertificateSet *> SipLayerTransport::getCertificateSet () {
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
				mdbg("signaling/sip") << "Some error occured while reading from UdpSocket"<<endl;
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
						mdbg("signaling/sip") << "SipLayerTransport: ERROR: NO SIP MESSAGE RECEIVER - DROPPING MESSAGE"<<endl;
				}
				pack=NULL;
			}
			
			catch(SipExceptionInvalidMessage & ){
				/* Probably we don't have enough data
				 * so go back to reading */
#ifdef DEBUG_OUTPUT
				mdbg("signaling/sip") << "Invalid data on UDP socket, discarded" << endl;
#endif
				return;
			}
			
			catch(SipExceptionInvalidStart & ){
				// This does not look like a SIP
				// packet, close the connection
				
#ifdef DEBUG_OUTPUT
				mdbg("signaling/sip") << "Invalid data on UDP socket, discarded" << endl;
#endif
				return;
			}
		} // if event
}

void StreamThreadData::inputReady( MRef<Socket*> socket ){
	streamSocketRead( ssocket );
}

#define STREAM_MAX_PKT_SIZE 65536

void StreamThreadData::streamSocketRead( MRef<StreamSocket *> socket ){
	char buffer[STREAM_MAX_PKT_SIZE+1];
	for (int i=0; i< STREAM_MAX_PKT_SIZE+1; i++){
		buffer[i]=0;
	}
	MRef<SipMessage*> pack;

			int32_t nread;
			nread = socket->read( buffer, STREAM_MAX_PKT_SIZE);

			if (nread == -1){
				mdbg("signaling/sip") << "Some error occured while reading from StreamSocket" << endl;
				return;
			}

			if ( nread == 0){
				// Connection was closed
				mdbg("signaling/sip") << "Connection was closed" << endl;
				transport->removeSocket( socket );
				return;
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
								mdbg("signaling/sip") << "SipLayerTransport: ERROR: NO SIP MESSAGE RECEIVER - DROPPING MESSAGE"<<endl;
						}
						pack=NULL;
					}

				}
			}
			
			catch(SipExceptionInvalidMessage &e ){
				mdbg("signaling/sip") << "INFO: SipLayerTransport::streamSocketRead: dropping malformed packet: "<<e.what()<<endl;

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
// 				continue;
			}
			
			catch(SipExceptionInvalidStart & ){
				// This does not look like a SIP
				// packet, close the connection
				
				mdbg("signaling/sip") << "This does not look like a SIP packet, close the connection" << endl;
				socket->close();
				transport->removeSocket( socket );
			}
}


/**
 *
 * @param externalPort  If the application wishes to override what port
 *   should be reported for the socket it is possible to specify such a port
 *   number here. This can be used for example to implement support for
 *   passing NATs with the help of a STUN server.
 */
MRef<SipSocketServer *> SipLayerTransport::createUdpServer( bool ipv6, const string &ipString, int32_t prefPort, int32_t externalPort)
{
	int32_t port = prefPort;
	int triesLeft=10;
	MRef<SipSocketServer *> server;
	bool fail;

	do {
		fail=false;
		try {

			MRef<DatagramSocket *> sock = new UDPSocket( port, ipv6 );
			server = new DatagramSocketServer( this, sock );

			// IPv6 doesn't need different external udp port
			// since it never is NATed.
			if( !ipv6 && externalPort ){
				server->setExternalPort( externalPort );
			}
			server->setExternalIp( ipString );


		} catch(const BindFailed &bf){
			fail=true;

			// If the port is already in use, try random port number in the range 2048 to 63488
			port = rand()%(0xFFFF-4096) + 2048; 
			triesLeft--;
			if (!triesLeft)
				throw;

		}
	}while(fail);

	if (externalPort>0)
		contactUdpPort=externalPort;
	else
		contactUdpPort=port;


	return server;
}

MRef<SipSocketServer *> SipLayerTransport::createTcpServer( bool ipv6, const string &ipString, int32_t prefPort)
{
	MRef<ServerSocket *> sock;
	MRef<SipSocketServer *> server;
	int32_t port = prefPort;
	bool fail;
	int triesLeft=10;

	do {
		fail=false;
		try {

			sock = ServerSocket::create( port, ipv6 );
			server = new StreamSocketServer( this, sock );
			server->setExternalIp( ipString );

		} catch ( const BindFailed &bf ){
			fail=true;
			triesLeft--;
			if (!triesLeft)
				throw;
		}
	} while ( fail );

	contactTcpPort = port;

	return server;
}

MRef<SipSocketServer *> SipLayerTransport::createTlsServer( bool ipv6, const string &ipString, int32_t prefPort, MRef<CertificateChain *> certChain, MRef<CertificateSet *> cert_db)
{
	MRef<ServerSocket *> sock;
	MRef<SipSocketServer *> server;
	int32_t port = prefPort;
	bool fail;
	int triesLeft=10;

	do {
		fail=false;
		try{
			sock = TLSServerSocket::create( ipv6, port, /*config->cert*/certChain->getFirst(),
					/*config->*/cert_db );
			server = new StreamSocketServer( this, sock );
			server->setExternalIp( ipString );
		} catch (const BindFailed &bf){
			fail=true;
			triesLeft--;
			if (!triesLeft)
				throw;
		}

	} while (fail);

	contactTlsPort = port;

	return server;
}

void SipLayerTransport::startUdpServer(const string &ipString, const string &ip6String, int32_t localUdpPort, int32_t externalContactUdpPort)
{
	MRef<SipSocketServer *> server;
/*
	string ipString;
	if( config->externalContactIP.size()>0 )
		ipString = config->externalContactIP;
	else
		ipString = config->localIpString;
*/

	server = createUdpServer( false, ipString, localUdpPort, externalContactUdpPort );
	addServer( server );

	if( /*config->localIp6String*/ ip6String != "" ){
		MRef<SipSocketServer *> server6;
		server6 = createUdpServer( true, /*config->localIp6String*/ ip6String,localUdpPort,externalContactUdpPort );
		addServer( server6 );
	}
}


void SipLayerTransport::startTcpServer( const string & ipString, const string & ip6String, int32_t prefPort)
{
	MRef<SipSocketServer *> server;

	server = createTcpServer( false, /*config->localIpString*/ ipString, prefPort);
	dispatcher->getLayerTransport()->addServer( server );

	if( /*config->localIp6String*/ ip6String != "" ){
		MRef<SipSocketServer *> server6;

		server6 = createTcpServer( true, /*config->localIp6String*/ ip6String, prefPort );
		addServer( server6 );
	}
}

void SipLayerTransport::startTlsServer( const string &ipString, const string &ip6String, int32_t prefPort, MRef<CertificateChain *> certChain, MRef<CertificateSet *> cert_db){
	MRef<SipSocketServer *> server;

	if( certChain.isNull() || certChain->getFirst().isNull() ){
		merr << "You need a personal certificate to run "
			"a TLS server. Please specify one in "
			"the certificate settings. minisip will "
			"now disable the TLS server." << endl;
		return;
	}

	server = createTlsServer( false, /*config->localIpString*/ ipString, prefPort, certChain, cert_db);
	addServer( server );

	if( /*config->localIp6String*/ ip6String != "" ){
		MRef<SipSocketServer *> server6;

		server6 = createTlsServer( true, /*config->localIp6String*/ ip6String, prefPort, certChain, cert_db );
		dispatcher->getLayerTransport()->addServer( server6 );
	}
}


int32_t SipLayerTransport::getLocalSipPort( const string &transport ) {
	if (transport=="TCP" || transport=="tcp")
		return contactTcpPort;
	if (transport=="TLS" || transport=="tls")
		return contactTlsPort;
	return contactUdpPort;
}


