// GuildManager.h: the guild policy engine (#121)
//
// The store (#120, GuildSqliteStore) owns what a guilds.db row set may BE;
// this engine owns what a player may DO to it. Every §3.1/§3.1.1 lifecycle
// ceremony enters here — create, disband, the queued join/leave ticket flows,
// kick, promote/demote, transfer-mastership, Kennedy self-dismissal — and
// every entry point runs its full gate ladder before the store is touched,
// returning a named guild_result the caller (a chat command, an admin surface,
// the guildcheck prover, and from #123 the wire handlers) can act on.
//
// Permission checks are bitmask checks, everywhere (ADR 0006): no gate here
// ever asks "is this member rank X?" — it asks has_permission(). The only
// rank comparisons in the file are the two the ADR carves out (kick targets
// strictly below the actor's rank; transfer/disband are the master's alone)
// plus the master-only refusals that keep "the master is always a member"
// true.
//
// The engine also owns the two pieces of live state the original faked with
// 2000-slot scans:
//
//   - CClient hydration: guild membership lives ONLY in guilds.db; the guild
//     fields on CClient are a login-time cache of one member_of() read,
//     refreshed by every mutation that touches that member. Nothing is read
//     from or written to character rows.
//   - The membership index: guild guid -> online client handles, maintained
//     on login/logout and on every membership mutation. online_members() is
//     what guild chat (#125), the disband sweep and the Phase-3 commander
//     routing consume instead of walking every client slot. (One lookup
//     still needs a name scan: syncing a just-approved joiner, who is by
//     definition in no bucket yet — decide_request pays it once per
//     approved join.)
//
// Since #123 the engine also owns the guild notify family (PacketGuild.h):
// every mutation broadcasts its event to the online bucket through the typed
// send path, and refresh_client/clear_client re-sync the subject's own state
// (PacketNotifyGuildSelf) — send_notify_msg still grows no arms. The wire
// handlers (Handlers/NetworkMessages_Guild.cpp) send only request/response
// envelopes; events stay here so chat commands, admin surfaces and the
// inactivity sweep announce exactly like wire requests.
//
// Time is stamped here (hb::time::unix_now) and passed down; the store never
// reads a clock (#120's determinism rule).
//
// Design contract: PLANS/CoreSystems_Restoration_Plan.md §3.1/§3.1.1;
// ADR 0006. Original semantics preserved per the reference survey: creation
// gates and their order, ticket ceremonies (consumed on filing), the −300
// clamp, the master-only CHR floor, crusade gates on create/disband/secession
// but not on joins — with the original's mechanical defects (unguarded
// handlers, tickets burned on refused ceremonies, name-keyed broadcasts)
// fixed by construction.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Game/GuildDefs.h"

namespace hb::net { struct PacketGuildSelfState; }

class CGame;
class CClient;

namespace hb::server
{
	class guild_sqlite_store;
	struct guild_level_row;
	struct guild_record;

	// Why an entry point refused (or didn't) — one name per gate. The
	// vocabulary moved to GuildDefs.h with #123 (the action envelope carries
	// it to the client); aliased so engine callers keep their
	// hb::server::guild_result spelling.
	namespace guild_result = hb::shared::guild::guild_result;
	using hb::shared::guild::guild_result_name;

	class guild_manager
	{
	public:
		explicit guild_manager(CGame* game) : m_game(game) {}

		guild_manager(const guild_manager&) = delete;
		guild_manager& operator=(const guild_manager&) = delete;

		// --- Lifecycle -------------------------------------------------------
		// Create: level/charisma floors, citizen standing in the home town
		// (m_location against the map's location name, the original's exact
		// test), free of any guild, outside crusade, name legal and untaken.
		// The creator becomes Guildmaster and is hydrated on the spot.
		int create_guild(int client_h, const char* name);

		// Disband: master only (hardcoded), typed-name confirmation
		// (case-insensitive, like every name here), outside crusade. The
		// Treasury remainder is granted to the master (inventory, ground at
		// their feet when full) BEFORE the store cascades everything away;
		// every online member is cleared and unindexed inline — the "broadcast"
		// is a guild.log line until #123 gives it a wire shape.
		int disband_guild(int client_h, const char* typed_name);

		// --- The queued join/leave ceremony (§3.1.1 deviation 2) -------------
		// Filing queues the request and consumes the ticket (88 join, 89
		// leave) in that order — consumed on FILING, not on decision, which is
		// the original's "consumed either way" without its burned-for-nothing
		// defect: a refused filing costs nothing. The hand-to-approver path
		// and the Kennedy by-guild-name path both land in file_join_request;
		// out_request_id names the queued row for decide_request.
		int file_join_request(int client_h, const char* guild_name,
			int64_t& out_request_id);

		// Leave is filed against the member's own guild. The master cannot
		// file one (transfer or disband instead). Crusade-gated like the
		// original's secession ceremony.
		int file_leave_request(int client_h, int64_t& out_request_id);

		// Any online member holding the matching permission bit decides
		// (approve_join / approve_leave — "any approver", §3.1.1). Approving
		// a join adds at Guildsman and retires every other application the
		// character had queued (store rule); approving a leave removes the
		// member. Rejection just deletes the row — the ticket was the filing
		// fee either way. The affected character is re-hydrated when online.
		int decide_request(int approver_h, int64_t request_id, bool approve);

		// Kennedy self-dismissal (§3.1: rank 12; §3.1.1: Officers too, via
		// auto-demote on the way out; the master never). Costs a Secession
		// Ticket plus kennedy_secession_exp_penalty exp, clamped at zero.
		// Outside crusade only.
		int kennedy_self_exit(int client_h);

		// --- Moderation ------------------------------------------------------
		// Kick: the kick bit, target strictly below the actor's rank (which
		// also makes self-kick and master-kick impossible by arithmetic).
		// Works on offline members — the store row is the target, any online
		// CClient is synced after. /banguild routes here.
		int kick_member(int actor_h, const char* target_name);

		// Promote guildsman -> officer / demote officer -> guildsman, under
		// the promote_demote bit. The master's rank moves only via transfer.
		int promote_member(int actor_h, const char* target_name);
		int demote_member(int actor_h, const char* target_name);

		// Succession (§3.1.1 deviation 1): master only, hardcoded. The
		// incoming master must be an online member of the same guild with
		// charisma at or above the master floor — the floor is meaningless if
		// succession can install a master already below it. The old master
		// becomes Officer (the store's transaction).
		int transfer_mastership(int master_h, const char* new_master_name);

		// --- Progression (#122, §3.1.2) --------------------------------------
		// The three Donation lanes. One gate ladder each: donations_enabled ->
		// membership -> minimum -> the donor's own balance; then the burn
		// decrements the donor's live counter (gold leaves as a Counted
		// GuildDonation outflow), record_donation persists lifetime + guild
		// counters + the level recomputed against the curve in one
		// transaction, and a level-up walks the online bucket so every cached
		// m_guild_level follows. Burn first, record second: a crash between
		// the two loses a few points and never mints any.
		int donate_gold(int client_h, int64_t amount);
		int donate_enemy_kills(int client_h, int64_t amount);
		int donate_contribution(int client_h, int64_t amount);

		// --- Titles (ADR 0007, §3.1.2 item 3) --------------------------------
		// Claim is first-come-first-served inside the slot counts the curve
		// grants at the guild's level (Commander pinned at 1, and gated on
		// the commander_eligible bit); one Title per member is schema fact.
		// Claiming ships always-on — only the bonuses read a switch.
		int claim_title(int client_h, int kind);

		// Voluntary drop of one's own Title.
		int drop_title(int client_h);

		// Strip a member's Title under the title_manage bit (§3.1.1: Officer+
		// re-assign; the freed slot is claimable the same tick).
		int strip_title(int actor_h, const char* target_name);

		// The duty-slot sweep, from on_timer every ~10s: an online holder is
		// touched while active and released once idle past the knob; an
		// offline holder's clock is last_seen_at — logout and disconnect ARE
		// inactivity (§3.1.2), they just do not shortcut the timer.
		void tick_titles(uint32_t now_ms);

		// --- Treasury (§3.1.1 deviation 6) -----------------------------------
		// Deposit is any member moving own gold in; withdraw is gated on the
		// treasury_withdraw bit and always logged by the store. Neither
		// touches progression (the altar/wallet firewall).
		int treasury_deposit(int client_h, int64_t amount);
		int treasury_withdraw(int client_h, int64_t amount);

		// --- Login / logout hooks (#121's CClient hydration) -----------------
		// Read member_of() once and fill the CClient guild cache + the index.
		// Unguilded characters get the cleared shape. Called from
		// init_player_data after the character row is loaded.
		void hydrate_login(int client_h);

		// Drop the handle from the index. Idempotent — pre-hydration
		// disconnects simply find nothing to drop. Called from delete_client
		// while the CClient is still alive.
		void on_logout(int client_h);

		// --- Queries ---------------------------------------------------------
		// The online handles of one guild, unordered. The empty vector for a
		// guild with nobody on. This is the index that replaces the
		// original's 2000-slot scans.
		const std::vector<int>& online_members(int64_t guild_guid) const;

		// Whether this client's hydrated rank holds the bit (ADR 0006). False
		// for the unguilded and for bad handles.
		bool has_permission(int client_h, uint32_t permission_bit) const;

		// The charisma floor state_change_handler must hold this client to:
		// guildmaster_charisma_floor for a hydrated Guildmaster, 0 otherwise
		// (the universal stat floor applies regardless).
		int min_charisma(int client_h) const;

		// The open store, or nullptr — the one availability rule, shared with
		// the wire handlers' read-only queries (#123) so no second copy of it
		// can drift.
		guild_sqlite_store* store() const;

		// The rank title every send site speaks through (#123): the ADR 0006
		// defaults today; the configurable-rank editor reads per-guild rows
		// through this same door tomorrow, so no send site needs finding then.
		const char* rank_title_for(int64_t guild_guid, int rank) const;

		// The own-guild snapshot from the hydrated cache — the ONE body under
		// the char-init contents and the GuildSelf re-sync.
		void fill_self_state(int client_h,
			hb::net::PacketGuildSelfState& out) const;

	private:

		// The acting-player lookup: refuses clients that are not fully in the
		// world, which is the right gate for every ceremony entry point.
		CClient* client_at(int client_h) const;

		// The hook lookup: hydrate_login runs BEFORE m_is_init_complete flips
		// (the #123 char-init contents will need the cache filled by then)
		// and on_logout runs during teardown, so the hooks and the cache
		// writers take any live CClient.
		CClient* client_raw(int client_h) const;

		// True when the client stands in the town of their citizenship — the
		// original's create gate: m_location equals the current map's
		// location name.
		bool is_in_own_town(const CClient& client) const;

		// Both halves of "what does this guild's level grant?": the guilds
		// row into `out`, the curve row returned. nullptr on either failure —
		// boot and reload validation keep every earned level inside the
		// curve, so a miss is drift, not a full guild; every caller maps it
		// to store_error.
		const hb::server::guild_level_row* curve_row(int64_t guid,
			guild_record& out) const;

		// Re-read membership from the store into the CClient cache and fix the
		// index bucket (add, move or drop). The one body every mutation and
		// hydrate_login share, so the cache cannot drift from the store.
		void refresh_client(int client_h);

		// The cleared cache shape, plus the index drop.
		void clear_client(int client_h);

		void index_add(int64_t guild_guid, int client_h);
		void index_remove(int64_t guild_guid, int client_h);

		// The online handle of `char_name` inside one guild's index bucket, or
		// -1. Bucket-sized, not server-sized — this is why moderation never
		// scans the client list.
		int find_online_member(int64_t guild_guid, const char* char_name) const;

		// The one body under promote_member and demote_member: the ladders are
		// token-identical, only (from, to, verb) move — the decide_request
		// treatment applied to the other symmetric pair. The promote direction
		// carries the officer-capacity rung (#122); any move releases a held
		// Commander whose new mask lost the eligibility bit.
		int move_rank(int actor_h, const char* target_name, int from_rank,
			int to_rank, const char* verb);

		// The one body under the three Donation lanes (#122): the ladder is
		// lane-independent, only the minimum knob, the balance read and the
		// burn write differ.
		enum class donation_lane { gold, enemy_kills, contribution };
		int donate(int client_h, donation_lane lane, int64_t amount);

		// --- The #123 notify layer ---------------------------------------
		// Events are sent from the mutations themselves — never from the wire
		// handlers — so a chat command, an admin surface and the inactivity
		// sweep announce exactly like a wire request does.
		//
		// The one PacketNotifyGuildSelf send site: every path that changes a
		// client's own cached guild state funnels through refresh_client or
		// clear_client, so those two call this and no mutation can forget to
		// re-sync its subject. Quiet before m_is_init_complete — the login
		// snapshot rides the char-init contents instead. (One deliberate
		// exception: donate's level-up walk writes m_guild_level directly and
		// announces with the GuildLevelUp broadcast instead of N Selfs.)
		void send_self_state(int client_h) const;

		// One queued-request notice to every online member holding the
		// request kind's deciding permission bit.
		void notify_request_queued(int64_t guild_guid, int kind,
			const char* applicant) const;

		// The three event shapes more than one mutation announces, so a new
		// site is one call, not a re-transcribed build-and-broadcast block.
		// Departures take a pre-captured audience (the mutation empties the
		// bucket underfoot); the other two read the live bucket.
		void broadcast_member_left(const std::vector<int>& audience,
			const char* member, int reason) const;
		void broadcast_rank_change(int64_t guild_guid, const char* member,
			int new_rank) const;
		void broadcast_title_change(int64_t guild_guid, const char* member,
			int kind, bool held, int reason) const;

		// Consume one ticket through item_deplete_handler — the shared
		// consuming-action path, so the ledger and the client-side erase
		// behave like every other consumable.
		void consume_ticket(int client_h, int slot);

		CGame* m_game = nullptr;

		// guid -> online handles. Vectors because buckets are guild-sized
		// (tens), churn is login/logout, and iteration is the hot use.
		std::unordered_map<int64_t, std::vector<int>> m_online_by_guild;
	};
}
