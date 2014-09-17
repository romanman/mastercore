// Smart Properties

#include "base58.h"
#include "rpcserver.h"
#include "init.h"
#include "util.h"
#include "wallet.h"

#include <stdint.h>
#include <string.h>

#include <fstream>
#include <algorithm>

#include <vector>

#include <utility>
#include <string>

#include <boost/assign/list_of.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>

#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"

#include "leveldb/db.h"
#include "leveldb/write_batch.h"

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;
using namespace leveldb;

#include "mastercore.h"

using namespace mastercore;

#include "mastercore_sp.h"

unsigned int CMPSPInfo::updateSP(unsigned int propertyID, Entry const &info)
{
    // cannot update implied SP
    if (MASTERCOIN_CURRENCY_MSC == propertyID || MASTERCOIN_CURRENCY_TMSC == propertyID) {
      return propertyID;
    }

    std::string nextIdStr;
    unsigned int res = propertyID;

    Object spInfo = info.toJSON();

    // generate the SP id
    string spKey = (boost::format(FORMAT_BOOST_SPKEY) % propertyID).str();
    string spValue = write_string(Value(spInfo), false);
    string spPrevKey = (boost::format("blk-%s:sp-%d") % info.update_block.ToString() % propertyID).str();
    string spPrevValue;

    leveldb::WriteBatch commitBatch;
    
    leveldb::ReadOptions readOpts;
    readOpts.fill_cache = false;
    leveldb::WriteOptions writeOptions;
    writeOptions.sync = true;

    // if a value exists move it to the old key
    if (false == pDb->Get(readOpts, spKey, &spPrevValue).IsNotFound()) {
      commitBatch.Put(spPrevKey, spPrevValue);
    }
    commitBatch.Put(spKey, spValue);
    pDb->Write(writeOptions, &commitBatch);

    fprintf(mp_fp,"\nUpdated LevelDB with SP data successfully\n");
    return res;
}

unsigned int CMPSPInfo::putSP(unsigned char ecosystem, Entry const &info)
{
    std::string nextIdStr;
    unsigned int res = 0;
    switch(ecosystem) {
    case MASTERCOIN_CURRENCY_MSC: // mastercoin ecosystem, MSC: 1, TMSC: 2, First available SP = 3
      res = next_spid++;
      break;
    case MASTERCOIN_CURRENCY_TMSC: // Test MSC ecosystem, same as above with high bit set
      res = next_test_spid++;
      break;
    default: // non standard ecosystem, ID's start at 0
      res = 0;
    }

    Object spInfo = info.toJSON();

    // generate the SP id
    string spKey = (boost::format(FORMAT_BOOST_SPKEY) % res).str();
    string spValue = write_string(Value(spInfo), false);
    string txIndexKey = (boost::format(FORMAT_BOOST_TXINDEXKEY) % info.txid.ToString() ).str();
    string txValue = (boost::format("%d") % res).str();

    // sanity checking
    string existingEntry;
    leveldb::ReadOptions readOpts;
    readOpts.fill_cache = true;
    if (false == pDb->Get(readOpts, spKey, &existingEntry).IsNotFound() && false == boost::equals(spValue, existingEntry)) {
      fprintf(mp_fp, "%s WRITING SP %d TO LEVELDB WHEN A DIFFERENT SP ALREADY EXISTS FOR THAT ID!!!\n", __FUNCTION__, res);
    } else if (false == pDb->Get(readOpts, txIndexKey, &existingEntry).IsNotFound() && false == boost::equals(txValue, existingEntry)) {
      fprintf(mp_fp, "%s WRITING INDEX TXID %s : SP %d IS OVERWRITING A DIFFERENT VALUE!!!\n", __FUNCTION__, info.txid.ToString().c_str(), res);
    }

    // atomically write both the the SP and the index to the database
    leveldb::WriteOptions writeOptions;
    writeOptions.sync = true;

    leveldb::WriteBatch commitBatch;
    commitBatch.Put(spKey, spValue);
    commitBatch.Put(txIndexKey, txValue);

    pDb->Write(writeOptions, &commitBatch);
    return res;
}


bool CMPSPInfo::getSP(unsigned int spid, Entry &info)
{
    // special cases for constant SPs MSC and TMSC
    if (MASTERCOIN_CURRENCY_MSC == spid) {
      info = implied_msc;
      return true;
    } else if (MASTERCOIN_CURRENCY_TMSC == spid) {
      info = implied_tmsc;
      return true;
    }

    leveldb::ReadOptions readOpts;
    readOpts.fill_cache = true;

    string spKey = (boost::format(FORMAT_BOOST_SPKEY) % spid).str();
    string spInfoStr;
    if (false == pDb->Get(readOpts, spKey, &spInfoStr).ok()) {
      return false;
    }

    // parse the encoded json, failing if it doesnt parse or is an object
    Value spInfoVal;
    if (false == read_string(spInfoStr, spInfoVal) || spInfoVal.type() != obj_type ) {
      return false;
    }

    // transfer to the Entry structure
    Object &spInfo = spInfoVal.get_obj();
    info.fromJSON(spInfo);
    return true;
}

bool CMPSPInfo::hasSP(unsigned int spid)
{
    // special cases for constant SPs MSC and TMSC
    if (MASTERCOIN_CURRENCY_MSC == spid || MASTERCOIN_CURRENCY_TMSC == spid) {
      return true;
    }

    leveldb::ReadOptions readOpts;
    readOpts.fill_cache = true;

    string spKey = (boost::format(FORMAT_BOOST_SPKEY) % spid).str();
    leveldb::Iterator *iter = pDb->NewIterator(readOpts);
    iter->Seek(spKey);
    bool res = (iter->Valid() && iter->key().compare(spKey) == 0);
    // clean up the iterator
    delete iter;

    return res;
}

unsigned int CMPSPInfo::findSPByTX(uint256 const &txid)
{
    unsigned int res = 0;
    leveldb::ReadOptions readOpts;
    readOpts.fill_cache = true;

    string txIndexKey = (boost::format(FORMAT_BOOST_TXINDEXKEY) % txid.ToString() ).str();
    string spidStr;
    if (pDb->Get(readOpts, txIndexKey, &spidStr).ok()) {
      res = boost::lexical_cast<unsigned int>(spidStr);
    }

    return res;
}

int CMPSPInfo::popBlock(uint256 const &block_hash)
{
    int res = 0;
    int remainingSPs = 0;
    leveldb::WriteBatch commitBatch;

    leveldb::ReadOptions readOpts;
    readOpts.fill_cache = false;
    leveldb::Iterator *iter = pDb->NewIterator(readOpts);
    for (iter->Seek("sp-"); iter->Valid() && iter->key().starts_with("sp-"); iter->Next()) {
      // parse the encoded json, failing if it doesnt parse or is an object
      Value spInfoVal;
      if (read_string(iter->value().ToString(), spInfoVal) && spInfoVal.type() == obj_type ) {
        Entry info;
        info.fromJSON(spInfoVal.get_obj());
        if (info.update_block == block_hash) {
          // need to roll this SP back
          if (info.update_block == info.creation_block) {
            // this is the block that created this SP, so delete the SP and the tx index entry
            string txIndexKey = (boost::format(FORMAT_BOOST_TXINDEXKEY) % info.txid.ToString() ).str();
            commitBatch.Delete(iter->key());
            commitBatch.Delete(txIndexKey);
          } else {
            std::vector<std::string> vstr;
            std::string key = iter->key().ToString();
            boost::split(vstr, key, boost::is_any_of("-"), token_compress_on);
            unsigned int propertyID = boost::lexical_cast<unsigned int>(vstr[1]);

            string spPrevKey = (boost::format("blk-%s:sp-%d") % info.update_block.ToString() % propertyID).str();
            string spPrevValue;

            if (false == pDb->Get(readOpts, spPrevKey, &spPrevValue).IsNotFound()) {
              // copy the prev state to the current state and delete the old state
              commitBatch.Put(iter->key(), spPrevValue);
              commitBatch.Delete(spPrevKey);
              remainingSPs++;
            } else {
              // failed to find a previous SP entry, trigger reparse
              res = -1;
            }
          }
        } else {
          remainingSPs++;
        }
      } else {
        // failed to parse the db json, trigger reparse
        res = -1;
      }
    }

    // clean up the iterator
    delete iter;

    leveldb::WriteOptions writeOptions;
    writeOptions.sync = true;

    pDb->Write(writeOptions, &commitBatch);

    if (res < 0) {
      return res;
    } else {
      return remainingSPs;
    }
}

