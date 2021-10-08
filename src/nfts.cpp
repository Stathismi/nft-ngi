#include <nfts.hpp>

ACTION nfts::setconfig(string version)
{
  require_auth(get_self());

  // can only have one symbol per contract
  config_index config_table(get_self(), get_self().value);
  auto config_singleton = config_table.get_or_create( get_self(), tokenconfigs{ "cometogether"_n, version, 0 } );

  // setconfig will always update version when called
  config_singleton.version = version;
  config_table.set( config_singleton, get_self() );
}

ACTION nfts::createacc(uint64_t id, checksum256 signature, name caller) 
{
  require_auth(caller);

  user_index user_table(get_self(), get_self().value );
  auto user_id = user_table.find(id);
  check( user_id == user_table.end(), "This id already exists");
    user_table.emplace( caller, [&]( auto& u ){
        u.id = id;
        u.signature = signature;
    });
}

ACTION nfts::createnft(name issuer,
                       uint64_t event,
                       name nft_name,
                       bool burnable,
                       bool sellable,
                       bool transferable,
                       asset price,
                       uint8_t max_per_account,
                       double sale_split,
                       string base_uri,
                       asset max_supply)
{
    require_auth( issuer );

    check( max_per_account > 0, "Max NFTs per account should be greaten than zero");
    check( price.amount > 0, "Price amount must be positive" );
    check( price.symbol == symbol( symbol_code("COME"), 2), "Price must be in COME token");
    checkasset(max_supply);
    // check if issuer account exists and if split is between 0 and 1
    check( is_account( issuer ), "Issuer account does not exist" );
    check( ( sale_split <= 1.0 ) && ( sale_split >= 0.0 ), "Sale split must be between 0 and 1" );

    // get nft_category_id (global id)
    config_index config_table( get_self(), get_self().value );
    check( config_table.exists(), "Config table does not exist" );
    auto config_singleton = config_table.get();
    auto nft_category_id = config_singleton.nft_category_id;

    event_index events_table( get_self(), get_self().value );
    auto existing_event = events_table.find( event );

    // Create event in which the new nft category will be assigned, if it hasn't already created
    if( existing_event == events_table.end() ) {
      events_table.emplace( issuer, [&]( auto& ev ) {
          ev.event = event;
          ev.creator = issuer;
      });
    }

    else {
      check( existing_event->creator == issuer, "Issuer must be the creator of the event");
    }

    asset current_supply = asset( 0, symbol("CTT", max_supply.symbol.precision()));
    asset issued_supply = asset( 0, symbol("CTT", max_supply.symbol.precision()));

    stat_index nfts_stats_table( get_self(), event );
    auto existing_nft_stats = nfts_stats_table.find( nft_name.value );
    check( existing_nft_stats == nfts_stats_table.end(), "NFT with this name already exists in this event");

    // Create token, if it hasn't already created
    nfts_stats_table.emplace( issuer, [&]( auto& stats ){
      stats.nft_category_id = nft_category_id;
      stats.issuer = issuer;
      stats.nft_name = nft_name;
      stats.burnable = burnable;
      stats.sellable = sellable;
      stats.transferable = transferable;
      stats.price = price;
      stats.max_per_account = max_per_account;
      stats.current_supply = current_supply;
      stats.issued_supply = issued_supply;
      stats.sale_split = sale_split;
      stats.base_uri = base_uri;
      stats.max_supply = max_supply;
    });

    // successful creation of token, update category_name_id to reflect
    config_singleton.nft_category_id++;
    config_table.set( config_singleton, get_self() );
}

ACTION nfts::deleteeve(uint64_t event) {
  event_index events_table(get_self(), get_self().value);
  const auto& selected_event = events_table.get(event, "No event with this id");
  require_auth(selected_event.creator); // ensure that only the event creator can call the action
  events_table.erase(selected_event);
}

ACTION nfts::deletestats(uint64_t event, name nft_name){
  stat_index nfts_stats_table(get_self(), event);
  const auto& stats = nfts_stats_table.get(nft_name.value, "A NFT with this name does not exist in this event");
  require_auth( stats.issuer ); // ensure that only the nft issuer can call the action
  nfts_stats_table.erase(stats);
  config_index config_table( get_self(), get_self().value );
  check( config_table.exists(), "Config table does not exist" );
  auto config_singleton = config_table.get();
  config_singleton.nft_category_id--;
  config_table.set( config_singleton, get_self() );
}

ACTION nfts::issue(uint64_t to,
                      uint64_t event,
                      name nft_name,
                      asset quantity,
                      string relative_uri,
                      string memo)
{
    //check( is_account( to ), "to account does not exist" ); // comment out because we don't use eosio accounts for our users atm 
    check( memo.size() <= 256, "memo has more than 256 bytes" );

    user_index user_table( get_self(), get_self().value);
    auto user = user_table.find( to );
    check( user != user_table.end(), "User with this id doesn't exist");    

    stat_index nfts_stats_table( get_self(), event );
    const auto& nft_stats = nfts_stats_table.get( nft_name.value, "NFT with this name is not redeemable for this event");

    account_index account_table( get_self(), to );
    auto account = account_table.find( nft_stats.nft_category_id );
    string string_max_tickets = "Every account is able to buy " + to_string(nft_stats.max_per_account) + " NFTs";
    
    if( account == account_table.end() ) {
      // first time issued this asset
      check( quantity.amount <= nft_stats.max_per_account, string_max_tickets.c_str());
    }
    else {
      // already has a quantity of this nft category asset in account
      check( quantity.amount + account->amount.amount <= nft_stats.max_per_account, string_max_tickets.c_str());
    }

    //ensure that only issuer can call that action and that quantity is valid
    require_auth( nft_stats.issuer);

    checkasset(quantity);
    string string_prop = "precision of quantity must be " + to_string(nft_stats.max_supply.symbol.precision() );
    check( quantity.symbol == nft_stats.max_supply.symbol, string_prop.c_str() );
    check( quantity.amount <= (nft_stats.max_supply.amount - nft_stats.current_supply.amount), "Cannot issue more than max supply" );

    if ( quantity.amount > 1 ) {
      asset issued_supply = nft_stats.issued_supply;
      asset one_token = asset( 1, nft_stats.max_supply.symbol);
      for ( uint64_t i = 1; i <= quantity.amount; i++ ) {
          mint(to, nft_stats.issuer, event, nft_name, issued_supply, relative_uri);
          issued_supply += one_token;
      }
    }
    else {
        mint(to, nft_stats.issuer, event, nft_name, nft_stats.issued_supply, relative_uri);
    }


    add_balance(to, get_self(), event, nft_name, nft_stats.nft_category_id, quantity);

    // increase current&issued supply of the selected asset
    nfts_stats_table.modify( nft_stats, same_payer, [&]( auto& s ) {
        s.current_supply += quantity;
        s.issued_supply += quantity;
    });
}

ACTION nfts::transfer(uint64_t from,
                         uint64_t to,
                         vector<uint64_t> nft_ids,
                         string memo ) {

  check( from != to, "Cannot transfer NFT to self" );

  // ensure 'to' account exists
  //check( is_account( to ), "to account does not exist"); // comment out because we don't use eosio accounts for our users atm 

  user_index user_table( get_self(), get_self().value);
  auto user = user_table.find( from );
  check( user != user_table.end(), "User 'from' with this id doesn't exist");
  user = user_table.find( to );
  check( user != user_table.end(), "User 'to' with this id doesn't exist");

  // check memo size
  check( memo.size() <= 256, "memo has more than 256 bytes" );

  changeowner( from, to, nft_ids, memo, true );
}

ACTION nfts::listsale(uint64_t seller,
                         uint64_t event,
                         name nft_name,
                         vector<uint64_t> nft_ids,
                         asset net_sale_price)
{
    user_index user_table( get_self(), get_self().value);
    auto user = user_table.find( seller );
    check( user != user_table.end(), "User with this id doesn't exist");

    check( net_sale_price.amount > 0, "amount must be positive" );
    check( net_sale_price.symbol == symbol( symbol_code("COME"), 2), "Only accept COME token for sale");
    nft_index nfts_table( get_self(), get_self().value );

    for( auto const& nft_id: nft_ids) {
      const auto& nft = nfts_table.get( nft_id, "NFT does not exist" );

      stat_index nfts_stats_table( get_self(), nft.event );
      const auto& nft_stats = nfts_stats_table.get( nft.nft_name.value, "A NFT with this name does not exist in this event" );
      require_auth( nft_stats.issuer); // ensure that only issuer can call the action

      check( nft.shared_with == NULL, "NFT must not be in a shareable mode");
      check( nft_stats.sellable == true, "Must be sellable" );
      check( nft.owner == seller, "Must be nft owner" );
      check( nft.event == event, "NFTs must be from the same event" );
      check( nft.nft_name == nft_name, "NFTs must have the same nft name" );

      // Check if nft is locked
      lock_index lockednfts_table( get_self(), get_self().value );
      auto lockednft = lockednfts_table.find( nft_id );
      check( lockednft == lockednfts_table.end(), "NFT locked ");

      // add resale price to nft
      nfts_table.modify(nft, same_payer, [&]( auto& t){
        t.resale_price = net_sale_price/nft_ids.size();
      });

      // add nft to lock stats_table
      lockednfts_table.emplace( get_self(), [&]( auto& l ){
        l.nft_id = nft_id;
      });
    }

    // add batch to table of asks
    ask_index asks_table( get_self(), get_self().value );
    asks_table.emplace( get_self(), [&]( auto& a ){
      a.batch_id = nft_ids[0];
      a.nft_ids = nft_ids;
      a.event = event;
      a.seller = seller;
      a.ask_price = net_sale_price;
      a.expiration = time_point_sec(current_time_point()) + WEEK_SEC;
    });
}

ACTION nfts::closesale( uint64_t seller,
                            uint64_t batch_id)
{
    ask_index asks_table( get_self(), get_self().value );
    const auto& ask = asks_table.get( batch_id, "Cannot find the desirable sale" );

    user_index user_table( get_self(), get_self().value);
    auto user = user_table.find( seller );
    check( user != user_table.end(), "User with this id doesn't exist");

    lock_index lockednfts_table( get_self(), get_self().value );
    nft_index nfts_table( get_self(), get_self().value );


    for( auto const& nft_id : ask.nft_ids ) {
      const auto& nft = nfts_table.get( nft_id, "NFT does not exist" );

      stat_index nfts_stats_table( get_self(), nft.event );
      const auto& nft_stats = nfts_stats_table.get( nft.nft_name.value, "A NFT with this name does not exist in this event" );
      require_auth( nft_stats.issuer); // ensure that only issuer can call the action

      nfts_table.modify(nft, same_payer, [&]( auto& t){
        t.resale_price = asset(0, symbol("COME", 2));;
      });

    }

    if( time_point_sec(current_time_point()) > ask.expiration ) {
      for( auto const& nft_id: ask.nft_ids ) {
        const auto& lockednft = lockednfts_table.get( nft_id, "NFT not found in lock table" );
        lockednfts_table.erase( lockednft );
      }
      asks_table.erase( ask );
    }
    else {
      //require_auth( seller );
      check( ask.seller == seller, "Only seller can cancel a sale in progress" );
      for( auto const& nft_id : ask.nft_ids ) {
        const auto& lockednft = lockednfts_table.get( nft_id, "NFT was not found in lock table" );
        lockednfts_table.erase( lockednft );
      }
      asks_table.erase( ask );
    }
}

ACTION nfts::share(uint64_t from, uint64_t nft_id, uint64_t to) {
  check( from != to, "Cannot share to self" );

  lock_index lockednfts_table( get_self(), get_self().value );
  auto lockednft = lockednfts_table.find( nft_id );
  check( lockednft == lockednfts_table.end(), "NFT is locked, it cannot be shared");

  nft_index nfts_table( get_self(), get_self().value );
  const auto& nft = nfts_table.get( nft_id, "NFT does not exist" );

  stat_index nfts_stats_table( get_self(), nft.event );
  const auto& nft_stats = nfts_stats_table.get( nft.nft_name.value, "A NFT with this name does not exist in this event" );
  require_auth( nft_stats.issuer); // ensure that only issuer can call the action

  nfts_table.modify( nft, same_payer,  [&]( auto& t ){
    t.shared_with = to;
  });
}

ACTION nfts::unshare(uint64_t nft_id){
  nft_index nfts_table( get_self(), get_self().value );
  const auto& nft = nfts_table.get( nft_id, "NFT does not exist" );

  stat_index nfts_stats_table( get_self(), nft.event );
  const auto& nft_stats = nfts_stats_table.get( nft.nft_name.value, "A NFT with this name does not exist in this event" );
  require_auth( nft_stats.issuer); // ensure that only issuer can call the action
  
  nfts_table.modify( nft, same_payer,  [&]( auto& t ){
    t.shared_with = NULL;
  });
}

ACTION nfts::buy(uint64_t to, uint64_t batch_id,  string memo)
{
  user_index user_table( get_self(), get_self().value);
  auto user = user_table.find( to );
  check( user != user_table.end(), "User with this id doesn't exist");

  check( memo.length() <= 32, "Memo should be less than 32 bytes" );

  ask_index asks_table( get_self(), get_self().value );
  const auto& ask = asks_table.get( batch_id, "Cannot find listing" );
  check( ask.expiration > time_point_sec(current_time_point()), "Sale has expired" );

  string string_prop = "bought by: " + to_string(to);

  changeowner( ask.seller, to, ask.nft_ids, string_prop.c_str(), false);

  lock_index lockednfts_table( get_self(), get_self().value );
  nft_index nfts_table( get_self(), get_self().value );

  for( auto const& nft_id : ask.nft_ids ) {
    const auto& nft = nfts_table.get( nft_id, "NFT does not exist" );
    nfts_table.modify(nft, same_payer, [&]( auto& t){
      t.resale_price = asset(0, symbol("COME", 2));
    });

    const auto& lockednft = lockednfts_table.get( nft_id, "NFT not found in lock table" );
    lockednfts_table.erase( lockednft );
  }

  //remove sale listing
  asks_table.erase( ask );
}

ACTION nfts::createauctn(uint64_t seller, uint64_t event, uint64_t nft_id, asset target_price, asset min_bid_price, time_point_sec expiration)
{
  require_auth(get_self());
  user_index user_table( get_self(), get_self().value);
  auto user = user_table.find( seller );
  check( user != user_table.end(), "User with this id doesn't exist");
 
  // target price validations  
  check( target_price.amount > 0, "Target price must be positive" );
  check( target_price.symbol == symbol( symbol_code("COME"), 2), "Only accept COME token for auction");
  // minimum bid price validations
  check( target_price.amount > 0, "Minimum bid price must be positive" );
  check( target_price.symbol == symbol( symbol_code("COME"), 2), "Only accept COME token for auction");
  
  nft_index nfts_table( get_self(), get_self().value );
  const auto& nft = nfts_table.get( nft_id, "NFT does not exist" );

  stat_index nfts_stats_table( get_self(), nft.event );
  const auto& nft_stats = nfts_stats_table.get( nft.nft_name.value, "NFT stats does not exist" );

  check( nft.shared_with == NULL, "NFT must not be in a shareable mode");
  check( nft_stats.sellable == true, "Must be sellable" );
  check( nft.owner == seller, "Must be nft owner" );
  check( nft.event == event, "NFTs must be from the same event" );

  // Check if nft is locked
  lock_index lockednfts_table( get_self(), get_self().value );
  auto lockednft = lockednfts_table.find( nft_id );
  check( lockednft == lockednfts_table.end(), "NFT locked ");

  // add nft to lock stats_table
  lockednfts_table.emplace( get_self(), [&]( auto& l ){
    l.nft_id = nft_id;
  });
    
  // add auction to the respective table
  auction_index auctions_table( get_self(), get_self().value );
  auctions_table.emplace( get_self(), [&]( auto& a ){
    a.nft_id = nft_id;
    a.event = event;
    a.seller = seller;
    a.target_price = target_price;
    a.min_bid_price = min_bid_price;
    a.current_price = asset(NULL, symbol("COME", 2));
    //a.expiration = time_point_sec(current_time_point()) + WEEK_SEC;
    a.expiration = expiration;

  });

}

ACTION nfts::closeauctn(uint64_t seller, uint64_t nft_id) 
{
  require_auth(get_self());
  auction_index auctions_table( get_self(), get_self().value );
  const auto& auction = auctions_table.get( nft_id, "Cannot find the desirable auction" );

  user_index user_table( get_self(), get_self().value);
  auto user = user_table.find( seller );
  check( user != user_table.end(), "User with this id doesn't exist");

  lock_index lockednfts_table( get_self(), get_self().value );

  check( time_point_sec(current_time_point()) < auction.expiration, "Auction is not in progress, you need to call the finalize action" ); // is auction still in progress?
  check( auction.seller == seller, "Only seller can cancel an auction in progress" );
    
  const auto& lockednft = lockednfts_table.get( nft_id, "NFT not found in lock table" );
  lockednfts_table.erase( lockednft );
  auctions_table.erase( auction );
}

ACTION nfts::bid(uint64_t nft_id, uint64_t bidder, asset bid_price)
{
  require_auth(get_self());
  auction_index auctions_table( get_self(), get_self().value );
  const auto& auction = auctions_table.get( nft_id, "Cannot find the desirable auction" );

  user_index user_table( get_self(), get_self().value);
  auto user = user_table.find( bidder );
  check( user != user_table.end(), "User with this id doesn't exist"); 

  // bid price validations  
  check( bid_price.amount > 0, "Bid price must be positive" );
  check( bid_price.symbol == symbol( symbol_code("COME"), 2), "Only accept COME token for auction");

  check( time_point_sec(current_time_point()) < auction.expiration, "Auction has ended" ); // is auction still in progress?
  check( bidder != auction.seller, "You cannot bid at your own auction" );
  check( bid_price > auction.current_price , "Your bid price is lower than the current one" );

  if (bid_price >= auction.target_price) {
    // the target price has been reached, so this is an instant buy bid
    string memo = "auction bought by: " + to_string(auction.bidder);
    vector<uint64_t> nft_ids{ nft_id }; // we transform the single value to a vector to match the parameter type needed by the changeowner function
    changeowner( auction.seller, bidder, nft_ids, memo.c_str(), false);

    // unlock nft & remove auction listing
    lock_index lockednfts_table( get_self(), get_self().value );
    const auto& lockednft = lockednfts_table.get( nft_id, "NFT not found in lock table" );
    lockednfts_table.erase( lockednft );
    auctions_table.erase( auction );  
  }
  else {
    check( bid_price - auction.current_price >= auction.min_bid_price , "Bid must be greater than the minimum bid price" );
    // new bid is greater than the current price, so we update the top bidder
    auctions_table.modify( auction, same_payer,  [&]( auto& t ){
      t.current_price = bid_price;
      t.bidder = bidder;
    });
  }
}

ACTION nfts::finalize(uint64_t nft_id, uint64_t seller)
{
  require_auth(get_self());
  auction_index auctions_table( get_self(), get_self().value );
  const auto& auction = auctions_table.get( nft_id, "Cannot find the desirable auction" );

  user_index user_table( get_self(), get_self().value);
  auto user = user_table.find( seller );
  check( user != user_table.end(), "User with this id doesn't exist"); 

  check( auction.seller == seller, "Only seller can finalize the auction" );
  check( time_point_sec(current_time_point()) > auction.expiration, "You cannot finalize an auction before its expiration" ); // is auction still in progress?

  if ( auction.bidder != 0) {
    // someone has a winning bid for this auction
    string memo = "auction bought by: " + to_string(auction.bidder);
    vector<uint64_t> nft_ids{ nft_id }; // we transform the single value to a vector to match the parameter type needed by the changeowner function
    changeowner( seller, auction.bidder, nft_ids, memo.c_str(), false);
  }

  // unlock nft & remove auction listing
  lock_index lockednfts_table( get_self(), get_self().value );
  const auto& lockednft = lockednfts_table.get( nft_id, "NFT not found in lock table" );
  lockednfts_table.erase( lockednft );
  auctions_table.erase( auction ); 
}


void nfts::checkasset(const asset& amount) {
  auto sym = amount.symbol;
  symbol_code req = symbol_code("CTT");
  check( sym.precision() == 0, "Symbol must be an int, with precision of 0" );
  check( amount.amount >= 1, "Amount must be >=1");
  check( sym.code().raw() == req.raw(), "Symbol must be CTT" );
  check( amount.is_valid(), "Invalid amount");
}

void nfts::mint(const uint64_t& to, const name& issuer, const uint64_t& event, const name& nft_name, const asset& issued_supply, const string& relative_uri)
{
  nft_index nfts_table( get_self(), get_self().value);
  auto nft_id = nfts_table.available_primary_key();

  if( relative_uri.empty() ) {
    nfts_table.emplace( issuer, [&]( auto& t ){
      t.id = nft_id;
      t.serial_number = issued_supply.amount + 1;
      t.event = event;
      t.owner = to;
      t.resale_price = asset(NULL, symbol("COME", 2));
      t.nft_name = nft_name;
    });
  } else {
      nfts_table.emplace( issuer, [&]( auto& t ) {
        t.id = nft_id;
        t.serial_number = issued_supply.amount + 1;
        t.event = event;
        t.owner = to;
        t.resale_price = asset(NULL, symbol("COME", 2));
        t.nft_name = nft_name;
        t.relative_uri = relative_uri;
      });
    }
}

// Helper function to add asset balance to user's account
void nfts::add_balance(const uint64_t& owner, const name& ram_payer, const uint64_t& event, const name& nft_name, const uint64_t& nft_category_id, const asset& quantity )
{
  account_index to_acnts( get_self(), owner );
  auto to = to_acnts.find( nft_category_id );
  if( to == to_acnts.end() ) {
    to_acnts.emplace( ram_payer, [&]( auto& a ){
      a.nft_category_id = nft_category_id;
      a.event = event;
      a.nft_name = nft_name;
      a.amount = quantity;
    });
  } else {
    to_acnts.modify( to, same_payer, [&]( auto& a ){
      a.amount += quantity;
    });
  }
}

// Helper function to subtract asset balance to user's account
void nfts::sub_balance(const uint64_t& owner, const uint64_t& nft_category_id, const asset& quantity)
{
  account_index from_acnts( get_self(), owner );
  auto from = from_acnts.find( nft_category_id );
  check( from->amount.amount >= quantity.amount, "Quantity must be equal or less than account balance" );

  if( from->amount.amount == quantity.amount ) {
    from_acnts.erase(from);
  } else {
    from_acnts.modify( from, same_payer, [&]( auto& a ){
      a.amount -= quantity;
    });
  }
}

void nfts::changeowner(const uint64_t& from, const uint64_t& to, vector<uint64_t> nft_ids, const string& memo, bool istransfer) {

  nft_index nfts_table(get_self(), get_self().value);
  lock_index lockednfts_table(get_self(), get_self().value);

  for( auto const& nft_id : nft_ids ) {
    const auto& nft = nfts_table.get( nft_id, "NFT not found");

    stat_index nfts_stats_table( get_self(), nft.event );
    const auto& nft_stat = nfts_stats_table.get( nft.nft_name.value, "A NFT with this name does not exist in this event" );
    require_auth( nft_stat.issuer); // ensure that only issuer can call the action

    if( istransfer ) {
      check( nft.owner == from, "Must be the owner");
      check( nft_stat.transferable == true, "Not transferable");
      auto locked_nft = lockednfts_table.find( nft_id);
      check( locked_nft == lockednfts_table.end(), "NFT is locked, so it cannot transferred");
    }

    nfts_table.modify( nft, same_payer, [&] ( auto& t ){
      t.owner = to;
    });

    asset quantity( 1, nft_stat.max_supply.symbol );
    sub_balance( from, nft_stat.nft_category_id, quantity );
    add_balance( to, get_self(), nft.event, nft.nft_name, nft_stat.nft_category_id, quantity );
  }
}

EOSIO_DISPATCH(nfts, (setconfig)(createacc)(createnft)(deleteeve)(deletestats)(issue)(transfer)(listsale)(closesale)(share)(unshare)(buy)(createauctn)(closeauctn)(bid)(finalize))
