#pragma once

#define TAQUITO_VERSION "1.0.1.1"
#define EV_TAQUITO_PATHING_STATE "EV_TAQUITO_PATHING_STATE"
#define EV_TAQUITO_PATHING_REQUEST "EV_TAQUITO_PATHING_REQUEST"

// STATE: void* -> NUL-terminated UTF-8 JSON, schema 1. Copy during the
// synchronous Events_Raise callback; never retain the payload pointer.
// REQUEST: nullptr. Replies retain the revision unless content has changed.
// Sharing is automatic while loaded. active=false signals unloading; consumers
// clear Taquito output. See PIEUI_PATHING_STATE_API.md.
