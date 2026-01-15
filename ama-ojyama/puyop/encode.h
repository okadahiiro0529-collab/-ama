#pragma once

#include <iostream>
#include <string>
#include <vector>
#include "../ai/ai.h"

namespace encode
{

constexpr char CHAR[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ[]";

// Maps cell::Type to code used for encoding control pairs.
// Note: we intentionally map GARBAGE to 4 so control can contain garbage pairs.
// NONE is not normally encoded in control; if encountered we will fallback to GARBAGE.
constexpr int get_cell_id(cell::Type cell)
{
    switch (cell)
    {
    case cell::Type::RED:
        return 0;
    case cell::Type::GREEN:
        return 1;
    case cell::Type::BLUE:
        return 2;
    case cell::Type::YELLOW:
        return 3;
    case cell::Type::GARBAGE:
        return 4;
    default:
        // Fallback for safety (e.g. NONE) -> treat as garbage in control
        return 4;
    }
};

// Maps cell::Type to code used for field encoding (different mapping from control)
constexpr int get_field_cell_id(cell::Type cell)
{
    switch (cell)
    {
    case cell::Type::NONE:
        return 0;
    case cell::Type::RED:
        return 1;
    case cell::Type::GREEN:
        return 2;
    case cell::Type::BLUE:
        return 3;
    case cell::Type::YELLOW:
        return 4;
    case cell::Type::GARBAGE:
        return 6;
    default:
        return 0;
    }
}

// --- 置き換え用: encode::get_encoded_control ---
inline std::string get_encoded_control(const std::vector<cell::Pair>& queue, const std::vector<move::Placement>& placements)
{
    std::string result;

    size_t n = placements.size();
    size_t i = 0;

    while (i < n) {
        auto pair = queue[i];
        auto plc  = placements[i];

        // Garbage pair handling
        if (pair.first == cell::Type::GARBAGE && pair.second == cell::Type::GARBAGE) {
            int r = static_cast<int>(plc.r);

            // Case A: single entry that encodes a contiguous cluster using plc.r > 1
            if (r > 1) {
                int start = static_cast<int>(plc.x);
                int k = r;
                if (start < 0) start = 0;
                if (start > 5) start = 5;
                if (k < 1) k = 1;
                if (k > 6) k = 6;
                if (start + k > 6) k = 6 - start;

                int mask = 0;
                for (int j = 0; j < k; ++j) mask |= (1 << (start + j));
                mask &= 0x3F;

                result.push_back(CHAR[mask]);
                result.push_back(CHAR[56]); // 'U'
                ++i;
                continue;
            }

            // Case B: a run of consecutive single-column garbage entries.
            // We must preserve multiplicity: count occurrences per column in the consecutive run,
            // then emit mask tokens repeatedly while any count > 0.
            // Find the consecutive run [i, j)
            size_t j = i;
            int counts[6] = {0,0,0,0,0,0};
            for (; j < n; ++j) {
                auto qj = queue[j];
                if (!(qj.first == cell::Type::GARBAGE && qj.second == cell::Type::GARBAGE)) break;
                int col = static_cast<int>(placements[j].x);
                if (col < 0) col = 0;
                if (col > 5) col = 5;
                counts[col] += 1;
            }

            // Emit mask tokens until all counts are zero.
            while (true) {
                int mask = 0;
                int maxcnt = 0;
                for (int c = 0; c < 6; ++c) {
                    if (counts[c] > 0) {
                        mask |= (1 << c);
                        if (counts[c] > maxcnt) maxcnt = counts[c];
                    }
                }
                if (mask == 0) break; // nothing left
                result.push_back(CHAR[mask & 0x3F]);
                result.push_back(CHAR[56]); // 'U'
                // decrement counts (we simulate one "round" of drops to all columns indicated)
                for (int c = 0; c < 6; ++c) {
                    if (counts[c] > 0) counts[c]--;
                }
            }

            // advance i to j (we consumed the whole consecutive garbage run)
            i = j;
            continue;
        }

        // Safety fallback: replace NONE with GARBAGE for encoding if it ever appears
        if (pair.first == cell::Type::NONE || pair.second == cell::Type::NONE) {
            pair.first = cell::Type::GARBAGE;
            pair.second = cell::Type::GARBAGE;
        }

        // Normal pair encoding (unchanged)
        i32 pair_code = get_cell_id(pair.first) * 5 + get_cell_id(pair.second);
        i32 placement_code = (i32(plc.x + 1) << 2) + static_cast<i32>(plc.r);
        i32 code = pair_code | (placement_code << 7);

        result.push_back(CHAR[code & 0x3F]);
        result.push_back(CHAR[(code >> 6) & 0x3F]);

        ++i;
    }

    return result;
}

// get_encoded_field / get_encoded_URL は以前どおり
inline std::string get_encoded_field(Field field)
{
    if (field.is_empty()) {
        return "";
    }

    std::string result;
    bool start = false;

    for (i8 y = 13; y > 0; --y) {
        for (i8 x = 0; x <= 4; x += 2) {
            if (!start && field.get_cell(x, y) == cell::Type::NONE && field.get_cell(x + 1, y) == cell::Type::NONE) {
                continue;
            }

            i32 code = get_field_cell_id(field.get_cell(x, y)) * 8 + get_field_cell_id(field.get_cell(x + 1, y));
            start = true;
            result.push_back(CHAR[code]);
        }
    }

    return result;
}

inline std::string get_encoded_URL(Field field, std::vector<cell::Pair> queue, std::vector<move::Placement> placements)
{
    return std::string("http://www.puyop.com/s/") + get_encoded_field(field) + "_" + get_encoded_control(queue, placements);
}

} // namespace encode