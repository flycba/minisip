/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* Copyright (C) 2004 
 *
 * Authors: Erik Eliasson <eliasson@it.kth.se>
 *          Johan Bilien <jobi@via.ecp.fr>
*/

#ifndef GROUPLIST_H
#define GROUPLIST_H

#include<config.h>
#include<vector>
#include"../codecs/Codec.h"
#include<libmutil/MemObject.h>
#include<libmnetutil/IPAddress.h>
#include"P2T.h"
#include"GroupListUserElement.h"

/**
 * the GroupList contains all information about a P2T Session
 * and their users.
 * <p><b>Group List Management</b><br>
 * If a user wants to participate in a P2T-Session he has to know which
 * other UAs are participating in order to set up the required SIP Sessions
 * resp. in order to start the required <CODE>SipDialogP2Tuser</CODE> dialogs.
 * <p>
 * <code>GroupList</code> is used to store the Group Member List and additional
 * for every participating user all relevant datas like the used codec, RTP/RTCP
 * ports, and SSRC are stored in <code>GroupListUserElement</code> objects.
 * <p>
 *
 * <b>Group Member List</b><br>
 * A typical example for a Group Member List that will be exchanged between different 
 * users encoded in XML: <a href=material/grouplist.xml>grouplist.xml</a>.
 * <p>
 * @see GroupListUserElement
 * @author Florian Maurer, florian.maurer@floHweb.ch
 */

class GroupList: public MObject{
	public:
		
		/**
		 * Constructor
		 */
		GroupList();
		
		/**
		 * Constructor.
		 * creates a GroupList from the xml code.
		 * @param xml the xml code
		 */
		 GroupList(string xml);
		
		/**
		 * Destructor
		 */
		virtual ~GroupList();

		/**
		 * set the GroupIdentity
		 * @param Gid Group Identity
		 */
		void setGroupIdentity(string Gid) {GroupIdentity=Gid;}

		/**
		 * returns the name of the GroupList.
		 * Used by the Memory Handling of Minisip.
		 * @return the string 'GroupList'
		 */
		virtual std::string getMemObjectType(){return "GroupList";}
		
		/**
		 *get the GroupIdentity
		 *@return a string with the GroupIdentity
		 */
		string getGroupIdentity(){return GroupIdentity;}
		
		/**
		 * set the Group Owner
		 * @param owner Group Owner
		 */
		void setGroupOwner(string owner) {GroupOwner=owner;}
		
		/**
		 * get the Group Owner
		 * @return a string with the Group Owner
		 */
		string getGroupOwner(){return GroupOwner;}
		
		/**
		 * set the Group Description
		 * @param description the description
		 */
		void setDescription(string Description) {this->Description=Description;}
		
		/**
		 * get the Group Descrition
		 * @return a string with the Group Owner
		 */
		string getDescription(){return Description;}
		
		/**
		 * set the SessionType.
		 * Look in <code>p2tDefinitions.h</code> for possible
		 * values
		 * @param type the Session Type
		 * @see p2tDefinitions
		 */
		void setSessionType(int type) {SessionType=type;}
		
		/**
		 * get the SessionType
		 * @return an integer with the Session Type
		 */
		int getSessionType(){return SessionType;}
		
		/**
		 * set the Membership. Open(0) or Restricted(1)
		 * @param Membership the value for the membership
		 */
		void setMembership(int Membership) {this->Membership=Membership;}
		
		/**
		 * get the Membership
		 * @return an integer with the value for the membership
		 */
		int getMembership(){return Membership;}
		
		/**
		 * set the maximum floor time. If no maximum floor time
		 * shall be specified set the value to 0.
		 * @param time the maximum floor time in s
		 */
		void setMaxFloorTime(int time) {MaxFloorTime=time;}
		
		/**
		 * get the maximum floor time. The value 0 means that no maximum
		 * floor time is specified.
		 * @return MaxFloorTime an integer with the maximun floor time in seconds.
		 */
		int getMaxFloorTime(){return MaxFloorTime;}
		
		/**
		 * set the maximum number of participants. If no maximum number
		 * shall be specified the value 0 can be used.
		 * @param max the max number of allowed participants
		 */
		void setMaxParticipants(int max) {MaxParticipants=max;}
		
		/**
		 * get the maximum number of participants. The value 0 means that
		 * no maximum number is specified.
		 * @return an integer with the maximum number of participants.
		 */
		int getMaxParticipants(){return MaxParticipants;}
		
		/**
		 * add a uri to the member list
		 * @param uri the uri of the user
		 */
		void addMember(string uri);
		
		/**
		 * can be used to check if a uri is in the memberlist
		 * and the user is allowed to participate in the
		 * P2T Session.
		 * @return true if uri is in memberlist
		 */
		bool isMember(string uri);
				
		/**
		 * can be used to check if a uri is in the participants
		 * list.
		 * @return true if uri is in memberlist
		 */
		bool isParticipant(string uri);
		
		/**
		 * can be used to check if a SSRC is in the participants
		 * list.
		 * @return true if ssrc is in memberlist
		 */
		bool isParticipant(int ssrc);
		
		
		/**
		 * return all members
		 * @return a vector containing strings with the uri
		 */
		vector<string> getAllMember(){return members;}
		
		
		
		/**
		 * add a user to the Group List
		 * @param uri       the uri of the user
		 * @param to_addr   the address of the user
		 * @param RTPport   the port for the RTP packets (media stream)
		 * @param RTCPport  the port for the RTCP packets (floor control)
		 * @param codec     the used codec
		 * @param priority  the priority of the user
		 * @param seqNo     the current sequence number the user is using
		 * @param status    the status of the user for the floor control. IDLE(0), GRANT(1, the user
		 *                  has granted the floor) or COLLISION(2, user 
		                    requested also the floor).
		 * @see p2tDefinitions
		 */
		void addUser(string uri, IPAddress* to_addr, int RTPport, int RTCPport, Codec *codec,
			int priority=0,
			int seqNo=0,
			int status=0);
			
		
		/**
		 * add a user to the Group List
		 * @param uri       the uri of the user
		 * @param priority  the priority of the user
		 * @param status    the status of the user for the floor control. IDLE(0), GRANT(1, the user
		 *                  has granted the floor) or COLLISION(2, user 
		 *                  requested also the floor).
		 * @see p2tDefinitions
		 */
		void addUser(string uri, int priority=0,int status=0);
		

		
		/**
		 * Get the information about a specific user. 
		 * @param uri the SIP URI of the user
		 * @return a <code>GroupListUserElement</code> object
		 * @see GroupListUserElement
		 */
		MRef<GroupListUserElement*> getUser(string uri);
		
		/**
		 * Get the information about a specific user.
		 * @param ssrc the SSRC of the user 
		 * @return a <code>GroupListUserElement</code> object
		 * @see GroupListUserElement
		 */
		MRef<GroupListUserElement*> getUser(int ssrc);

		/**
		 * Get the information about all users.
		 * Returns a vector containing <code>GroupListUserElement</code>s.
		 * @return vector with <code>GroupListUserElement</code> objects.
		 * @see GroupListUserElement
		 */
		vector<MRef<GroupListUserElement*> > getAllUser(){return users;}
				
		/**
		 * remove a user from the GroupList.
		 * @param uri the uri of the user that should be removed
		 */
		void removeUser(string uri);
		
		/**
		 * returns the GroupList in XML format
		 * @return the GroupList in XML format
		 */
		string print();
		
		string print_debug();
	

	private:
		
		///the identity or name of the group
		string GroupIdentity;
		
		///The owner who is responsible for the GroupList
		string GroupOwner;
		
		///The description of the P2T Session
		string Description;
		
		/**
		 * The type of P2T Session (Instant Personal Talk, 
		 * Ad-Hoc Instant Group, Instant Group or Chat Group
		 */			
		int SessionType;
		
		///Open / Restricted
		int Membership;
		
		///the maximum duration a user can take the floor
		int MaxFloorTime;
		
		///the limit of number of participants in a P2T Session
		int MaxParticipants;
		
		/**
		 * a vector containing <code>MRef<GroupListUserElement*></code> objects
		 * with SIP URIs and priorities of the participating members
		*/
		vector<MRef<GroupListUserElement*> > users;
		
		///a vector containing URIs of the allowed members
		vector<string> members;
		

};

#endif