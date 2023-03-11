#ifndef STEAM_MULTIPLAYER_PEER_H
#define STEAM_MULTIPLAYER_PEER_H

/////////////////////////////////////////////////
// SILENCE STEAMWORKS WARNINGS
/////////////////////////////////////////////////
//
// Turn off MSVC-only warning about strcpy
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1
#pragma warning(disable : 4996)
#pragma warning(disable : 4828)
#endif

/////////////////////////////////////////////////
// INCLUDE HEADERS
/////////////////////////////////////////////////

#include <inttypes.h>
#include <unordered_map>

// Include Steamworks API headers
#include "steam/steam_api.h"

// Include Godot headers
#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"
#include "scene/main/multiplayer_peer.h"

class SteamMultiplayerPeer : public MultiplayerPeer {
	GDCLASS(SteamMultiplayerPeer, MultiplayerPeer);

private:
	struct Packet {
		bool reliable;
		int peer_id;
		int channel;
		PackedByteArray data;
	};

	struct HashSteamID {
		uint32_t operator()(const CSteamID &id) const;
	};

	void add_peer(CSteamID);
	void remove_peer(CSteamID);
	void on_lobby_joined(LobbyEnter_t *);
	void on_lobby_chat_update(LobbyChatUpdate_t *);
	void on_network_messages_session_request(SteamNetworkingMessagesSessionRequest_t *);
	void on_network_messages_session_failed(SteamNetworkingMessagesSessionFailed_t *);

	uint32_t channel_count{ 1 };
	CSteamID lobby_id;
	int target_peer;
	ConnectionStatus connection_status;

	// last packet retrieved with get_packet
	// we're responsible for keeping the buffer until the next get_packet
	Packet last_packet;
	List<Packet> packets;

	std::unordered_map<int, CSteamID> peer_to_steam_id;
	std::unordered_map<CSteamID, int, HashSteamID> steam_to_peer_id;

	STEAM_CALLBACK(SteamMultiplayerPeer, lobby_joined, LobbyEnter_t, callbackLobbyJoined);
	STEAM_CALLBACK(SteamMultiplayerPeer, lobby_chat_update, LobbyChatUpdate_t, callbackLobbyChatUpdate);
	STEAM_CALLBACK(SteamMultiplayerPeer, network_messages_session_request, SteamNetworkingMessagesSessionRequest_t, callbackNetworkMessagesSessionRequest);
	STEAM_CALLBACK(SteamMultiplayerPeer, network_messages_session_failed, SteamNetworkingMessagesSessionFailed_t, callbackNetworkMessagesSessionFailed);

protected:
	static void _bind_methods();

public:
	void set_channel_count(uint32_t p_channel_count);
	void join_lobby(uint64_t p_lobby_id);

	// PacketPeer implementation
	int get_available_packet_count() const override;
	int get_max_packet_size() const override;

	Error get_packet(const uint8_t **r_buffer, int &r_buffer_size) override;
	Error put_packet(const uint8_t *p_buffer, int p_buffer_size) override;

	// MultiplayerPeer implementation
	virtual void set_target_peer(int p_peer_id) override;

	virtual int get_packet_peer() const override;
	virtual TransferMode get_packet_mode() const override;
	virtual int get_packet_channel() const override;

	virtual void disconnect_peer(int p_peer, bool p_force = false) override;

	virtual bool is_server() const override;

	virtual void poll() override;
	virtual void close() override;

	virtual int get_unique_id() const override;

	virtual ConnectionStatus get_connection_status() const override;

	SteamMultiplayerPeer();
};
#endif // STEAM_MULTIPLAYER_PEER_H
