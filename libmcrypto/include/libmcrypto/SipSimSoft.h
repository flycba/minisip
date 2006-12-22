#ifndef _SIPSIMSOFT_H
#define _SIPSIMSOFT_H

#include<libmcrypto/SipSim.h>

/**
 *
 * "Soft" refers to that crypto-algorithms are
 * implemented in software (using OpenSSL or 
 * GnuTLS), but could also be read as "soft" security
 * compared to SmartCard hardware security devices.
 *
 */
class LIBMCRYPTO_API SipSimSoft : public SipSim{
	public:
		SipSimSoft(MRef<certificate_chain*> chain, MRef<ca_db*> cas);

		virtual bool getSignature(unsigned char * data,
				int dataLength,
				unsigned char * signaturePtr,
				int & signatureLength,
				bool doHash,
				int hash_alg=HASH_SHA1);


	private:

};




#endif