#pragma once

#include "arranger/Style.h"

namespace arr::detail {

// Transposition used when a file carries no rule for a part (Yamaha style without CASM, Korg tracks).
inline TransposeRule defaultRule(PartRole role)
{
    TransposeRule rule;
    TransposeRange& r = rule.mid;
    switch (role)
    {
        case PartRole::SubRhythm:
        case PartRole::Rhythm:
            r.ntr = Ntr::RootFixed;
            r.ntt = Ntt::Bypass;
            r.rtr = Rtr::Stop;
            break;
        case PartRole::Bass:
            r.ntt = Ntt::Melody;
            r.bassOn = true;
            break;
        case PartRole::Chord1:
        case PartRole::Chord2:
        case PartRole::Pad:
            r.ntt = Ntt::Chord;
            break;
        case PartRole::Phrase1:
        case PartRole::Phrase2:
            r.ntt = Ntt::Melody;
            break;
    }
    return rule;
}

} // namespace arr::detail
