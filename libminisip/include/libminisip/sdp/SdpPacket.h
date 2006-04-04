/*
 Copyright (C) 2004-2006 the Minisip Team
 
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
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

/* Copyright (C) 2004 
 *
 * Authors: Erik Eliasson <eliasson@it.kth.se>
 *          Johan Bilien <jobi@via.ecp.fr>
*/

/* Name
 * 	SdpPacket.h
 * Author
 * 	Erik Eliasson, eliasson@it.kth.se
 * Purpose
 * 
*/


#ifndef SDPPACKET_H
#define SDPPACKET_H

#include<libmnetutil/IPAddress.h>
#include<vector>
#include<libminisip/sdp/SdpPacket.h>
#include<libminisip/sdp/SdpHeader.h>
//#include<libmsip/CODECInterface.h>
#include<libmutil/MemObject.h>
#include<libmsip/SipMessageContent.h>
#include<libmsip/SipMessageContentFactory.h>

MRef<SipMessageContent*> sdpSipMessageContentFactory(const std::string & buf, const std::string & ContentType);

class SdpPacket : public SipMessageContent{
	public:
		SdpPacket();
		SdpPacket(std::string build_from);
	//	SdpPacket(string ipAddr, int32_t local_media_port, vector<CODECInterface *> &codecs);
	//	SdpPacket(string ipAddr, int32_t local_media_port, vector<CODECInterface *> &codecs, string key_mgmt);
	
		virtual std::string getMemObjectType(){return "SdpPacket";}
		
		IPAddress *getRemoteAddr(int &ret_port);
		std::string getKeyMgmt();
		void addHeader(MRef<SdpHeader*> h);
		virtual std::string getString();
                virtual std::string getContentType(){return "application/sdp";}
	

		std::vector<MRef<SdpHeader*> > getHeaders();
		int32_t getCodecMatch(SdpPacket &pack);
//		int32_t getCodecMatch(vector<CODECInterface *> codecs);
		int32_t getFirstMediaFormat();
		bool mediaFormatAvailable(int32_t f);
		
		/**
 		 * sets an attribute in the Session Level Part (before the first 'm'
 		 * tag) in the SDP packet. For example, a=p2tGroupIdentity:1234
 		 * @param type   the attribute type
 		 * @param value  the value of the attribute
 		 * @author Florian Maurer, florian.maurer@floHweb.ch
 		 */
		void setSessionLevelAttribute(std::string type, std::string value);
		
		/**
		 * search in the Session Level Part for an attribute and
		 * returns the value.
		 * a=type:value
		 * @param type the attribute type
		 * @return the value of the attribute or an empty string
		 *         if the attribute was not found.
		 */
		std::string getSessionLevelAttribute(std::string type);

		
	private:
		std::vector<MRef<SdpHeader*> > headers;
};


#endif