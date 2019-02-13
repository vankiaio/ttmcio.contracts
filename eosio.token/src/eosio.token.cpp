/**
 *  @file
 *  @copyright defined in eos/LICENSE.txt
 */

#include <eosio.token/eosio.token.hpp>
#include <eosiolib/action.hpp>
#include <eosiolib/transaction.hpp>

namespace eosio {

void token::create( name   issuer,
                    asset  maximum_supply )
{
    require_auth( _self );

    auto sym = maximum_supply.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( maximum_supply.is_valid(), "invalid supply");
    eosio_assert( maximum_supply.amount > 0, "max-supply must be positive");

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    eosio_assert( existing == statstable.end(), "token with symbol already exists" );

    statstable.emplace( _self, [&]( auto& s ) {
       s.supply.symbol = maximum_supply.symbol;
       s.max_supply    = maximum_supply;
       s.issuer        = issuer;
    });
}


void token::issue( name to, asset quantity, string memo )
{
    auto sym = quantity.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    eosio_assert( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
    const auto& st = *existing;

    require_auth( st.issuer );
    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must issue positive quantity" );

    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    eosio_assert( quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

    statstable.modify( st, same_payer, [&]( auto& s ) {
       s.supply += quantity;
    });

    add_balance( st.issuer, quantity, st.issuer );

    if( to != st.issuer ) {
      SEND_INLINE_ACTION( *this, transfer, { {st.issuer, "active"_n} },
                          { st.issuer, to, quantity, memo }
      );
    }
}


void token::issuelock( name to, asset quantity, string memo, asset lockquantity, uint32_t unlock_delay_sec )
{
    auto sym = quantity.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    eosio_assert( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
    const auto& st = *existing;

    require_auth( st.issuer );
    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must issue positive quantity" );

    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    eosio_assert( quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

    statstable.modify( st, same_payer, [&]( auto& s ) {
       s.supply += quantity;
    });

    add_balance( st.issuer, quantity, st.issuer );

    if( to != st.issuer ) {
      SEND_INLINE_ACTION( *this, transfer, { {st.issuer, "active"_n} },
                          { st.issuer, to, quantity, memo }
      );
      SEND_INLINE_ACTION( *this, lock, { {st.issuer, "active"_n} },
                          { to, lockquantity, unlock_delay_sec }
      );
      SEND_INLINE_ACTION( *this, unlock, { {st.issuer, "active"_n} },
                          { to, symbol_code("TTMC") }
      );
    }
}

void token::retire( asset quantity, string memo )
{
    auto sym = quantity.symbol;
    eosio_assert( sym.is_valid(), "invalid symbol name" );
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

    stats statstable( _self, sym.code().raw() );
    auto existing = statstable.find( sym.code().raw() );
    eosio_assert( existing != statstable.end(), "token with symbol does not exist" );
    const auto& st = *existing;

    require_auth( st.issuer );
    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must retire positive quantity" );

    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

    statstable.modify( st, same_payer, [&]( auto& s ) {
       s.supply -= quantity;
    });

    sub_balance( st.issuer, quantity );
}

void token::transfer( name    from,
                      name    to,
                      asset   quantity,
                      string  memo )
{
    eosio_assert( from != to, "cannot transfer to self" );
    require_auth( from );
    eosio_assert( is_account( to ), "to account does not exist");
    auto sym = quantity.symbol.code();
    stats statstable( _self, sym.raw() );
    const auto& st = statstable.get( sym.raw() );

    require_recipient( from );
    require_recipient( to );

    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must transfer positive quantity" );
    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
    eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

    locked_accounts locked_acnts( _self, from.value );
    auto target = locked_acnts.find( quantity.symbol.code().raw() );
    if ( target != locked_acnts.end() ) {
       // check if sufficient amount is not locked for this transfer
       accounts from_acnts( _self, from.value );
       const auto& from = from_acnts.get( quantity.symbol.code().raw(), "no balance object found" );
       eosio_assert( from.balance.amount - target->total_balance.amount >= quantity.amount, "locked balance" );
    }

    auto payer = has_auth( to ) ? to : from;

    sub_balance( from, quantity );
    add_balance( to, quantity, payer );
}

void token::lock( name owner, asset quantity, uint32_t unlock_delay_sec )
{
    auto sym = quantity.symbol.code();
    stats statstable( _self, sym.raw() );
    const auto& st = statstable.get( sym.raw() );

    require_auth( st.issuer );

    eosio_assert( quantity.is_valid(), "invalid quantity" );
    eosio_assert( quantity.amount > 0, "must lock positive quantity" );
    eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

    eosio_assert( unlock_delay_sec > 0, "must set unlock delay");
    //eosio_assert( unlock_delay_sec > 3*24*3600, "must set unlock delay less than refund delay");

    accounts acnts( _self, owner.value );
    const auto& acnt = acnts.get( quantity.symbol.code().raw(), "no balance object found" );
    eosio_assert( quantity.amount <= acnt.balance.amount, "quantity to lock is larger than current balance" );

    locked_accounts locked_acnts( _self, owner.value );
    auto target = locked_acnts.find( quantity.symbol.code().raw() );

    if( target == locked_acnts.end() ) {
       locked_acnts.emplace( st.issuer, [&]( auto& a ) {
          a.total_balance = quantity;
          a.balances.reserve( 1 );
          a.balances.push_back( frozen_balance{quantity, unlock_delay_sec, time_point_sec::min(), time_point_sec::min()} );
         //  a.unlock_delay_sec = unlock_delay_sec;
         //  a.unlock_request_time = time_point_sec::min();
         //  a.unlock_execute_time = time_point_sec::min();
       });
    } else {
       eosio_assert( quantity.amount + target->total_balance.amount < acnt.balance.amount, "increased quantity to lock is larger than current balance" );
       locked_acnts.modify( target, same_payer, [&]( auto& a ) {
          a.total_balance.amount += quantity.amount;
          a.balances.reserve( a.balances.size() + 1 );
          a.balances.push_back( frozen_balance{quantity, unlock_delay_sec, time_point_sec::min(), time_point_sec::min()} );
       });
    }
}

void token::unlock( name owner, symbol_code sym_code )
{
   auto sym = sym_code;
   stats statstable( _self, sym.raw() );
   const auto& st = statstable.get( sym.raw() );

   require_auth( st.issuer );

   // if( quantity.amount <= 0){
   //    cancel_deferred( owner.value ); // check if needed
   //    return;
   // }

   // eosio_assert( quantity.is_valid(), "invalid quantity" );
   // eosio_assert( quantity.amount > 0, "must unlock positive quantity" );
   // eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
   locked_accounts locked_acnts( _self, owner.value );
   auto target = locked_acnts.find( sym_code.raw() );
   eosio_assert( target != locked_acnts.end(), "no locked balance object found" );
   eosio_assert( 0 < target->total_balance.amount && 0 < target->balances.size(), "no locked balances item found" );

   locked_acnts.modify( target, same_payer, [&]( auto& a ) {
      for(auto itr = a.balances.begin(); itr != a.balances.end();itr++) {
         if(itr->unlock_request_time <= time_point_sec::min() + seconds(1)){
            itr->unlock_request_time = time_point_sec(now());
         }
         if(itr->unlock_execute_time <= time_point_sec::min() + seconds(1)){
            itr->unlock_execute_time = time_point_sec(now()) + itr->unlock_delay_sec;
         }
      }
      if(a.balances.size() > 1) {
         std::sort (a.balances.begin(), a.balances.end(), sort_by_execute_time);
      }
   });

   eosio::transaction out;
   out.actions.emplace_back( permission_level{owner, "active"_n},
                             _self, "dounlock"_n,
                             std::make_tuple(owner, symbol_code("TTMC"))
   );
   int32_t ndelay_sec = target->balances.begin()->unlock_execute_time.sec_since_epoch() - time_point_sec(now()).sec_since_epoch() ;
   if(ndelay_sec <= 0) {
      out.delay_sec = 3;
   } else {
      out.delay_sec = ndelay_sec;
   }
   printf("unlock-----out.delay_sec=%d",out.delay_sec);
   cancel_deferred( owner.value ); // check if needed
   out.send( owner.value, st.issuer, true );

}

void token::dounlock( name owner, symbol_code sym_code )
{
   auto sym = sym_code;
   stats statstable( _self, sym.raw() );
   const auto& st = statstable.get( sym.raw() );

   locked_accounts locked_acnts( _self, owner.value );
   auto target = locked_acnts.find( sym_code.raw() );
   eosio_assert( target != locked_acnts.end(), "no locked balance object found" );
   eosio_assert( target->balances.begin()->unlock_request_time > time_point_sec::min(), "unlock is not requested" );
   eosio_assert( time_point_sec(target->balances.begin()->unlock_request_time + seconds(target->balances.begin()->unlock_delay_sec)) <= time_point_sec(now()),
                 "unlock is not avalialbe yet");
   eosio_assert( 0 < target->total_balance.amount && 0 < target->balances.size(), "no locked balances item found" );

   if (target->total_balance.amount >= target->balances.begin()->balance.amount) {
      locked_acnts.modify( target, same_payer, [&]( auto& a ) {
         a.total_balance.amount -= a.balances.begin()->balance.amount;
         a.balances.erase(a.balances.begin());
      });
   } else {
      locked_acnts.erase( target );
   }

   if(target->balances.size() > 0 && target->total_balance.amount > 0)
   {
      SEND_INLINE_ACTION( *this, unlock, { {st.issuer, "active"_n} },
                     { owner, symbol_code("TTMC") }
      );
   } else {
      locked_acnts.erase( target );
   }
}

void token::sub_balance( name owner, asset value ) {
   accounts from_acnts( _self, owner.value );

   const auto& from = from_acnts.get( value.symbol.code().raw(), "no balance object found" );
   eosio_assert( from.balance.amount >= value.amount, "overdrawn balance" );

   from_acnts.modify( from, owner, [&]( auto& a ) {
         a.balance -= value;
      });
}

void token::add_balance( name owner, asset value, name ram_payer )
{
   accounts to_acnts( _self, owner.value );
   auto to = to_acnts.find( value.symbol.code().raw() );
   if( to == to_acnts.end() ) {
      to_acnts.emplace( ram_payer, [&]( auto& a ){
        a.balance = value;
      });
   } else {
      to_acnts.modify( to, same_payer, [&]( auto& a ) {
        a.balance += value;
      });
   }
}

void token::open( name owner, const symbol& symbol, name ram_payer )
{
   require_auth( ram_payer );

   auto sym_code_raw = symbol.code().raw();

   stats statstable( _self, sym_code_raw );
   const auto& st = statstable.get( sym_code_raw, "symbol does not exist" );
   eosio_assert( st.supply.symbol == symbol, "symbol precision mismatch" );

   accounts acnts( _self, owner.value );
   auto it = acnts.find( sym_code_raw );
   if( it == acnts.end() ) {
      acnts.emplace( ram_payer, [&]( auto& a ){
        a.balance = asset{0, symbol};
      });
   }
}

void token::close( name owner, const symbol& symbol )
{
   require_auth( owner );
   accounts acnts( _self, owner.value );
   auto it = acnts.find( symbol.code().raw() );
   eosio_assert( it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect." );
   eosio_assert( it->balance.amount == 0, "Cannot close because the balance is not zero." );
   acnts.erase( it );
}

} /// namespace eosio

EOSIO_DISPATCH( eosio::token, (create)(issue)(issuelock)(transfer)(open)(close)(retire)(lock)(unlock)(dounlock) )
