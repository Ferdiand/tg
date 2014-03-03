#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef USE_LUA
#include "lua-tg.h"

#include "include.h"
#include <string.h>
#include <stdlib.h>


#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
lua_State *luaState;

#include "structures.h"
#include "interface.h"
#include "constants.h"
#include "tools.h"
#include "queries.h"
#include "net.h"

extern int verbosity;

static int have_file;

#define my_lua_checkstack(L,x) assert (lua_checkstack (L, x))
void push_user (peer_t *P UU);
void push_peer (peer_id_t id, peer_t *P);

void lua_add_string_field (const char *name, const char *value) {
  assert (name && strlen (name));
  if (!value || !strlen (value)) { return; }
  my_lua_checkstack (luaState, 3);
  lua_pushstring (luaState, name);
  lua_pushstring (luaState, value);
  lua_settable (luaState, -3);
}

void lua_add_num_field (const char *name, double value) {
  assert (name && strlen (name));
  my_lua_checkstack (luaState, 3);
  lua_pushstring (luaState, name);
  lua_pushnumber (luaState, value);
  lua_settable (luaState, -3);
}

void push_peer_type (int x) {
  switch (x) {
  case PEER_USER:
    lua_pushstring (luaState, "user");
    break;
  case PEER_CHAT:
    lua_pushstring (luaState, "chat");
    break;
  case PEER_ENCR_CHAT:
    lua_pushstring (luaState, "encr_chat");
    break;
  default:
    assert (0);
  }
}

void push_user (peer_t *P UU) {
  my_lua_checkstack (luaState, 4);
  lua_add_string_field ("first_name", P->user.first_name);
  lua_add_string_field ("last_name", P->user.last_name);
  lua_add_string_field ("real_first_name", P->user.real_first_name);
  lua_add_string_field ("real_last_name", P->user.real_last_name);
  lua_add_string_field ("phone", P->user.phone);
}

void push_chat (peer_t *P) {
  my_lua_checkstack (luaState, 4);
  assert (P->chat.title);
  lua_add_string_field ("title", P->chat.title);
  lua_add_num_field ("members_num", P->chat.users_num);
}

void push_encr_chat (peer_t *P) {
  my_lua_checkstack (luaState, 4);
  lua_pushstring (luaState, "user");
  push_peer (MK_USER (P->encr_chat.user_id), user_chat_get (MK_USER (P->encr_chat.user_id)));
  lua_settable (luaState, -3);
}

void push_peer (peer_id_t id, peer_t *P) {
  lua_newtable (luaState);
 
  lua_add_num_field ("id", get_peer_id (id));
  lua_pushstring (luaState, "type");
  push_peer_type (get_peer_type (id));
  lua_settable (luaState, -3);


  if (!P || !(P->flags & FLAG_CREATED)) {
    lua_pushstring (luaState, "print_name"); 
    static char s[100];
    switch (get_peer_type (id)) {
    case PEER_USER:
      sprintf (s, "user#%d", get_peer_id (id));
      break;
    case PEER_CHAT:
      sprintf (s, "chat#%d", get_peer_id (id));
      break;
    case PEER_ENCR_CHAT:
      sprintf (s, "encr_chat#%d", get_peer_id (id));
      break;
    default:
      assert (0);
    }
    lua_pushstring (luaState, s); 
    lua_settable (luaState, -3); // flags
  
    return;
  }
  
  lua_add_string_field ("print_name", P->print_name);
  lua_add_num_field ("flags", P->flags);
  
  switch (get_peer_type (id)) {
  case PEER_USER:
    push_user (P);
    break;
  case PEER_CHAT:
    push_chat (P);
    break;
  case PEER_ENCR_CHAT:
    push_encr_chat (P);
    break;
  default:
    assert (0);
  }
}

void push_media (struct message_media *M) {
  my_lua_checkstack (luaState, 4);

  switch (M->type) {
  case CODE_message_media_photo:
  case CODE_decrypted_message_media_photo:
    lua_pushstring (luaState, "photo");
    break;
  case CODE_message_media_video:
  case CODE_decrypted_message_media_video:
    lua_pushstring (luaState, "video");
    break;
  case CODE_message_media_audio:
  case CODE_decrypted_message_media_audio:
    lua_pushstring (luaState, "audio");
    break;
  case CODE_message_media_document:
  case CODE_decrypted_message_media_document:
    lua_pushstring (luaState, "document");
    break;
  case CODE_message_media_unsupported:
    lua_pushstring (luaState, "unsupported");
    break;
  case CODE_message_media_geo:
    lua_newtable (luaState);
    lua_add_num_field ("longitude", M->geo.longitude);
    lua_add_num_field ("latitude", M->geo.latitude);
    break;
  case CODE_message_media_contact:
  case CODE_decrypted_message_media_contact:
    lua_newtable (luaState);
    lua_add_string_field ("phone", M->phone);
    lua_add_string_field ("first_name", M->first_name);
    lua_add_string_field ("last_name", M->last_name);
    lua_add_num_field ("user_id", M->user_id);
    break;
  default:
    lua_pushstring (luaState, "???");
  }
}

void push_message (struct message *M) {
  assert (M);
  my_lua_checkstack (luaState, 10);
  lua_newtable (luaState);

  static char s[30];
  tsnprintf (s, 30, "%lld", M->id);
  lua_add_string_field ("id", s);
  lua_add_num_field ("flags", M->flags);
  
  if (get_peer_type (M->fwd_from_id)) {
    lua_pushstring (luaState, "fwd_from");
    push_peer (M->fwd_from_id, user_chat_get (M->fwd_from_id));
    lua_settable (luaState, -3); // fwd_from

    lua_add_num_field ("fwd_date", M->fwd_date);
  }
  
  lua_pushstring (luaState, "from");
  push_peer (M->from_id, user_chat_get (M->from_id));
  lua_settable (luaState, -3); 
  
  lua_pushstring (luaState, "to");
  push_peer (M->to_id, user_chat_get (M->to_id));
  lua_settable (luaState, -3); 
  
  lua_pushstring (luaState, "out");
  lua_pushboolean (luaState, M->out);
  lua_settable (luaState, -3); 
  
  lua_pushstring (luaState, "unread");
  lua_pushboolean (luaState, M->unread);
  lua_settable (luaState, -3); 
  
  lua_pushstring (luaState, "date");
  lua_pushnumber (luaState, M->date);
  lua_settable (luaState, -3); 
  
  lua_pushstring (luaState, "service");
  lua_pushboolean (luaState, M->service);
  lua_settable (luaState, -3); 

  if (!M->service) {  
    if (M->message_len && M->message) {
      lua_pushstring (luaState, "text");
      lua_pushlstring (luaState, M->message, M->message_len);
      lua_settable (luaState, -3); 
