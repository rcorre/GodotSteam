#include "register_types.h"
#include "core/object/class_db.h"
#include "core/config/engine.h"
#include "godotsteam.h"
#include "steam_multiplayer_peer.h"

static Steam* SteamPtr = NULL;

void initialize_godotsteam_module(ModuleInitializationLevel level){
	if(level == MODULE_INITIALIZATION_LEVEL_SERVERS){
		ClassDB::register_class<Steam>();
		ClassDB::register_class<SteamMultiplayerPeer>();
		SteamPtr = memnew(Steam);
		Engine::get_singleton()->add_singleton(Engine::Singleton("Steam", Steam::get_singleton()));
	}
}

void uninitialize_godotsteam_module(ModuleInitializationLevel level){
	if(level == MODULE_INITIALIZATION_LEVEL_SERVERS){
		memdelete(SteamPtr);
	}
}
