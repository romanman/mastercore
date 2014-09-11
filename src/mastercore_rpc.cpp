//

#include "rpcserver.h"
#include "util.h"
#include "wallet.h"
#include "walletdb.h"

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

