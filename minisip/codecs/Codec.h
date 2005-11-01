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

#ifndef CODECINTERFACE_H
#define CODECINTERFACE_H

#include<sys/types.h>

#include<string>

#include<libmutil/MemObject.h>

using namespace std;

class Codec;
class CodecState;

class Codec: public MObject{
	public:

		virtual MRef<CodecState *> newInstance()=0;
		
		virtual std::string getCodecName()=0;
		
		virtual std::string getCodecDescription()=0;
		
		virtual uint8_t getSdpMediaType()=0;

		virtual std::string getSdpMediaAttributes()=0;

		virtual std::string getMemObjectType(){return "Codec";}

};

class CodecState: public MObject{
	public:
		/**
		 * @returns Number of bytes in output buffer
		 */
		virtual uint32_t encode(void *in_buf, int32_t in_buf_size, void *out_buf)=0;

		/**
		 * 
		 * @returns Number of frames in output buffer
		 */
		virtual uint32_t decode(void *in_buf, int32_t in_buf_size, void *out_buf)=0;

		virtual std::string getMemObjectType(){return "CodecState";};
	
		uint8_t getSdpMediaType(){ return codec->getSdpMediaType(); };

		void setCodec( MRef<Codec *> c ){ codec = c; };
	
		MRef<Codec*> getCodec(){return codec;}
		
	private:
		MRef<Codec *> codec;
		
};


class AudioCodec : public Codec{
	public:
		/**
		 * @returns A CODEC state for the given payloadType
		 * (NULL if not handled)
		 */
		static MRef<CodecState *> createState( uint8_t payloadType );
		
                /**
		 * @returns A CODEC instance for the given description string
		 * (NULL if not handled)
		 */
		static MRef<AudioCodec *> create( const std::string& );


		
		/**
		 * size of the output of the codec in bytes.
		 * Returns -1 if output size may vary.
		 */
//		virtual int32_t getEncodedNrBytes()=0;//
		
		virtual int32_t getInputNrSamples()=0;
	
		/**
		 * @return Requested sampling freq for the CODEC
		 */
		virtual int32_t getSamplingFreq()=0;

		/**
		 * Time in milliseconds to put in each frame/packet
		 */
		virtual int32_t getSamplingSizeMs()=0;
		
		//virtual std::string getMemObjectType(){return "AudioCodec";}
		
};


#endif
