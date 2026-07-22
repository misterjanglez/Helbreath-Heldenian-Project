#pragma once

#include "PacketHeaders.h"
#include "NetConstants.h"

#include <cstdint>

// PacketTradingPost.h - Trading Post protocol packets
//
// The Trading Post is the escrow-based barter market opened through the
// Auctioneer NPC (Vince). Clients send Tp* requests; the server answers browse
// and detail queries with dedicated response packets, and answers every
// mutating action with a PacketResponseTpActionResult carrying a TpResultCode.
//
// Item data is never trusted from the client: create/offer requests carry
// inventory slot indices + amounts only, and the server re-reads the
// character's item list at request time.
//
// Field names follow the Trading Post domain language (see CONTEXT.md):
// Seller, Listing, Offer, Finalize, Rescind, Delist, Seeking note.

namespace hb {
namespace net {

	// Which Trading Post action a PacketResponseTpActionResult refers to.
	namespace TpAction
	{
		enum : uint16_t
		{
			BoardPage       = 0,
			ListingDetail   = 1,
			CreateListing   = 2,
			PlaceOffer      = 3,
			RescindOffer    = 4,
			Finalize        = 5,
			Delist          = 6,
			MyBoard         = 7,   // my-Listings / my-Offers query (error channel)
		};
	}

	// Which personal slice of the board PacketRequestTpMyBoard asks for. The
	// response reuses PacketResponseTpBoardPage (same row shape as the public
	// board); the server just filters the rows by the actor's identity.
	namespace TpMyBoardFilter
	{
		enum : uint8_t
		{
			MyListings = 0,   // Listings the actor is the Seller of
			MyOffers   = 1,   // Listings the actor has an active Offer on
		};
	}

	// Outcome codes returned in PacketResponseTpActionResult::result_code.
	namespace TpResultCode
	{
		enum : uint16_t
		{
			Ok                = 0,   // action succeeded (value carries an id/count where relevant)
			TooManyListings   = 1,   // Seller already has TpMaxListingsPerChar active Listings
			ListingGone       = 2,   // Listing no longer exists (expired, delisted, or race loser)
			OfferGone         = 3,   // Offer no longer exists (rescinded or race loser)
			AccountSelfTrade  = 4,   // offerer's account owns the Listing (same-account block)
			TooManyOffers     = 5,   // Listing already has TpMaxOffersPerListing Offers
			AlreadyOffered    = 6,   // character already has an active Offer on this Listing
			InventoryChanged  = 7,   // requested inventory slots/amounts no longer match server state
			InvalidBundle     = 8,   // bundle empty, over cap, or has invalid/duplicate slots
			InvalidNote       = 9,   // seeking note too long or has disallowed characters
			NotSeller         = 10,  // actor is not the Listing's Seller (finalize/delist)
			NotNearAuctioneer = 11,  // actor is not within range of an Auctioneer NPC
			WarehouseFull     = 12,  // a recipient's Warehouse is at the hard item cap
			Busy              = 13,  // actor is mid-Exchange or otherwise unavailable
			Failed            = 14,  // generic failure
		};
	}

	HB_PACK_BEGIN

	//------------------------------------------------------------------------
	// Shared item payloads
	//------------------------------------------------------------------------

	// One item to escrow, identified by the sender's inventory slot only.
	// The server validates the slot and amount against live inventory.
	struct HB_PACKED TpEscrowSlot {
		uint8_t inv_slot;   // inventory slot index (0..MaxItems-1)
		int32_t amount;     // stack amount to escrow (full count for non-stackables)
	};

	// Compact item for browse rows: id/count plus the name-affecting instance
	// fields, so the client renders display names (prefix/secondary/enchant/
	// custom/dye) via item_name_formatter without the full payload.
	struct HB_PACKED TpItemBrief {
		int16_t item_id;
		uint64_t count;
		uint8_t item_color;
		uint8_t custom_made;
		uint8_t prefix_type;
		uint8_t prefix_value;
		uint8_t secondary_type;
		uint8_t secondary_value;
		uint8_t enchant_bonus;
	};

	// Full item instance columns (mirror character_bank_items /
	// item_instance_data) for detail views, letting the client rebuild complete
	// tooltips.
	struct HB_PACKED TpItemFull {
		int16_t item_id;
		uint64_t count;
		int16_t touch_effect_type;
		int16_t touch_effect_value1;
		int16_t touch_effect_value2;
		int16_t touch_effect_value3;
		uint8_t item_color;
		int16_t spec_effect_value1;
		int16_t spec_effect_value2;
		int16_t spec_effect_value3;
		uint16_t cur_durability;
		uint8_t custom_made;
		uint8_t prefix_type;
		uint8_t prefix_value;
		uint8_t secondary_type;
		uint8_t secondary_value;
		uint8_t enchant_bonus;
	};

	//------------------------------------------------------------------------
	// Requests (client -> server)
	//------------------------------------------------------------------------

	// Browse one page of the board (newest Listings first).
	struct HB_PACKED PacketRequestTpBoardPage : packet_base {
		PacketHeader header;
		uint16_t page;
	};

	// Browse one page of the actor's own slice of the board: the Listings they
	// sell, or the Listings they have an Offer on (see TpMyBoardFilter). The
	// answer is a normal PacketResponseTpBoardPage — no dedicated response type.
	struct HB_PACKED PacketRequestTpMyBoard : packet_base {
		PacketHeader header;
		uint16_t page;
		uint8_t which;    // TpMyBoardFilter
	};

	// Fetch full detail (bundle + Offers) for one Listing.
	struct HB_PACKED PacketRequestTpListingDetail : packet_base {
		PacketHeader header;
		int32_t listing_id;
	};

	// Create a Listing from 1..TpMaxListingItems inventory slots + seeking note.
	struct HB_PACKED PacketRequestTpCreateListing : packet_base {
		PacketHeader header;
		char note[hb::shared::limits::TpSeekingNoteLen];
		uint8_t item_count;                                      // 1..TpMaxListingItems
		TpEscrowSlot items[hb::shared::limits::TpMaxListingItems];
	};

	// Place an Offer of 1..TpMaxOfferItems inventory slots on a Listing.
	struct HB_PACKED PacketRequestTpPlaceOffer : packet_base {
		PacketHeader header;
		int32_t listing_id;
		uint8_t item_count;                                      // 1..TpMaxOfferItems
		TpEscrowSlot items[hb::shared::limits::TpMaxOfferItems];
	};

	// Rescind the actor's own Offer.
	struct HB_PACKED PacketRequestTpRescindOffer : packet_base {
		PacketHeader header;
		int32_t offer_id;
	};

	// Finalize (Seller only): accept one Offer on the actor's Listing.
	struct HB_PACKED PacketRequestTpFinalize : packet_base {
		PacketHeader header;
		int32_t listing_id;
		int32_t offer_id;
	};

	// Delist (Seller only): remove the actor's Listing, refunding all Offers.
	struct HB_PACKED PacketRequestTpDelist : packet_base {
		PacketHeader header;
		int32_t listing_id;
	};

	//------------------------------------------------------------------------
	// Responses (server -> client)
	//------------------------------------------------------------------------

	// One browse row: Listing summary + compact bundle preview.
	struct HB_PACKED TpBoardRow {
		int32_t listing_id;
		char seller[hb::shared::limits::CharNameLen];
		uint8_t nation;                                          // Seller's nation/side
		uint8_t offer_count;                                     // active Offers (0..TpMaxOffersPerListing)
		uint16_t expires_hours;                                  // hours until expiry
		char note[hb::shared::limits::TpSeekingNoteLen];         // seeking note
		uint8_t item_count;                                      // items in bundle (1..TpMaxListingItems)
		TpItemBrief items[hb::shared::limits::TpMaxListingItems];
	};

	// A page of the board. Only the first row_count rows are valid.
	struct HB_PACKED PacketResponseTpBoardPage {
		PacketHeader header;
		uint16_t page;                                           // this page index
		uint16_t page_count;                                     // total pages
		uint16_t row_count;                                      // valid rows (0..TpBoardPageRows)
		TpBoardRow rows[hb::shared::limits::TpBoardPageRows];
	};

	// One Offer as shown in the detail view (full item columns for tooltips).
	struct HB_PACKED TpOfferView {
		int32_t offer_id;
		char offerer[hb::shared::limits::CharNameLen];
		uint8_t item_count;                                      // items in Offer (1..TpMaxOfferItems)
		TpItemFull items[hb::shared::limits::TpMaxOfferItems];
	};

	// Full detail for one Listing: bundle + all active Offers. Only the first
	// item_count bundle items and offer_count offers are valid.
	struct HB_PACKED PacketResponseTpListingDetail {
		PacketHeader header;
		int32_t listing_id;
		char seller[hb::shared::limits::CharNameLen];
		uint8_t nation;                                          // Seller's nation/side
		char note[hb::shared::limits::TpSeekingNoteLen];         // seeking note
		uint16_t expires_hours;                                  // hours until expiry
		uint8_t item_count;                                      // items in bundle (1..TpMaxListingItems)
		TpItemFull items[hb::shared::limits::TpMaxListingItems];
		uint8_t offer_count;                                     // active Offers (0..TpMaxOffersPerListing)
		TpOfferView offers[hb::shared::limits::TpMaxOffersPerListing];
	};

	// Result of any mutating action (create/offer/rescind/finalize/delist) and
	// the error channel for browse/detail. action is a TpAction, result_code a
	// TpResultCode; value carries a new listing_id/offer_id on Ok where useful.
	struct HB_PACKED PacketResponseTpActionResult {
		PacketHeader header;
		uint16_t action;        // TpAction
		uint16_t result_code;   // TpResultCode
		int32_t value;          // action-specific payload (id/count), 0 when unused
	};

	HB_PACK_END

} // namespace net
} // namespace hb
