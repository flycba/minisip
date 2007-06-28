
#include <config.h>
#include <libmcrypto/SipSimSoft.h>
#include <libmcrypto/rand.h>

using namespace std;

SipSimSoft::SipSimSoft(MRef<CertificateChain*> chain, MRef<CertificateSet*> cas)
{
	certChain = chain;
	ca_set = cas;
}

bool SipSimSoft::getSignature(unsigned char * data,
		int dataLength,
		unsigned char * signaturePtr,
		int & signatureLength,
		bool doHash,
		int hash_alg)
{
	MRef<Certificate*> myCert = certChain->getFirst();
	assert(doHash /*we don't support not hashing in SipSimSoft yet...*/);
	myCert->signData(data, dataLength, signaturePtr, &signatureLength);
	return true;
}

bool SipSimSoft::getRandomValue(unsigned char * randomPtr, unsigned long randomLength)
{
	Rand::randomize(randomPtr, randomLength);
	return false;
}

