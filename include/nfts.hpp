#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/singleton.hpp>

using namespace std;
using namespace eosio;

CONTRACT nfts : public contract {
  public:
    using contract::contract;

    const int WEEK_SEC = 3600*24*7;

    ACTION setconfig(string version);

    ACTION createacc(uint64_t id, checksum256 signature, name caller);

    ACTION createnft(name issuer,
                uint64_t event,
                name nft_name,
                bool burnable,
                bool sellable,
                bool transferable,
                asset price,
                uint8_t max_per_account,
                double sale_split,
                string base_uri,
                asset max_supply);

    ACTION deleteeve(uint64_t event);

    ACTION deletestats(uint64_t event, name nft_name);

    ACTION issue(uint64_t to,
               uint64_t event,
               name nft_name,
               asset quantity,
               string relative_uri,
               string memo);

    ACTION transfer(uint64_t from, uint64_t to, vector<uint64_t> nft_ids, string memo);

    ACTION listsale(uint64_t seller, uint64_t event, name nft_name, vector<uint64_t> nft_ids, asset net_sale_price);

    ACTION closesale(uint64_t seller, uint64_t batch_id);

    ACTION share(uint64_t from, uint64_t nft_id, uint64_t to);

    ACTION unshare(uint64_t nft_id);

    ACTION buy(uint64_t to, uint64_t batch_id,  string memo);

    ACTION createauctn(uint64_t seller, uint64_t event, uint64_t nft_id, asset target_price, asset min_bid_price, time_point_sec expiration);

    ACTION closeauctn(uint64_t seller, uint64_t nft_id);

    ACTION bid(uint64_t nft_id, uint64_t bidder, asset bid_price);

    ACTION finalize(uint64_t nft_id, uint64_t seller);

    nfts(name receiver, name code, datastream<const char*> ds): contract(receiver, code, ds) {}

    TABLE tokenconfigs {
        name standard;
        string version;
        uint64_t nft_category_id;
     };

    // Table with events for which redeemable NFTs exists
    TABLE event {
        uint64_t event;
        name creator;

        uint64_t primary_key() const { return event; }
     };

    // scope is event
    // Table with all the information for each NFT category
    TABLE nft_stat {
      uint64_t nft_category_id;
      bool burnable;
      bool sellable;
      bool transferable;
      name issuer;
      name nft_name;
      asset price;
      uint8_t max_per_account;
      asset max_supply;
      asset current_supply;
      asset issued_supply;
      double sale_split;
      string base_uri;

      uint64_t primary_key() const { return nft_name.value; }
     };

    // Table with all the registered users
    TABLE user {
      uint64_t id;
      checksum256 signature;

      uint64_t primary_key() const { return id;}
    };

    // scope is self
    TABLE nft {
      uint64_t id;
      uint64_t serial_number;
      uint64_t event;
      uint64_t owner;
      name nft_name;
      asset resale_price;
      uint64_t shared_with;
      std::optional<string> relative_uri; //for specific metadata to the ticket

      uint64_t primary_key() const { return id; }
      uint64_t get_owner() const { return owner; }
      uint64_t get_byeve() const { return event;}
      uint64_t get_byshare() const { return shared_with; }
    };
    EOSLIB_SERIALIZE( nft, (id)(serial_number)(event)(owner)(nft_name)(resale_price)(shared_with)(relative_uri) )

    // scope is owner
    TABLE account {
      uint64_t nft_category_id;
      uint64_t event;
      name nft_name;
      asset amount;

      uint64_t primary_key() const { return nft_category_id; }
     };
    
    TABLE ask {
      uint64_t batch_id;
      uint64_t event;
      vector<uint64_t> nft_ids;
      uint64_t seller;
      asset ask_price; 
      time_point_sec expiration;

      uint64_t primary_key() const { return batch_id; }
      uint64_t get_byevent() const { return event; }
      uint64_t get_byprice() const { return ask_price.amount; }

    };

    TABLE lockednft {
      uint64_t nft_id;

      uint64_t primary_key() const { return nft_id; }
    };

    TABLE auction {
      uint64_t nft_id;
      uint64_t event;
      uint64_t seller;
      asset target_price; // instant buy bid
      asset min_bid_price;
      asset current_price; // the current winning bid
      uint64_t bidder; // the eos id of the current winning bidder
      time_point_sec expiration;

      uint64_t primary_key() const { return nft_id; }
      uint64_t get_seller() const { return seller; }
      uint64_t get_bidder() const { return bidder; }
    };

    using config_index = eosio::singleton<"tokenconfigs"_n, tokenconfigs>;
    using event_index = eosio::multi_index<"events"_n, event>;
    using stat_index = eosio::multi_index<"nftstats"_n, nft_stat>;
    using user_index = eosio::multi_index<"users"_n, user>;
    using account_index = eosio::multi_index<"accounts"_n, account>;
    using nft_index = eosio::multi_index<"nfts"_n, nft, indexed_by<"byowner"_n, const_mem_fun<nft, uint64_t, &nft::get_owner>>, indexed_by<"byeve"_n, const_mem_fun<nft, uint64_t, &nft::get_byeve>>, indexed_by<"byshare"_n, const_mem_fun<nft, uint64_t, &nft::get_byshare>>>;
    using ask_index = eosio::multi_index<"asks"_n, ask, indexed_by<"byevent"_n, const_mem_fun<ask, uint64_t, &ask::get_byevent>>, indexed_by<"byprice"_n, const_mem_fun< ask, uint64_t, &ask::get_byprice>>>;
    using lock_index = eosio::multi_index<"lockednfts"_n, lockednft>;
    using auction_index = eosio::multi_index<"auctions"_n, auction, indexed_by<"byseller"_n, const_mem_fun<auction, uint64_t, &auction::get_seller>>, indexed_by<"bybidder"_n, const_mem_fun<auction, uint64_t, &auction::get_bidder>>>;
    
  private:
    void checkasset(const asset& amount);
    void mint(const uint64_t& to, const name& issuer, const uint64_t& event, const name& nft_name, const asset& issued_supply, const string& relative_uri);
    void add_balance(const uint64_t& owner, const name& ram_payer, const uint64_t& event, const name& nft_name, const uint64_t& nft_category_id, const asset& quantity );
    void sub_balance(const uint64_t& owner, const uint64_t& nft_category_id, const asset& quantity);
    void changeowner(const uint64_t& from, const uint64_t& to, vector<uint64_t> nft_ids, const string& memo, bool istransfer);
    
};
