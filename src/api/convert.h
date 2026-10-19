#pragma once

#include <string>

#include "adsb/api/v1/api.pb.h"
#include "decode/aircraft.h"
#include "decode/frame_decode.h"
#include "dsp/spectrum.h"

/**
 * @file
 * The UI API's messages (proto/adsb/api/v1/api.proto), and conversions to them from the decoder's and DSP's
 * types. doc/design/ui_api.md describes the API.
 */

namespace api = adsb::api::v1;

/** Every field of `s` that the UI shows, with each unset optional left unset. */
api::Aircraft to_proto(const AircraftState& s);

/**
 * A validated frame, as the UI shows it.
 * @param summary frame_summary(), which needs the sink's AircraftTracker; empty for none
 */
api::Frame to_proto(const DecodedFrameView& v, std::string summary);

/** One spectrum row. */
api::SpectrumRow to_proto(const SpectrumFrame& s);
