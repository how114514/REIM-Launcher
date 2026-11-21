/*
 * Pinned UE Native Adapter identity (M2-E3/E6).
 *
 * The UE runtime is the pinned "adapter". The Converter records this identity
 * in ue_adapter.json (converter/adapters/ue/generate_ue_adapter.py reads the
 * pin file and checks these defines), and the Bridge refuses a NativeVersion
 * whose adapter id/version/UE/ABI/render level does not match this build.
 *
 * Changing any value here is an adapter version change and requires a new
 * adapter_pin.json plus a regenerated ue_adapter.json.
 */
#pragma once

#define REIM_ADAPTER_ID "reim-ue-adapter"
#define REIM_ADAPTER_VERSION "1.0.0-r0"
#define REIM_ADAPTER_UE_VERSION "5.8.4"
#define REIM_ADAPTER_ABI_VERSION 2u
#define REIM_ADAPTER_RENDER_LEVEL 0u
