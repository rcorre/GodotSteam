#include "steam_multiplayer_peer.h"
#include "core/error/error_macros.h"
#include "core/string/print_string.h"

namespace {
const size_t MAX_MESSAGES_PER_POLL = 32;

String lobby_error_string(uint32_t resp) {
	switch (resp) {
		case EChatRoomEnterResponse::k_EChatRoomEnterResponseSuccess:
			return "Success";
		case EChatRoomEnterResponse::k_EChatRoomEnterResponseDoesntExist:
			return "Chat doesn't exist (probably closed)";
		case EChatRoomEnterResponse::k_EChatRoomEnterResponseNotAllowed:
			return "General Denied - You don't have the permissions needed to join the chat";
		case EChatRoomEnterResponse::k_EChatRoomEnterResponseFull:
			return "Chat room has reached its maximum size";
		case EChatRoomEnterResponse::k_EChatRoomEnterResponseError:
			return "Unexpected Error";
		case EChatRoomEnterResponse::k_EChatRoomEnterResponseBanned:
			return "You are banned from this chat room and may not join";
		case EChatRoomEnterResponse::k_EChatRoomEnterResponseLimited:
			return "Joining this chat is not allowed because you are a limited user (no value on account)";
		case EChatRoomEnterResponse::k_EChatRoomEnterResponseClanDisabled:
			return "Attempt to join a clan chat when the clan is locked or disabled";
		case EChatRoomEnterResponse::k_EChatRoomEnterResponseCommunityBan:
			return "Attempt to join a chat when the user has a community lock on their account";
		case EChatRoomEnterResponse::k_EChatRoomEnterResponseMemberBlockedYou:
			return "Join failed - some member in the chat has blocked you from joining";
		case EChatRoomEnterResponse::k_EChatRoomEnterResponseYouBlockedMember:
			return "Join failed - you have blocked some member already in the chat";
		case EChatRoomEnterResponse::k_EChatRoomEnterResponseRatelimitExceeded:
			return "Join failed - to many join attempts in a very short period of time";
		default:
			return "Unknown error " + itos(resp);
	}
};

// Allow easy logging of CSteamID
String operator+(const String &s, const CSteamID &id) {
	return s + uitos(id.ConvertToUint64());
}

} //namespace

uint32_t SteamMultiplayerPeer::HashSteamID::operator()(const CSteamID &id) const { return id.GetAccountID(); }

SteamMultiplayerPeer::SteamMultiplayerPeer() :
		callbackLobbyJoined(this, &SteamMultiplayerPeer::on_lobby_joined),
		callbackLobbyChatUpdate(this, &SteamMultiplayerPeer::on_lobby_chat_update),
		callbackNetworkMessagesSessionRequest(this, &SteamMultiplayerPeer::on_network_messages_session_request),
		callbackNetworkMessagesSessionFailed(this, &SteamMultiplayerPeer::on_network_messages_session_failed) {}

void SteamMultiplayerPeer::set_channel_count(uint32_t p_channel_count) {
	channel_count = p_channel_count;
}

void SteamMultiplayerPeer::join_lobby(uint64_t p_lobby_id) {
	ERR_FAIL_COND_MSG(SteamUser() == NULL, "join_lobby: SteamUser unavailable");
	ERR_FAIL_COND_MSG(SteamMatchmaking() == NULL, "join_lobby: SteamMatchmaking unavailable");
	lobby_id.SetFromUint64(p_lobby_id);
	connection_status = CONNECTION_CONNECTING;
	add_peer(SteamUser()->GetSteamID());
	SteamMatchmaking()->JoinLobby(lobby_id);
}

void SteamMultiplayerPeer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_channel_count", "count"), &SteamMultiplayerPeer::set_channel_count);
	ClassDB::bind_method(D_METHOD("join_lobby", "lobby_id"), &SteamMultiplayerPeer::join_lobby);
}

void SteamMultiplayerPeer::add_peer(CSteamID steam_id) {
	ERR_FAIL_COND_MSG(SteamUser() == NULL, "add_peer: SteamUser unavailable");
	ERR_FAIL_COND_MSG(SteamMatchmaking() == NULL, "add_peer: SteamMatchmaking unavailable");
	// The peer ID must fit in a uint31, see MultiplayerPeer::generate_unique_id
	int peer_id = steam_id == SteamMatchmaking()->GetLobbyOwner(lobby_id) ? TARGET_PEER_SERVER : (steam_id.GetAccountID() & 0x7FFFFFFF);
	peer_to_steam_id[peer_id] = steam_id;
	steam_to_peer_id[steam_id] = peer_id;
	print_verbose("Mapped steam ID " + steam_id + " to peer id " + itos(peer_id));
	if (steam_id != SteamUser()->GetSteamID()) {
		emit_signal("peer_connected", peer_id);
	}
}

void SteamMultiplayerPeer::remove_peer(CSteamID steam_id) {
	ERR_FAIL_COND_MSG(SteamUser() == NULL, "add_peer: SteamUser unavailable");
	// The peer ID to fit in the signed part of an int32 (i.e. a uint31)
	// See MultiplayerPeer::generate_unique_id
	auto peer_id = steam_to_peer_id.find(steam_id);
	if (peer_id == steam_to_peer_id.end()) {
		return;
	}
	peer_to_steam_id.erase(peer_id->second);
	steam_to_peer_id.erase(peer_id);
	print_verbose("Removed steam ID " + steam_id + ", peer id " + itos(peer_id->second));
	if (steam_id != SteamUser()->GetSteamID()) {
		emit_signal("peer_disconnected", peer_id->second);
	}
}

void SteamMultiplayerPeer::on_lobby_joined(LobbyEnter_t *lobbyData) {
	if (lobbyData->m_EChatRoomEnterResponse != EChatRoomEnterResponse::k_EChatRoomEnterResponseSuccess) {
		WARN_PRINT("Failed to join lobby " + uitos(lobbyData->m_ulSteamIDLobby) + ": " + lobby_error_string(lobbyData->m_EChatRoomEnterResponse));
		connection_status = CONNECTION_DISCONNECTED;
		return;
	}

	ERR_FAIL_COND_MSG(SteamMatchmaking() == NULL, "on_lobby_joined: SteamMatchmaking unavailable");

	lobby_id = lobbyData->m_ulSteamIDLobby;
	print_verbose("Joined lobby: " + lobby_id);
	connection_status = CONNECTION_CONNECTED;

	for (int i = 0; i < SteamMatchmaking()->GetNumLobbyMembers(lobby_id); ++i) {
		CSteamID steam_id = SteamMatchmaking()->GetLobbyMemberByIndex(lobby_id, i);
		add_peer(steam_id);
	}
}

void SteamMultiplayerPeer::on_lobby_chat_update(LobbyChatUpdate_t *call_data) {
	ERR_FAIL_COND_MSG(lobby_id != call_data->m_ulSteamIDLobby, "on_lobby_chat_update: not our lobby: " + uitos(call_data->m_ulSteamIDLobby));

	CSteamID steam_id = call_data->m_ulSteamIDUserChanged;
	switch (call_data->m_rgfChatMemberStateChange) {
		case k_EChatMemberStateChangeEntered:
			add_peer(steam_id);
			break;
		case k_EChatMemberStateChangeLeft:
		case k_EChatMemberStateChangeDisconnected:
		case k_EChatMemberStateChangeKicked:
		case k_EChatMemberStateChangeBanned:
			remove_peer(steam_id);
			break;
	}
}

void SteamMultiplayerPeer::on_network_messages_session_request(SteamNetworkingMessagesSessionRequest_t *call_data) {
	ERR_FAIL_COND_MSG(SteamNetworkingMessages() == NULL, "on_network_messages_session_request: SteamNetworkingMessages not available");
	SteamNetworkingIdentity remote = call_data->m_identityRemote;
	if (is_refusing_new_connections()) {
		print_verbose("Refusing connection with " + remote.GetSteamID());
	} else {
		print_verbose("Accepting connection with " + remote.GetSteamID());
		SteamNetworkingMessages()->AcceptSessionWithUser(remote);
	}
}

void SteamMultiplayerPeer::on_network_messages_session_failed(SteamNetworkingMessagesSessionFailed_t *call_data) {
	print_verbose("Messaging session failed: " + call_data->m_info.m_identityRemote.GetSteamID());
}

// PacketPeer Overrides
int SteamMultiplayerPeer::get_available_packet_count() const {
	return packets.size();
}

int SteamMultiplayerPeer::get_max_packet_size() const {
	return 1 << 24; // copied from ENetPacketPeer, dunno if steam has a limit
}

Error SteamMultiplayerPeer::get_packet(const uint8_t **r_buffer, int &r_buffer_size) {
	ERR_FAIL_COND_V_MSG(packets.is_empty(), ERR_UNAVAILABLE, "get_packet: no packets available");
	last_packet = *packets.begin();
	packets.pop_front();
	r_buffer_size = last_packet.data.size();
	*r_buffer = last_packet.data.ptr();
	return OK;
}

Error SteamMultiplayerPeer::put_packet(const uint8_t *p_buffer, int p_buffer_size) {
	ERR_FAIL_COND_V_MSG(SteamNetworkingMessages() == NULL, ERR_UNAVAILABLE, "put_packet: SteamNetworkingMessages unavailable");
	auto steam_id = peer_to_steam_id.find(target_peer);
	ERR_FAIL_COND_V_MSG(steam_id == peer_to_steam_id.end(), ERR_UNAVAILABLE, "put_packet: missing id mapping");
	SteamNetworkingIdentity id;
	id.SetSteamID(steam_id->second);
	int flags = k_nSteamNetworkingSend_AutoRestartBrokenSession | (get_transfer_mode() == TRANSFER_MODE_RELIABLE ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable);

	return SteamNetworkingMessages()->SendMessageToUser(id, p_buffer, p_buffer_size, flags, get_transfer_channel()) ? OK : FAILED;
}

void SteamMultiplayerPeer::set_target_peer(int p_peer_id) {
	target_peer = p_peer_id;
}

int SteamMultiplayerPeer::get_packet_peer() const {
	return packets.begin()->peer_id;
}

MultiplayerPeer::TransferMode SteamMultiplayerPeer::get_packet_mode() const {
	return packets.begin()->reliable ? TransferMode::TRANSFER_MODE_RELIABLE : TransferMode::TRANSFER_MODE_UNRELIABLE;
}

int SteamMultiplayerPeer::get_packet_channel() const {
	return packets.begin()->channel;
}

void SteamMultiplayerPeer::disconnect_peer(int p_peer, bool p_force) {
	ERR_FAIL_COND_MSG(SteamNetworkingMessages() == NULL, "disconnect_peer: SteamNetworkingMessages unavailable");

	auto steam_id = peer_to_steam_id.find(p_peer);
	ERR_FAIL_COND_MSG(steam_id == peer_to_steam_id.end(), "disconnect_peer: peer not found: " + itos(p_peer));

	SteamNetworkingIdentity id;
	id.SetSteamID(steam_id->second);
	SteamNetworkingMessages()->CloseSessionWithUser(id);

	if (p_force) {
		peer_to_steam_id.erase(p_peer);
		steam_to_peer_id.erase(steam_id->second);
		print_verbose("Removed steam ID " + steam_id->second + ", peer id " + itos(p_peer));
	}
}

bool SteamMultiplayerPeer::is_server() const {
	ERR_FAIL_COND_V_MSG(SteamUser() == NULL, 0, "is_server: SteamUser unavailable");
	ERR_FAIL_COND_V_MSG(SteamMatchmaking() == NULL, 0, "is_server: SteamMatchmaking unavailable");
	// Assume lobby owner is the server
	return SteamUser()->GetSteamID() == SteamMatchmaking()->GetLobbyOwner(lobby_id);
}

void SteamMultiplayerPeer::poll() {
	ERR_FAIL_COND_MSG(SteamNetworkingMessages() == NULL, "poll: SteamNetworkingMessages unavailable");

	for (uint32_t channel = 0; channel < channel_count; ++channel) {
		SteamNetworkingMessage_t *messages[MAX_MESSAGES_PER_POLL];
		int count = SteamNetworkingMessages()->ReceiveMessagesOnChannel(channel, messages, MAX_MESSAGES_PER_POLL);
		for (int i = 0; i < count; ++i) {
			const SteamNetworkingMessage_t &msg = *messages[i];
			auto it = steam_to_peer_id.find(msg.m_identityPeer.GetSteamID());
			if (it == steam_to_peer_id.end()) {
				WARN_PRINT(String("Message from unknown steam ID ") + msg.m_identityPeer.GetSteamID());
				messages[i]->Release();
				continue;
			}
			Packet packet;
			packet.data.resize(msg.m_cbSize);
			memcpy(packet.data.ptrw(), msg.m_pData, msg.m_cbSize);
			packet.reliable = msg.m_nFlags & k_nSteamNetworkingSend_Reliable;
			packet.peer_id = it->second;
			packet.channel = msg.m_nChannel;
			packets.push_back(packet);
			messages[i]->Release();
		}
	}
}

void SteamMultiplayerPeer::close() {
	ERR_FAIL_COND_MSG(SteamMatchmaking() == NULL, "is_server: SteamMatchmaking unavailable");
	SteamMatchmaking()->LeaveLobby(lobby_id);
	connection_status = CONNECTION_DISCONNECTED;
}

int SteamMultiplayerPeer::get_unique_id() const {
	ERR_FAIL_COND_V_MSG(SteamUser() == NULL, 0, "get_unique_id: SteamUser unavailable");
	const auto peer_id = steam_to_peer_id.find(SteamUser()->GetSteamID());
	ERR_FAIL_COND_V_MSG(peer_id == steam_to_peer_id.end(), 0, "get_unique_id: ID not mapped");
	return peer_id->second;
}

MultiplayerPeer::ConnectionStatus SteamMultiplayerPeer::get_connection_status() const {
	return connection_status;
}
