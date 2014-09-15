//

#include "rpcserver.h"
#include "util.h"
#include "wallet.h"

#include <stdint.h>
#include <string.h>
#include <map>

#include <fstream>
#include <vector>
#include <string>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>

#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"

#include "leveldb/db.h"
#include "leveldb/write_batch.h"

#include <openssl/sha.h>

using namespace std;
using namespace boost;
using namespace json_spirit;

#include "mastercore.h"

using namespace mastercore;

#include "mastercore_tx.h"

// display the tally map & the offer/accept list(s)
Value mscrpc(const Array& params, bool fHelp)
{
int extra = 0;
int extra2 = 0, extra3 = 0;

    if (fHelp || params.size() > 3)
        throw runtime_error(
            "mscrpc\n"
            "\nReturns the number of blocks in the longest block chain.\n"
            "\nResult:\n"
            "n    (numeric) The current block count\n"
            "\nExamples:\n"
            + HelpExampleCli("mscrpc", "")
            + HelpExampleRpc("mscrpc", "")
        );

  if (0 < params.size()) extra = atoi(params[0].get_str());
  if (1 < params.size()) extra2 = atoi(params[1].get_str());
  if (2 < params.size()) extra3 = atoi(params[2].get_str());

  printf("%s(extra=%d,extra2=%d,extra3=%d)\n", __FUNCTION__, extra, extra2, extra3);

  bool bDivisible = isPropertyDivisible(extra2);

  // various extra tests
  switch (extra)
  {
    case 0: // the old output
    {
    uint64_t total = 0;

        // display all balances
        for(map<string, CMPTally>::iterator my_it = mp_tally_map.begin(); my_it != mp_tally_map.end(); ++my_it)
        {
          // my_it->first = key
          // my_it->second = value

          printf("%34s => ", (my_it->first).c_str());
          total += (my_it->second).print(extra2, bDivisible);
        }

        printf("total for property %d  = %X is %s\n", extra2, extra2, FormatDivisibleMP(total).c_str());
      }
      break;

    case 1:
      // display the whole CMPTxList (leveldb)
      p_txlistdb->printAll();
      p_txlistdb->printStats();
      break;

    case 2:
        // display smart properties
        _my_sps->printAll();
      break;

    case 3:
        unsigned int id;
        // for each address display all currencies it holds
        for(map<string, CMPTally>::iterator my_it = mp_tally_map.begin(); my_it != mp_tally_map.end(); ++my_it)
        {
          // my_it->first = key
          // my_it->second = value

          printf("%34s => ", (my_it->first).c_str());
          (my_it->second).print(extra2);

          (my_it->second).init();
          while (0 != (id = (my_it->second).next()))
          {
            printf("Id: %u=0x%X ", id, id);
          }
          printf("\n");
        }
      break;

    case 4:
      for(CrowdMap::const_iterator it = my_crowds.begin(); it != my_crowds.end(); ++it)
      {
        (it->second).print(it->first);
      }
      break;

    case 5:
      printf("isMPinBlockRange(%d,%d)=%s\n", extra2, extra3, isMPinBlockRange(extra2, extra3, false) ? "YES":"NO");
      break;

    case 6:
      for(MetaDExMap::iterator it = metadex.begin(); it != metadex.end(); ++it)
      {
        // it->first = key
        // it->second = value
        printf("%s = %s\n", (it->first).c_str(), (it->second).ToString().c_str());
      }
      break;
  }

  return GetHeight();
}

// display an MP balance via RPC
Value getbalance_MP(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "getbalance_MP\n"
            "\nReturns the Master Protocol balance for a given address and currency/property.\n"
            "\nResult:\n"
            "n    (numeric) The applicable balance for address:currency/propertyID pair\n"
            "\nExamples:\n"
            ">mastercored getbalance_MP 1EXoDusjGwvnjZUyKkxZ4UHEf77z6A5S4P 1\n"
        );
    std::string address = params[0].get_str();
    int64_t tmpPropertyId = params[1].get_int64();
    if ((1 > tmpPropertyId) || (4294967295 < tmpPropertyId)) // not safe to do conversion
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property ID");

    unsigned int propertyId = int(tmpPropertyId);
    CMPSPInfo::Entry sp;
    if (false == _my_sps->getSP(propertyId, sp)) {
      throw JSONRPCError(RPC_INVALID_PARAMETER, "Property ID does not exist");
    }

    bool divisible = false;
    divisible=sp.isDivisible();

    Object balObj;

    int64_t tmpBalAvailable = getMPbalance(address, propertyId, MONEY);
    int64_t tmpBalReservedSell = getMPbalance(address, propertyId, SELLOFFER_RESERVE);
    int64_t tmpBalReservedAccept = 0;
    if (propertyId<3) tmpBalReservedAccept = getMPbalance(address, propertyId, ACCEPT_RESERVE);

    if (divisible)
    {
        balObj.push_back(Pair("balance", FormatDivisibleMP(tmpBalAvailable)));
        balObj.push_back(Pair("reserved", FormatDivisibleMP(tmpBalReservedSell+tmpBalReservedAccept)));
    }
    else
    {
        balObj.push_back(Pair("balance", FormatIndivisibleMP(tmpBalAvailable)));
        balObj.push_back(Pair("reserved", FormatIndivisibleMP(tmpBalReservedSell+tmpBalReservedAccept)));
    }

    return balObj;
}

// send a MP transaction via RPC - simple send
Value send_MP(const Array& params, bool fHelp)
{
if (fHelp || params.size() < 4 || params.size() > 5)
        throw runtime_error(
            "send_MP\n"
            "\nCreates and broadcasts a simple send for a given amount and currency/property ID.\n"
            "\nParameters:\n"
            "FromAddress   : the address to send from\n"
            "ToAddress     : the address to send to\n"
            "PropertyID    : the id of the smart property to send\n"
            "Amount        : the amount to send\n"
            "RedeemAddress : (optional) the address that can redeem the bitcoin outputs. Defaults to FromAddress\n"
            "Result:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">mastercored send_MP 1FromAddress 1ToAddress PropertyID Amount 1RedeemAddress\n"
        );

  std::string FromAddress = (params[0].get_str());
  std::string ToAddress = (params[1].get_str());
  std::string RedeemAddress = (params.size() > 4) ? (params[4].get_str()): "";

  int64_t tmpPropertyId = params[2].get_int64();
  if ((1 > tmpPropertyId) || (4294967295 < tmpPropertyId)) // not safe to do conversion
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property ID");
  unsigned int propertyId = int(tmpPropertyId);

  CMPSPInfo::Entry sp;
  if (false == _my_sps->getSP(propertyId, sp)) {
    throw JSONRPCError(RPC_INVALID_PARAMETER, "Property ID does not exist");
  }

  bool divisible = false;
  divisible=sp.isDivisible();

//  printf("%s(), params3='%s' line %d, file: %s\n", __FUNCTION__, params[3].get_str().c_str(), __LINE__, __FILE__);

  string strAmount = params[3].get_str();
  int64_t Amount = 0;
  Amount = strToInt64(strAmount, divisible);

  if ((Amount > 9223372036854775807) || (0 >= Amount))
           throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");

  //some sanity checking of the data supplied?
  int code = 0;
  uint256 newTX = send_INTERNAL_1packet(FromAddress, ToAddress, RedeemAddress, propertyId, Amount, MSC_TYPE_SIMPLE_SEND, &code);

  if (0 != code) throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("error code= %i", code));

  //we need to do better than just returning a string of 0000000 here if we can't send the TX
  return newTX.GetHex();
}

// send a MP transaction via RPC - simple send
Value sendtoowners_MP(const Array& params, bool fHelp)
{
if (fHelp || params.size() < 3 || params.size() > 4)
        throw runtime_error(
            "sendtoowners_MP\n"
            "\nCreates and broadcasts a send-to-owners transaction for a given amount and currency/property ID.\n"
            "\nParameters:\n"
            "FromAddress   : the address to send from\n"
            "PropertyID    : the id of the smart property to send\n"
            "Amount (string): the amount to send\n"
            "RedeemAddress : (optional) the address that can redeem the bitcoin outputs. Defaults to FromAddress\n"
            "\nResult:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">mastercored send_MP 1FromAddress PropertyID Amount 1RedeemAddress\n"
        );

  std::string FromAddress = (params[0].get_str());
  std::string RedeemAddress = (params.size() > 3) ? (params[3].get_str()): "";

  int64_t tmpPropertyId = params[1].get_int64();
  if ((1 > tmpPropertyId) || (4294967295 < tmpPropertyId)) // not safe to do conversion
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property ID");

  unsigned int propertyId = int(tmpPropertyId);

  if (!isTestEcosystemProperty(propertyId)) // restrict usage to test eco only
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Send to owners restricted to test properties only in this build"); 

  CMPSPInfo::Entry sp;
  if (false == _my_sps->getSP(propertyId, sp)) {
    throw JSONRPCError(RPC_INVALID_PARAMETER, "Property ID does not exist");
  }

  bool divisible = false;
  divisible=sp.isDivisible();

//  printf("%s(), params3='%s' line %d, file: %s\n", __FUNCTION__, params[3].get_str().c_str(), __LINE__, __FILE__);

  string strAmount = params[2].get_str();
  int64_t Amount = 0;
  Amount = strToInt64(strAmount, divisible);

  if ((Amount > 9223372036854775807) || (0 >= Amount))
           throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount");

//  printf("%s() %40.25lf, %lu, line %d, file: %s\n", __FUNCTION__, tmpAmount, Amount, __LINE__, __FILE__);

  //some sanity checking of the data supplied?
  int code = 0;
  uint256 newTX = send_INTERNAL_1packet(FromAddress, "", RedeemAddress, propertyId, Amount, MSC_TYPE_SEND_TO_OWNERS, &code);

  if (0 != code) throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("error code= %i", code));

  //we need to do better than just returning a string of 0000000 here if we can't send the TX
  return newTX.GetHex();
}

Value sendrawtx_MP(const Array& params, bool fHelp)
{
if (fHelp || params.size() < 3 || params.size() > 4)
        throw runtime_error(
            "sendrawtx_MP\n"
            "\nCreates and broadcasts a simple send for a given amount and currency/property ID.\n"
            "\nParameters:\n"
            "FromAddress   : the address to send from\n"
            "RawTX         : the hex-encoded raw transaction\n"
            "ToAddress     : the address to send to.  This should be empty: (\"\") for transaction\n"
            "                types that do not use a reference/to address\n"
            "RedeemAddress : (optional) the address that can redeem the bitcoin outputs. Defaults to FromAddress\n"
            "\nResult:\n"
            "txid    (string) The transaction ID of the sent transaction\n"
            "\nExamples:\n"
            ">mastercored sendrawtx_MP 1FromAddress <tx bytes hex> 1ToAddress 1RedeemAddress\n"
        );

  std::string FromAddress = (params[0].get_str());
  std::string hexTransaction = (params[1].get_str());
  std::string ToAddress = (params.size() > 2) ? (params[2].get_str()): "";
  std::string RedeemAddress = (params.size() > 3) ? (params[3].get_str()): "";

  //some sanity checking of the data supplied?
  uint256 newTX;
  vector<unsigned char> data = ParseHex(hexTransaction);
  int rc = ClassB_send(FromAddress, ToAddress, RedeemAddress, data, newTX);

  if (0 != rc) throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("error code= %i", rc));

  //we need to do better than just returning a string of 0000000 here if we can't send the TX
  return newTX.GetHex();
}

Value getallbalancesforid_MP(const Array& params, bool fHelp)
{
   if (fHelp || params.size() != 1)
        throw runtime_error(
            "getallbalancesforid_MP currencyID\n"
            "\nGet a list of address balances for a given currency/property ID\n"
            "\nArguments:\n"
            "1. currencyID    (int, required) The currency/property ID\n"
            "\nResult:\n"
            "{\n"
            "  \"address\" : 1Address,        (string) The address\n"
            "  \"balance\" : x.xxx,     (string) The available balance of the address\n"
            "  \"reserved\" : x.xxx,   (string) The amount reserved by sell offers and accepts\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("getallbalancesforid_MP", "1")
            + HelpExampleRpc("getallbalancesforid_MP", "1")
        );

    int64_t tmpPropertyId = params[0].get_int64();
    if ((1 > tmpPropertyId) || (4294967295 < tmpPropertyId)) // not safe to do conversion
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property ID");

    unsigned int propertyId = int(tmpPropertyId);
    CMPSPInfo::Entry sp;
    if (false == _my_sps->getSP(propertyId, sp)) {
      throw JSONRPCError(RPC_INVALID_PARAMETER, "Property ID does not exist");
    }

    bool divisible=false;
    divisible=sp.isDivisible();

    Array response;

    for(map<string, CMPTally>::iterator my_it = mp_tally_map.begin(); my_it != mp_tally_map.end(); ++my_it)
    {
        unsigned int id;
        bool includeAddress=false;
        string address = (my_it->first).c_str();
        (my_it->second).init();
        while (0 != (id = (my_it->second).next()))
        {
           if(id==propertyId) { includeAddress=true; break; }
        }

        if (!includeAddress) continue; //ignore this address, has never transacted in this propertyId

        Object addressbal;

        int64_t tmpBalAvailable = getMPbalance(address, propertyId, MONEY);
        int64_t tmpBalReservedSell = getMPbalance(address, propertyId, SELLOFFER_RESERVE);
        int64_t tmpBalReservedAccept = 0;
        if (propertyId<3) tmpBalReservedAccept = getMPbalance(address, propertyId, ACCEPT_RESERVE);

        addressbal.push_back(Pair("address", address));
        if(divisible)
        {
        addressbal.push_back(Pair("balance", FormatDivisibleMP(tmpBalAvailable)));
        addressbal.push_back(Pair("reserved", FormatDivisibleMP(tmpBalReservedSell+tmpBalReservedAccept)));
        }
        else
        {
        addressbal.push_back(Pair("balance", FormatIndivisibleMP(tmpBalAvailable)));
        addressbal.push_back(Pair("reserved", FormatIndivisibleMP(tmpBalReservedSell+tmpBalReservedAccept)));
        }
        response.push_back(addressbal);
    }
return response;
}

Value getallbalancesforaddress_MP(const Array& params, bool fHelp)
{
   string address;

   if (fHelp || params.size() != 1)
        throw runtime_error(
            "getallbalancesforaddress_MP address\n"
            "\nGet a list of all balances for a given address\n"
            "\nArguments:\n"
            "1. currencyID    (int, required) The currency/property ID\n"
            "\nResult:\n"
            "{\n"
            "  \"propertyid\" : x,        (numeric) the property id\n"
            "  \"balance\" : x.xxx,     (string) The available balance of the address\n"
            "  \"reserved\" : x.xxx,   (string) The amount reserved by sell offers and accepts\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("getallbalancesforaddress_MP", "address")
            + HelpExampleRpc("getallbalancesforaddress_MP", "address")
        );

    address = params[0].get_str();
    if (address.empty())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address");

    Array response;

    CMPTally *addressTally=getTally(address);

    if (NULL == addressTally) // addressTally object does not exist
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Address not found");

    addressTally->init();

    uint64_t propertyId; // avoid issues with json spirit at uint32
    while (0 != (propertyId = addressTally->next()))
    {
            bool divisible=false;
            CMPSPInfo::Entry sp;
            if (_my_sps->getSP(propertyId, sp)) {
              divisible = sp.isDivisible();
            }

            Object propertyBal;

            propertyBal.push_back(Pair("propertyid", propertyId));

            int64_t tmpBalAvailable = getMPbalance(address, propertyId, MONEY);
            int64_t tmpBalReservedSell = getMPbalance(address, propertyId, SELLOFFER_RESERVE);
            int64_t tmpBalReservedAccept = 0;
            if (propertyId<3) tmpBalReservedAccept = getMPbalance(address, propertyId, ACCEPT_RESERVE);

            if (divisible)
            {
                    propertyBal.push_back(Pair("balance", FormatDivisibleMP(tmpBalAvailable)));
                    propertyBal.push_back(Pair("reserved", FormatDivisibleMP(tmpBalReservedSell+tmpBalReservedAccept)));
            }
            else
            {
                    propertyBal.push_back(Pair("balance", FormatIndivisibleMP(tmpBalAvailable)));
                    propertyBal.push_back(Pair("reserved", FormatIndivisibleMP(tmpBalReservedSell+tmpBalReservedAccept)));
            }

            response.push_back(propertyBal);
    }

    return response;
}

Value getproperty_MP(const Array& params, bool fHelp)
{
   if (fHelp || params.size() != 1)
        throw runtime_error(
            "getproperty_MP propertyID\n"
            "\nGet details for a property ID\n"
            "\nArguments:\n"
            "1. propertyID    (int, required) The property ID\n"
            "\nResult:\n"
            "{\n"
            "  \"name\" : \"PropertyName\",     (string) the property name\n"
            "  \"category\" : \"PropertyCategory\",     (string) the property category\n"
            "  \"subcategory\" : \"PropertySubCategory\",     (string) the property subcategory\n"
            "  \"data\" : \"PropertyData\",     (string) the property data\n"
            "  \"url\" : \"PropertyURL\",     (string) the property URL\n"
            "  \"divisible\" : false,     (boolean) whether the property is divisible\n"
            "  \"issuer\" : \"1Address\",     (string) the property issuer address\n"
            "  \"issueancetype\" : \"Fixed\",     (string) the property method of issuance\n"
            "  \"totaltokens\" : x     (string) the total number of tokens in existence\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("getproperty_MP", "3")
            + HelpExampleRpc("getproperty_MP", "3")
        );

    int64_t tmpPropertyId = params[0].get_int64();
    if ((1 > tmpPropertyId) || (4294967295 < tmpPropertyId)) // not safe to do conversion
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property ID");

    unsigned int propertyId = int(tmpPropertyId);
    CMPSPInfo::Entry sp;
    if (false == _my_sps->getSP(propertyId, sp)) {
      throw JSONRPCError(RPC_INVALID_PARAMETER, "Property ID does not exist");
    }

    Object response;
        bool divisible = false;
        divisible=sp.isDivisible();
        string propertyName = sp.name;
        string propertyCategory = sp.category;
        string propertySubCategory = sp.subcategory;
        string propertyData = sp.data;
        string propertyURL = sp.url;
        uint256 creationTXID = sp.txid;
        int64_t totalTokens = getTotalTokens(propertyId);
        string issuer = sp.issuer;
        bool fixedIssuance = sp.fixed;

        uint64_t dispPropertyId = propertyId; //json spirit needs a uint64 as noted elsewhere
        response.push_back(Pair("propertyid", dispPropertyId)); //req by DexX to include propId in this output, no harm :)
        response.push_back(Pair("name", propertyName));
        response.push_back(Pair("category", propertyCategory));
        response.push_back(Pair("subcategory", propertySubCategory));
        response.push_back(Pair("data", propertyData));
        response.push_back(Pair("url", propertyURL));
        response.push_back(Pair("divisible", divisible));
        response.push_back(Pair("issuer", issuer));
        response.push_back(Pair("creationtxid", creationTXID.GetHex()));
        response.push_back(Pair("fixedissuance", fixedIssuance));
        if (divisible)
        {
            response.push_back(Pair("totaltokens", FormatDivisibleMP(totalTokens)));
        }
        else
        {
            response.push_back(Pair("totaltokens", FormatIndivisibleMP(totalTokens)));
        }

return response;
}

Value listproperties_MP(const Array& params, bool fHelp)
{
   if (fHelp)
        throw runtime_error(
            "listproperties_MP\n"
            "\nList smart properties\n"
            "\nResult:\n"
            "{\n"
            "  \"name\" : \"PropertyName\",     (string) the property name\n"
            "  \"category\" : \"PropertyCategory\",     (string) the property category\n"
            "  \"subcategory\" : \"PropertySubCategory\",     (string) the property subcategory\n"
            "  \"data\" : \"PropertyData\",     (string) the property data\n"
            "  \"url\" : \"PropertyURL\",     (string) the property URL\n"
            "  \"divisible\" : false,     (boolean) whether the property is divisible\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("listproperties_MP", "")
            + HelpExampleRpc("listproperties_MP", "")
        );

    Array response;

    int64_t propertyId;
    unsigned int nextSPID = _my_sps->peekNextSPID(1);
    for (propertyId = 1; propertyId<nextSPID; propertyId++)
    {
        CMPSPInfo::Entry sp;
        if (false != _my_sps->getSP(propertyId, sp))
        {
            Object responseItem;

            bool divisible=sp.isDivisible();
            string propertyName = sp.name;
            string propertyCategory = sp.category;
            string propertySubCategory = sp.subcategory;
            string propertyData = sp.data;
            string propertyURL = sp.url;

            responseItem.push_back(Pair("propertyid", propertyId));
            responseItem.push_back(Pair("name", propertyName));
            responseItem.push_back(Pair("category", propertyCategory));
            responseItem.push_back(Pair("subcategory", propertySubCategory));
            responseItem.push_back(Pair("data", propertyData));
            responseItem.push_back(Pair("url", propertyURL));
            responseItem.push_back(Pair("divisible", divisible));

            response.push_back(responseItem);
        }
    }

    unsigned int nextTestSPID = _my_sps->peekNextSPID(2);
    for (propertyId = TEST_ECO_PROPERTY_1; propertyId<nextTestSPID; propertyId++)
    {
        CMPSPInfo::Entry sp;
        if (false != _my_sps->getSP(propertyId, sp))
        {
            Object responseItem;

            bool divisible=sp.isDivisible();
            string propertyName = sp.name;
            string propertyCategory = sp.category;
            string propertySubCategory = sp.subcategory;
            string propertyData = sp.data;
            string propertyURL = sp.url;

            responseItem.push_back(Pair("propertyid", propertyId));
            responseItem.push_back(Pair("name", propertyName));
            responseItem.push_back(Pair("category", propertyCategory));
            responseItem.push_back(Pair("subcategory", propertySubCategory));
            responseItem.push_back(Pair("data", propertyData));
            responseItem.push_back(Pair("url", propertyURL));
            responseItem.push_back(Pair("divisible", divisible));

            response.push_back(responseItem);
        }
    }
return response;
}

Value getcrowdsale_MP(const Array& params, bool fHelp)
{
   if (fHelp || params.size() < 1 )
        throw runtime_error(
            "getcrowdsale_MP propertyID\n"
            "\nGet crowdsale info for a property ID\n"
            "\nArguments:\n"
            "1. propertyID    (int, required) The property ID\n"
            "\nResult:\n"
            "{\n"
            "  \"name\" : \"PropertyName\",     (string) the property name\n"
            "  \"active\" : false,     (boolean) whether the crowdsale is active\n"
            "  \"issuer\" : \"1Address\",     (string) the issuer address\n"
            "  \"creationtxid\" : \"txid\",     (string) the transaction that created the crowdsale\n"
            "  \"propertyiddesired\" : x,     (numeric) the property ID desired\n"
            "  \"tokensperunit\" : x,     (numeric) the number of tokens awarded per unit\n"
            "  \"earlybonus\" : x,     (numeric) the percentage per week early bonus applied\n"
            "  \"percenttoissuer\" : x,     (numeric) the percentage awarded to the issuer\n"
            "  \"starttime\" : xxx,     (numeric) the start time of the crowdsale\n"
            "  \"deadline\" : xxx,     (numeric) the time the crowdsale will automatically end\n"
            "  \"closedearly\" : false,     (boolean) whether the crowdsale was ended early\n"
            "  \"maxtokens\" : false,     (boolean) whether the crowdsale was ended early due to maxing the token count\n"
            "  \"endedtime\" : xxx,     (numeric) the time the crowdsale ended\n"
            "  \"closetx\" : \"txid\",     (string) the transaction that closed the crowdsale\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("getcrowdsale_MP", "3")
            + HelpExampleRpc("getcrowdsale_MP", "3")
        );

    int64_t tmpPropertyId = params[0].get_int64();
    if ((1 > tmpPropertyId) || (4294967295 < tmpPropertyId)) // not safe to do conversion
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property ID");

    unsigned int propertyId = int(tmpPropertyId);
    CMPSPInfo::Entry sp;
    if (false == _my_sps->getSP(propertyId, sp)) {
      throw JSONRPCError(RPC_INVALID_PARAMETER, "Property ID does not exist");
    }

    bool showVerbose = false;
    if (params.size() > 1) showVerbose = params[1].get_bool();

    bool fixedIssuance = sp.fixed;
    bool manualIssuance = sp.manual;
    if (fixedIssuance || manualIssuance) // property was not a variable issuance
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Property was not created with a crowdsale");

    uint256 creationHash = sp.txid;

    CTransaction tx;
    uint256 hashBlock = 0;
    if (!GetTransaction(creationHash, tx, hashBlock, true))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

    if ((0 == hashBlock) || (NULL == mapBlockIndex[hashBlock]))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Exception: blockHash is 0");

    Object response;

    bool active = false;
    active = isCrowdsaleActive(propertyId);
    bool divisible = false;
    divisible=sp.isDivisible();
    string propertyName = sp.name;
    int64_t startTime = mapBlockIndex[hashBlock]->nTime;
    int64_t deadline = sp.deadline;
    int8_t earlyBonus = sp.early_bird;
    int8_t percentToIssuer = sp.percentage;
    bool closeEarly = sp.close_early;
    bool maxTokens = sp.max_tokens;
    int64_t timeClosed = sp.timeclosed;
    string txidClosed = sp.txid_close.GetHex();
    string issuer = sp.issuer;
    int64_t amountRaised = 0;
    int64_t tokensIssued = getTotalTokens(propertyId);
    int64_t tokensPerUnit = sp.num_tokens;
    int64_t propertyIdDesired = sp.currency_desired;
    std::map<std::string, std::vector<uint64_t> > database;

    if (active)
    {
          bool crowdFound = false;
          for(CrowdMap::const_iterator it = my_crowds.begin(); it != my_crowds.end(); ++it)
          {
              CMPCrowd crowd = it->second;
              int64_t tmpPropertyId = crowd.getPropertyId();
              if (tmpPropertyId == propertyId)
              {
                  crowdFound = true;
                  database = crowd.getDatabase();
              }
          }
          if (!crowdFound)
                  throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Exception: crowdsale is flagged active but cannot be found in CrowdMap");
    }
    else
    {
        database = sp.historicalData;
    }

    fprintf(mp_fp,"\nSIZE OF DB %lu\n", sp.historicalData.size() );
    //bool closedEarly = false; //this needs to wait for dead crowdsale persistence
    //int64_t endedTime = 0; //this needs to wait for dead crowdsale persistence

    bool divisibleDesired = false;
    CMPSPInfo::Entry spDesired;
    if (false == _my_sps->getSP(propertyId, spDesired)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Desired property ID does not exist");
    }
    divisibleDesired = spDesired.isDivisible();
    divisibleDesired = isPropertyDivisible(propertyIdDesired);

    Array participanttxs;
    std::map<std::string, std::vector<uint64_t> >::const_iterator it;
    for(it = database.begin(); it != database.end(); it++)
    {
        Object participanttx;

        string txid = it->first; //uint256 txid = it->first;
        int64_t userTokens = it->second.at(2);
        int64_t issuerTokens = it->second.at(3);
        int64_t amountSent = it->second.at(0);

        amountRaised += amountSent;
        participanttx.push_back(Pair("txid", txid)); //.GetHex()).c_str();
        if (divisibleDesired)
        {
             participanttx.push_back(Pair("amountsent", FormatDivisibleMP(amountSent)));
        }
        else
        {
             participanttx.push_back(Pair("amountsent", FormatIndivisibleMP(amountSent)));
        }
        if (divisible)
        {
             participanttx.push_back(Pair("participanttokens", FormatDivisibleMP(userTokens)));
        }
        else
        {
             participanttx.push_back(Pair("participanttokens", FormatIndivisibleMP(userTokens)));
        }
        if (divisible)
        {
             participanttx.push_back(Pair("issuertokens", FormatDivisibleMP(issuerTokens)));
        }
        else
        {
             participanttx.push_back(Pair("issuertokens", FormatIndivisibleMP(issuerTokens)));
        }
        participanttxs.push_back(participanttx);
    }

    response.push_back(Pair("name", propertyName));
    response.push_back(Pair("active", active));
    response.push_back(Pair("issuer", issuer));
    response.push_back(Pair("propertyiddesired", propertyIdDesired));
    if (divisible)
    {
        response.push_back(Pair("tokensperunit", FormatDivisibleMP(tokensPerUnit)));
    }
    else
    {
        response.push_back(Pair("tokensperunit", FormatIndivisibleMP(tokensPerUnit)));
    }
    response.push_back(Pair("earlybonus", earlyBonus));
    response.push_back(Pair("percenttoissuer", percentToIssuer));
    response.push_back(Pair("starttime", startTime));
    response.push_back(Pair("deadline", deadline));

    if (divisibleDesired)
    {
        response.push_back(Pair("amountraised", FormatDivisibleMP(amountRaised)));
    }
    else
    {
        response.push_back(Pair("amountraised", FormatIndivisibleMP(amountRaised)));
    }
    if (divisible)
    {
        response.push_back(Pair("tokensissued", FormatDivisibleMP(tokensIssued)));
    }
    else
    {
        response.push_back(Pair("tokensissued", FormatIndivisibleMP(tokensIssued)));
    }
    if (!active) response.push_back(Pair("closedearly", closeEarly));
    if (!active) response.push_back(Pair("maxtokens", maxTokens));
    if (closeEarly) response.push_back(Pair("endedtime", timeClosed));
    if (closeEarly && !maxTokens) response.push_back(Pair("closetx", txidClosed));
    
    // array of txids contributing to crowdsale here if needed
    if (showVerbose)
    {
        response.push_back(Pair("participanttransactions", participanttxs));
    }
return response;
}

Value getactivecrowdsales_MP(const Array& params, bool fHelp)
{
   if (fHelp)
        throw runtime_error(
            "getactivecrowdsales_MP\n"
            "\nGet active crowdsales\n"
            "\nResult:\n"
            "{\n"
            "  \"name\" : \"PropertyName\",     (string) the property name\n"
            "  \"issuer\" : \"1Address\",     (string) the issuer address\n"
            "  \"creationtxid\" : \"txid\",     (string) the transaction that created the crowdsale\n"
            "  \"propertyiddesired\" : x,     (numeric) the property ID desired\n"
            "  \"tokensperunit\" : x,     (numeric) the number of tokens awarded per unit\n"
            "  \"earlybonus\" : x,     (numeric) the percentage per week early bonus applied\n"
            "  \"percenttoissuer\" : x,     (numeric) the percentage awarded to the issuer\n"
            "  \"starttime\" : xxx,     (numeric) the start time of the crowdsale\n"
            "  \"deadline\" : xxx,     (numeric) the time the crowdsale will automatically end\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("getactivecrowdsales_MP", "")
            + HelpExampleRpc("getactivecrowdsales_MP", "")
        );

      Array response;

      for(CrowdMap::const_iterator it = my_crowds.begin(); it != my_crowds.end(); ++it)
      {
          CMPCrowd crowd = it->second;
          CMPSPInfo::Entry sp;
          bool spFound = _my_sps->getSP(crowd.getPropertyId(), sp);
          int64_t propertyId = crowd.getPropertyId();
          if (spFound)
          {
              Object responseObj;

              uint256 creationHash = sp.txid;

              CTransaction tx;
              uint256 hashBlock = 0;
              if (!GetTransaction(creationHash, tx, hashBlock, true))
                  throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

              if ((0 == hashBlock) || (NULL == mapBlockIndex[hashBlock]))
                  throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Exception: blockHash is 0");

              bool divisible = false;
              divisible=sp.isDivisible();
              string propertyName = sp.name;
              int64_t startTime = mapBlockIndex[hashBlock]->nTime;
              int64_t deadline = sp.deadline;
              int8_t earlyBonus = sp.early_bird;
              int8_t percentToIssuer = sp.percentage;
              string issuer = sp.issuer;
              int64_t tokensPerUnit = sp.num_tokens;
              int64_t propertyIdDesired = sp.currency_desired;

              responseObj.push_back(Pair("propertyid", propertyId));
              responseObj.push_back(Pair("name", propertyName));
              responseObj.push_back(Pair("issuer", issuer));
              responseObj.push_back(Pair("propertyiddesired", propertyIdDesired));
              if (divisible)
              {
                  responseObj.push_back(Pair("tokensperunit", FormatDivisibleMP(tokensPerUnit)));
              }
              else
              {
                  responseObj.push_back(Pair("tokensperunit", FormatIndivisibleMP(tokensPerUnit)));
              }
              responseObj.push_back(Pair("earlybonus", earlyBonus));
              responseObj.push_back(Pair("percenttoissuer", percentToIssuer));
              responseObj.push_back(Pair("starttime", startTime));
              responseObj.push_back(Pair("deadline", deadline));

              response.push_back(responseObj);
          }
      }

return response;
}

Value getgrants_MP(const Array& params, bool fHelp)
{
   if (fHelp || params.size() != 1 )
        throw runtime_error(
            "getgrants_MP propertyID\n"
            "\nGet grant/revoke info for a property ID\n"
            "\nArguments:\n"
            "1. propertyID    (int, required) The property ID\n"
            "\nResult:\n"
            "{\n"
            "  \"name\" : \"PropertyName\",   (string) the property name\n"
            "  \"issuer\" : \"1Address\",     (string) the issuer address\n"
            "  \"creationtxid\" : \"txid\",   (string) the transaction that created the crowdsale\n"
            "  \"totaltokens\" : \"1234\",    (string) the number of tokens in circulation\n"
            "  \"issuances\" : [\n"
            "                    {\n"
            "                      \"txid\" : \"txid\",   (string) the transaction that granted/revoked tokens\n"
            "                      \"grant\" : \"1234\",  (string) the number of tokens granted\n"
            "                    },\n"
            "                    {\n"
            "                      \"txid\" : \"txid\",   (string) the transaction that granted/revoked tokens\n"
            "                      \"revoke\" : \"1234\", (string) the number of tokens revoked\n"
            "                    },\n"
            "                    ...\n"
            "                  ]\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("getgrants_MP", "3")
            + HelpExampleRpc("getgrants_MP", "3")
        );

    int64_t tmpPropertyId = params[0].get_int64();
    if ((1 > tmpPropertyId) || (4294967295 < tmpPropertyId)) // not safe to do conversion
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid property ID");

    unsigned int propertyId = int(tmpPropertyId);
    CMPSPInfo::Entry sp;
    if (false == _my_sps->getSP(propertyId, sp)) {
      throw JSONRPCError(RPC_INVALID_PARAMETER, "Property ID does not exist");
    }

    bool fixedIssuance = sp.fixed;
    bool manualIssuance = sp.manual;
    if (fixedIssuance || !manualIssuance) // property was not a manual issuance
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Property was not created with a manual issuance");

    uint256 creationHash = sp.txid;

    Object response;

//    bool active = isCrowdsaleActive(propertyId);  // UNUSED WARNING
//    bool divisible = sp.isDivisible(); // UNUSED WARNING
    string propertyName = sp.name;
    string issuer = sp.issuer;
    int64_t totalTokens = getTotalTokens(propertyId);

    Array issuancetxs;
    std::map<std::string, std::vector<uint64_t> >::const_iterator it;
    for(it = sp.historicalData.begin(); it != sp.historicalData.end(); it++)
    {

        string txid = it->first; //uint256 txid = it->first;
        int64_t grantedTokens = it->second.at(0);
        int64_t revokedTokens = it->second.at(1);
        if (grantedTokens > 0){
          Object granttx;
          granttx.push_back(Pair("txid", txid));
          if (sp.isDivisible()) {
            granttx.push_back(Pair("grant", FormatDivisibleMP(grantedTokens)));
          } else {
            granttx.push_back(Pair("grant", FormatIndivisibleMP(grantedTokens)));
          }
          issuancetxs.push_back(granttx);
        }

        if (revokedTokens > 0){
          Object revoketx;
          revoketx.push_back(Pair("txid", txid));
          if (sp.isDivisible()) {
            revoketx.push_back(Pair("revoke", FormatDivisibleMP(revokedTokens)));
          } else {
            revoketx.push_back(Pair("revoke", FormatIndivisibleMP(revokedTokens)));
          }
          issuancetxs.push_back(revoketx);
        }
    }

    response.push_back(Pair("name", propertyName));
    response.push_back(Pair("issuer", issuer));
    response.push_back(Pair("creationtxid", creationHash.GetHex()));
    if (sp.isDivisible()) {
      response.push_back(Pair("totaltokens", FormatDivisibleMP(totalTokens)));
    } else {
      response.push_back(Pair("totaltokens", FormatIndivisibleMP(totalTokens)));
    }
    response.push_back(Pair("issuances", issuancetxs));
    return response;
}

Value getactivedexsells_MP(const Array& params, bool fHelp)
{
   if (fHelp)
        throw runtime_error(
            "getactivedexsells_MP\n"
            "\nGet currently active distributed exchange sell offers\n"
            "\nResult:\n"
            "{\n"
            "}\n"

            "\nbExamples\n"
            + HelpExampleCli("getactivedexsells_MP", "")
            + HelpExampleRpc("getactivedexsells_MP", "")
        );

      //if 0 params list all sells, otherwise first param is filter address
      bool addressFilter = false;
      string addressParam;

      if (params.size() > 0)
      {
          addressParam = params[0].get_str();
          addressFilter = true;
      }

      Array response;

      for(OfferMap::iterator it = my_offers.begin(); it != my_offers.end(); ++it)
      {
          CMPOffer selloffer = it->second;
          string sellCombo = it->first;
          string seller = sellCombo.substr(0, sellCombo.size()-2);

          //filtering
          if ((addressFilter) && (seller != addressParam)) continue;

          uint256 sellHash = selloffer.getHash();
          string txid = sellHash.GetHex();
          uint64_t propertyId = selloffer.getCurrency();
          uint64_t minFee = selloffer.getMinFee();
          unsigned char timeLimit = selloffer.getBlockTimeLimit();
          uint64_t sellOfferAmount = selloffer.getOfferAmountOriginal(); //badly named - "Original" implies off the wire, but is amended amount
          uint64_t sellBitcoinDesired = selloffer.getBTCDesiredOriginal(); //badly named - "Original" implies off the wire, but is amended amount
          uint64_t amountAvailable = getMPbalance(seller, propertyId, SELLOFFER_RESERVE);
          uint64_t amountAccepted = getMPbalance(seller, propertyId, ACCEPT_RESERVE);

          //unit price & updated bitcoin desired calcs
          double unitPriceFloat = 0;
          if ((sellOfferAmount>0) && (sellBitcoinDesired > 0)) unitPriceFloat = (double)sellBitcoinDesired/(double)sellOfferAmount; //divide by zero protection
          uint64_t unitPrice = rounduint64(unitPriceFloat * COIN);
          uint64_t bitcoinDesired = rounduint64(amountAvailable*unitPriceFloat);

          Object responseObj;

          responseObj.push_back(Pair("txid", txid));
          responseObj.push_back(Pair("propertyid", propertyId));
          responseObj.push_back(Pair("seller", seller));
          responseObj.push_back(Pair("amountavailable", FormatDivisibleMP(amountAvailable)));
          responseObj.push_back(Pair("bitcoindesired", FormatDivisibleMP(bitcoinDesired)));
          responseObj.push_back(Pair("unitprice", FormatDivisibleMP(unitPrice)));
          responseObj.push_back(Pair("timelimit", timeLimit));
          responseObj.push_back(Pair("minimumfee", FormatDivisibleMP(minFee)));

          // display info about accepts related to sell
          responseObj.push_back(Pair("amountaccepted", FormatDivisibleMP(amountAccepted)));
          Array acceptsMatched;
          for(AcceptMap::iterator ait = my_accepts.begin(); ait != my_accepts.end(); ++ait)
          {
              Object matchedAccept;

              CMPAccept accept = ait->second;
              string acceptCombo = ait->first;
              uint256 matchedHash = accept.getHash();
              // does this accept match the sell?
              if (matchedHash == sellHash)
              {
                  //split acceptCombo out to get the buyer address
                  string buyer = acceptCombo.substr((acceptCombo.find("+")+1),(acceptCombo.size()-(acceptCombo.find("+")+1)));
                  uint64_t acceptBlock = accept.getAcceptBlock();
                  uint64_t acceptAmount = accept.getAcceptAmountRemaining();
                  matchedAccept.push_back(Pair("buyer", buyer));
                  matchedAccept.push_back(Pair("block", acceptBlock));
                  matchedAccept.push_back(Pair("amount", FormatDivisibleMP(acceptAmount)));
                  acceptsMatched.push_back(matchedAccept);
              }
          }
          responseObj.push_back(Pair("accepts", acceptsMatched));

          // add sell object into response array
          response.push_back(responseObj);
      }

return response;
}

Value listblocktransactions_MP(const Array& params, bool fHelp)
{
   if (fHelp || params.size() != 1)
        throw runtime_error(
            "listblocktransactions_MP index\n"
            "\nReturns all Master Protocol transactions in a block.\n"
            
            "\nArguments:\n"
            "1. index         (numeric, required) The block height or index\n"
            
            "\nResult:\n"
            "[                (array of string)\n"
            "  \"hash\"         (string) Transaction id\n"            
            "  ,...\n"
            "]\n"

            "\nExamples\n"
            + HelpExampleCli("listblocktransactions_MP", "279007")
            + HelpExampleRpc("listblocktransactions_MP", "279007")
        );

  // firstly let's get the block height given in the param
  int blockHeight = params[0].get_int();
  if (blockHeight < 0 || blockHeight > GetHeight())
        throw runtime_error("Cannot display MP transactions for a non-existent block.");

  // next let's obtain the block for this height
  CBlockIndex* mpBlockIndex = chainActive[blockHeight];
  CBlock mpBlock;

  // now let's read this block in from disk so we can loop its transactions
  if(!ReadBlockFromDisk(mpBlock, mpBlockIndex))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Internal Error: Failed to read block from disk");

  // create an array to hold our response
  Array response;

  // now we want to loop through each of the transactions in the block and run against CMPTxList::exists
  // those that return positive add to our response array
  BOOST_FOREACH(const CTransaction&tx, mpBlock.vtx)
  {
       bool mptx = p_txlistdb->exists(tx.GetHash());
       if (mptx)
       {
            // later we can add a verbose flag to decode here, but for now callers can send returned txids into gettransaction_MP
            // add the txid into the response as it's an MP transaction
            response.push_back(tx.GetHash().GetHex());
       }
  }
  return response;
}

