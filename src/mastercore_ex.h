#ifndef _MASTERCOIN_EX
#define _MASTERCOIN_EX 1

#include "mastercore.h"

//

// this is the internal format for the offer primary key (TODO: replace by a class method)
#define STR_SELLOFFER_ADDR_CURR_COMBO(x) ( x + "-" + strprintf("%d", curr))
#define STR_ACCEPT_ADDR_CURR_ADDR_COMBO( _seller , _buyer ) ( _seller + "-" + strprintf("%d", curr) + "+" + _buyer)
#define STR_PAYMENT_SUBKEY_TXID_PAYMENT_COMBO(txidStr) ( txidStr + "-" + strprintf("%d", paymentNumber))

unsigned int eraseExpiredAccepts(int blockNow);

namespace mastercore
{
bool DEx_offerExists(const string &seller_addr, unsigned int curr);
CMPOffer *DEx_getOffer(const string &seller_addr, unsigned int curr);
CMPAccept *DEx_getAccept(const string &seller_addr, unsigned int curr, const string &buyer_addr);
int DEx_offerCreate(string seller_addr, unsigned int curr, uint64_t nValue, int block, uint64_t amount_desired, uint64_t fee, unsigned char btl, const uint256 &txid, uint64_t *nAmended = NULL);
int DEx_offerDestroy(const string &seller_addr, unsigned int curr);
int DEx_offerUpdate(const string &seller_addr, unsigned int curr, uint64_t nValue, int block, uint64_t desired, uint64_t fee, unsigned char btl, const uint256 &txid, uint64_t *nAmended = NULL);
int DEx_acceptCreate(const string &buyer, const string &seller, int curr, uint64_t nValue, int block, uint64_t fee_paid, uint64_t *nAmended = NULL);
int DEx_acceptDestroy(const string &buyer, const string &seller, int curr, bool bForceErase = false);
int DEx_payment(uint256 txid, unsigned int vout, string seller, string buyer, uint64_t BTC_paid, int blockNow, uint64_t *nAmended = NULL);

CMPMetaDEx *getMetaDEx(const string &sender_addr, unsigned int curr);

int MetaDEx_Trade(const string &customer, unsigned int currency, unsigned int currency_desired, uint64_t amount_desired, uint64_t price_int, uint64_t price_frac);
int MetaDEx_Phase1(const string &addr, unsigned int property, bool bSell, const uint256 &txid, unsigned int idx);
int MetaDEx_Create(const string &sender_addr, unsigned int curr, uint64_t amount, int block, unsigned int currency_desired, uint64_t amount_desired, const uint256 &txid, unsigned int idx);
int MetaDEx_Destroy(const string &sender_addr, unsigned int curr);
int MetaDEx_Update(const string &sender_addr, unsigned int curr, uint64_t nValue, int block, unsigned int currency_desired, uint64_t amount_desired, const uint256 &txid, unsigned int idx);
}

#endif // #ifndef _MASTERCOIN_EX

