#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <string>
#include <sstream>
#include <random>
#include <algorithm>
#include <array>
#include "../ai/ai.h"
#include "encode.h"

void load_json(beam::eval::Weight& h)
{
    std::ifstream file;
    file.open("config.json");
    json js;
    file >> js;
    file.close();
    from_json(js, h);
}

void save_json()
{
    std::ifstream f("config.json");
    if (f.good()) {
        return;
    }
    f.close();

    std::ofstream o("config.json");
    json js;
    to_json(js, beam::eval::Weight());
    o << std::setw(4) << js << std::endl;
    o.close();
}

struct ScheduledGarbage {
    int due_move; // when to drop (loop index i)
    int count;    // how many garbage puyos to drop at once (cluster)
};

static bool has_conflict(const std::vector<ScheduledGarbage>& schedule, int due) {
    // Conflict if any scheduled event at due or adjacent (due-1, due+1)
    for (const auto &s : schedule) {
        if (s.due_move == due) return true;
        if (s.due_move == due - 1) return true;
        if (s.due_move == due + 1) return true;
    }
    return false;
}

// Helper: compute heights for all 6 columns and return array
static std::array<int,6> get_all_heights(Field& field) {
    std::array<int,6> h;
    for (int x = 0; x < 6; ++x) {
        h[x] = field.get_height(x);
    }
    return h;
}

// Map internal cell::Type to integers as you specified:
// NONE=0 RED=1 GREEN=2 BLUE=3 YELLOW=4 GARBAGE=6
static int cell_type_to_int(cell::Type t) {
    switch (t) {
    case cell::Type::NONE:    return 0;
    case cell::Type::RED:     return 1;
    case cell::Type::GREEN:   return 2;
    case cell::Type::BLUE:    return 3;
    case cell::Type::YELLOW:  return 4;
    case cell::Type::GARBAGE: return 6;
    default:                  return 0;
    }
}

// Print the entire field as a 14x6 matrix (top row first).
// (Kept only for mismatch debugging; calls removed from normal flow)
/*static void print_field_matrix(Field& field, const std::string& label) {
    printf("FIELD DUMP (label=%s)\n", label.c_str());
    for (int y = 13; y >= 0; --y) {
        printf("[");
        for (int x = 0; x < 6; ++x) {
            int v = cell_type_to_int(field.get_cell(static_cast<i8>(x), static_cast<i8>(y)));
            printf("%d", v);
            if (x < 5) printf(",");
        }
        printf("]\n");
    }
}*/

// --- push_control_entry の実装（main.cpp 内） ---
static void push_control_entry(std::vector<cell::Pair>& control_queue,
                               std::vector<move::Placement>& control_placements,
                               std::vector<Field>& control_field_snapshots,
                               const Field& current_field,
                               const cell::Pair& pair,
                               const move::Placement& plc)
{
    // push control and snapshot (snapshot represents the state AFTER this control was applied)
    control_queue.push_back(pair);
    control_placements.push_back(plc);
    control_field_snapshots.push_back(current_field);
}

// 連鎖集計用
int total_chain_events = 0;      // 連鎖が発生した回数（chain.count > 0 の回数）
int sum_chain_lengths = 0;       // 全連鎖長の合計（chain.count の合計）
int max_chain_length = 0;        // 最大連鎖長
int total_popped_overall = 0;    // 全体で消した puyo 個数（オプション

int main(int argc, char** argv)
{
    using namespace std;

    srand(uint32_t(time(NULL)));
    beam::eval::Weight w;
    save_json();
    load_json(w);

    u32 seed = rand() & 0xFFFF;
    seed = rand() & 0xFFFF;

    if (argc == 2) {
        seed = std::atoi(argv[1]);
    }

    printf("seed: %d\n", seed);

    auto queue = cell::create_queue(seed);
    Field field;

    // Final control to encode into single URL:
    vector<cell::Pair> control_queue;
    vector<move::Placement> control_placements;

    std::vector<Field> control_field_snapshots;
    // 各連鎖イベントの段数を記録する
    std::vector<int> chain_events;

    vector<move::Placement> placements_for_sim; // for logging
    vector<ScheduledGarbage> schedule; // scheduled garbage events

    i32 logical = 0;             // これを 100 にするまでループ（設置 + おじゃま の合計）
    i32 placements_done = 0;     // queue の何番目を消費したか（プレイヤーの手だけ進む）
    i32 time = 0;
    i32 score = 0;

    // --- initial garbage schedule seed so garbage starts appearing ---
    {
        int init_delay = (rand() % 3) + 3;   // 3..5
        int init_gcount = (rand() % 3) + 1;  // 1..3
        int init_due = init_delay; // relative to move 0
        if (init_due < 100) {
            schedule.push_back(ScheduledGarbage{ init_due, init_gcount });
        }
    }

    bool stopped_by_game_over = false;
    bool did_garbage = false;

    // Main simulation loop: up to 100 logical moves (placements + garbage events)
    while (logical < 100) {
        // 1) まず、logical に対して期限が来ているスケジュール済みゴミを探す
        std::vector<ScheduledGarbage> pending_schedules;
        bool did_garbage = false;

        for (auto it = schedule.begin(); it != schedule.end(); ) {
            if (it->due_move == logical) {
                // このスケジュールを1手分として適用する（プレイヤー手とは別に数える）
                int gcount = it->count; // 1..3

                // record heights before drop
                auto heights_before = get_all_heights(field);

                // drop randomly according to Field::drop_garbage_random
                field.drop_garbage_random(gcount);

                // record heights after drop
                auto heights_after = get_all_heights(field);

                // compute per-column delta (how many puyos were added to each column)
                int deltas[6];
                for (int x = 0; x < 6; ++x) {
                    deltas[x] = heights_after[x] - heights_before[x];
                    if (deltas[x] < 0) deltas[x] = 0; // safety
                }

                // Build control entries reflecting actual increases (既存ロジックそのまま)
                std::vector<int> increased_cols;
                for (int x = 0; x < 6; ++x) if (deltas[x] > 0) increased_cols.push_back(x);

                if (increased_cols.empty()) {
                    printf("[move %d] WARNING: drop did not increase any column\n", logical);
                } else {
                    bool contiguous = true;
                    for (size_t k = 1; k < increased_cols.size(); ++k) {
                        if (increased_cols[k] != increased_cols[k-1] + 1) { contiguous = false; break; }
                    }
                    bool all_one = true;
                    for (int col : increased_cols) if (deltas[col] != 1) { all_one = false; break; }

                    if (contiguous && all_one) {
                        int start_col = increased_cols.front();
                        int k = (int)increased_cols.size();
                        cell::Pair gp = { cell::Type::GARBAGE, cell::Type::GARBAGE };
                        move::Placement gp_placement;
                        gp_placement.x = static_cast<i8>(start_col);
                        gp_placement.r = static_cast<direction::Type>(k);
                        push_control_entry(control_queue, control_placements, control_field_snapshots, field, gp, gp_placement);
                    } else {
                        for (int col = 0; col < 6; ++col) {
                            for (int rep = 0; rep < deltas[col]; ++rep) {
                                cell::Pair gp = { cell::Type::GARBAGE, cell::Type::GARBAGE };
                                move::Placement gp_placement;
                                gp_placement.x = static_cast<i8>(col);
                                gp_placement.r = static_cast<direction::Type>(1);
                                push_control_entry(control_queue, control_placements, control_field_snapshots, field, gp, gp_placement);
                            }
                        }
                    }

                    int low = increased_cols.front();
                    int high = increased_cols.back();
                    int total = 0;
                }

                // Debug print
                {
                    auto hs = get_all_heights(field);
                    int center_h = field.get_height(2);
                    std::string viewer_url = encode::get_encoded_URL(field, control_queue, control_placements);
                    if (center_h > 11) {
                        stopped_by_game_over = true;
                    }
                }

                // schedule next garbage relative to logical (既存ロジックだが i -> logical に変更)
                if (!stopped_by_game_over) {
                    int next_gcount = (rand() % 3) + 1;
                    int chosen_due = -1;
                    const int MAX_SAMPLES = 6;
                    for (int s = 0; s < MAX_SAMPLES; ++s) {
                        int delay = (rand() % 3) + 3;
                        int due = logical + delay;
                        if (due >= 100) continue;
                        if (!has_conflict(schedule, due) && !has_conflict(pending_schedules, due)) {
                            chosen_due = due;
                            break;
                        }
                    }
                    if (chosen_due < 0) {
                        int max_search = std::min<int>(99, logical + 20);
                        for (int d = logical + 3; d <= max_search; ++d) {
                            if (!has_conflict(schedule, d) && !has_conflict(pending_schedules, d)) {
                                chosen_due = d;
                                break;
                            }
                        }
                    }
                    if (chosen_due < 0) {
                    } else {
                        pending_schedules.push_back(ScheduledGarbage{ chosen_due, next_gcount });
                    }
                }

                // erase current schedule entry and mark we did an おじゃま手
                it = schedule.erase(it);
                if (!pending_schedules.empty()) schedule.insert(schedule.end(), pending_schedules.begin(), pending_schedules.end());

                // count this garbage event as one logical 手
                logical++;
                did_garbage = true;

                // if game over, stop outer loop
                if (stopped_by_game_over) break;

                // break out of for loop because we only process one scheduled event per logical step
                break;
            } else {
                ++it;
            }
        } // end schedule loop

        if (stopped_by_game_over) break;

        if (did_garbage) {
            // we applied a garbage event this logical step; continue to next logical
            continue;
        }

        // 2) No scheduled garbage for this logical step -> プレイヤー（AI）手を1手分消費する
        // Prepare player's pair for AI using placements_done as the queue index
        vector<cell::Pair> tqueue;
        tqueue.push_back(queue[(placements_done + 0) % 128]);
        tqueue.push_back(queue[(placements_done + 1) % 128]);

        // Debug: show field passed to AI (same as before)
        {
            auto hs_before = get_all_heights(field);
            std::string viewer_url_before = encode::get_encoded_URL(field, control_queue, control_placements);
        }

        // AI thinking (unchanged)
        auto time_start = chrono::high_resolution_clock::now();
        auto ai_result = beam::search_multi(field, tqueue, w);
        auto time_stop = chrono::high_resolution_clock::now();
        auto dt = chrono::duration_cast<chrono::milliseconds>(time_stop - time_start).count();
        time += dt;

        // --- DEBUG: show top candidate simulations ---
        {
            int debug_show = std::min<int>(5, (int)ai_result.candidates.size());
            for (int ci = 0; ci < debug_show; ++ci) {
                auto c = ai_result.candidates[ci];
                // simulate candidate on a copy of the field
                Field tmp = field;
                tmp.drop_pair(c.placement.x, c.placement.r, tqueue[0]);
                auto mask_sim = tmp.pop();
                auto chain_sim = chain::get_score(mask_sim);
                auto hs_sim = get_all_heights(tmp);

                // viewer URL for this candidate (current control + this single candidate move)
                std::vector<cell::Pair> tmp_q = control_queue;
                std::vector<move::Placement> tmp_p = control_placements;
                tmp_q.push_back(tqueue[0]);
                tmp_p.push_back(c.placement);
                std::string cand_url = encode::get_encoded_URL(field, tmp_q, tmp_p);
            }
        }
        // --- end DEBUG block ---

        // If AI returned candidates, apply chosen move (or skip if no candidate). Regardless, this logical step counts as a player move.
        if (ai_result.candidates.empty()) {
            printf("[move %d] AI returned no candidates -> skipping placement\n", logical);
            // no placement applied, but we still consume the current pair
        } else if (field.get_height(2) > 11) {
            int center_h_check = field.get_height(2);
            printf("[move %d] (pre-AI) detected center column height=%d -> stopping\n", logical, center_h_check);
            stopped_by_game_over = true;
            break;
        } else {
            auto mv = ai_result.candidates[0];
            field.drop_pair(mv.placement.x, mv.placement.r, tqueue[0]);
            auto mask = field.pop();
            auto chain = chain::get_score(mask);

            // chain.count : 連鎖長（段数）
            // chain.score : スコア（既に使われている）
            int chain_len = chain.count;
            int chain_score = chain.score;

            if (chain_len > 0) {
                total_chain_events += 1;
                sum_chain_lengths += chain_len;
                chain_events.push_back(chain.count);
                if (chain_len > max_chain_length) max_chain_length = chain_len;
            }

            placements_for_sim.push_back(mv.placement);

            push_control_entry(control_queue, control_placements, control_field_snapshots, field, tqueue[0], mv.placement);

            printf("[move %d] AI placed (ets: %d) - %d ms\n", logical, mv.score / beam::BRANCH, dt);

            // Debug: show heights & viewer URL after placement/pop
            {
                auto hs_after = get_all_heights(field);
                int center_h_after = field.get_height(2);
                std::string viewer_url_after = encode::get_encoded_URL(field, control_queue, control_placements);
                //print_field_matrix(field, "after_placement");
                if (center_h_after > 11) {
                    printf("[move %d] GAME OVER detected after placement (center column height > 11). Stopping simulation.\n", logical);
                    stopped_by_game_over = true;
                    break;
                }
            }
            score += chain.score;
        }

        placements_done++;
        logical++;
    } // end main loop

    if (control_queue.size() != control_field_snapshots.size() || control_placements.size() != control_field_snapshots.size()) {
        printf("[ERROR] control/snapshot size mismatch: control_queue=%zu control_placements=%zu snapshots=%zu\n",
            control_queue.size(), control_placements.size(), control_field_snapshots.size());
        // optionally abort the replay check early to prevent misleading diffs
    } else {
        // --- replay-based debug: find first control-index mismatch =====
        Field replay;
        size_t first_mismatch = (size_t)-1;

        for (size_t idx = 0; idx < control_placements.size(); ++idx) {
        auto q = control_queue[idx];
        auto plc = control_placements[idx];

        if (q.first == cell::Type::GARBAGE && q.second == cell::Type::GARBAGE) {
            int r = static_cast<int>(plc.r);
            int x = static_cast<int>(plc.x);

            if (r > 1) {
                // contiguous cluster encoded in one entry
                if (x < 0) x = 0;
                if (x > 5) x = 5;
                int k = std::min<int>(r, 6 - x);
                for (int c = 0; c < k; ++c) {
                    replay.drop_puyo(static_cast<i8>(x + c), cell::Type::GARBAGE);
                }
            } else {
                // aggregate consecutive single-column garbage entries into mask
                int mask = 0;
                size_t j = idx;
                for (; j < control_placements.size(); ++j) {
                    auto qj = control_queue[j];
                    if (!(qj.first == cell::Type::GARBAGE && qj.second == cell::Type::GARBAGE)) break;
                    int col = static_cast<int>(control_placements[j].x);
                    if (col < 0) col = 0;
                    if (col > 5) col = 5;
                    mask |= (1 << col);
                }
                for (int c = 0; c < 6; ++c) {
                    if (mask & (1 << c)) {
                        replay.drop_puyo(static_cast<i8>(c), cell::Type::GARBAGE);
                    }
                }
                // jump forward to last processed single entry
                idx = j - 1;
            }
        } else {
            // player pair
            replay.drop_pair(plc.x, plc.r, q);
            replay.pop();
        }

            // compare this replay state to the snapshot saved when main actually applied this control
            auto real_h = get_all_heights(control_field_snapshots[idx]);
            auto replay_h = get_all_heights(replay);
            bool same = true;
            for (int c = 0; c < 6; ++c) {
                if (real_h[c] != replay_h[c]) { same = false; break; }
            }
            if (!same) {
                first_mismatch = idx;
                printf("[REPLAY CHECK] first mismatch at control index %zu\n", idx);
                printf("  real heights = [%d,%d,%d,%d,%d,%d]\n", real_h[0], real_h[1], real_h[2], real_h[3], real_h[4], real_h[5]);
                printf("  replay heights= [%d,%d,%d,%d,%d,%d]\n", replay_h[0], replay_h[1], replay_h[2], replay_h[3], replay_h[4], replay_h[5]);

                // dump nearby control entries for inspection
                int dump_start = std::max<int>(0, (int)idx - 6);
                int dump_end = std::min<int>((int)control_placements.size() - 1, (int)idx + 6);
                printf("Nearby control entries [%d..%d]:\n", dump_start, dump_end);
                for (int k = dump_start; k <= dump_end; ++k) {
                    auto pq = control_queue[k];
                    auto pp = control_placements[k];
                    int f = (pq.first == cell::Type::GARBAGE) ? 6 : encode::get_field_cell_id(pq.first);
                    int s = (pq.second == cell::Type::GARBAGE) ? 6 : encode::get_field_cell_id(pq.second);
                    printf("  [%d] pair=(%d,%d) plc.x=%d plc.r=%d\n", k, f, s, (int)pp.x, (int)pp.r);
                }
                break;
            }
        } // end for

        if (first_mismatch == (size_t)-1) {
            printf("[REPLAY CHECK] all control entries replayed ok (no mismatch)\n");
        }
    }
    // ===== end replay check =====

    std::string final_viewer = encode::get_encoded_URL(Field(), control_queue, control_placements);
    std::cout << final_viewer << std::endl;

    printf("time per move (avg ms): %s ms\n", std::to_string(double(time) / double(std::max<size_t>(1, placements_for_sim.size()))).c_str());
    printf("total score: %d\n", score);
    printf("control length (steps): %zu (includes garbage steps)\n", control_placements.size());
    printf("total chain events: %d\n", total_chain_events);
    printf("sum of chain lengths: %d\n", sum_chain_lengths);
    printf("max chain length: %d\n", max_chain_length);
    // 連鎖イベントリストを出力
    printf("chain events: [");
    for (size_t ci = 0; ci < chain_events.size(); ++ci) {
        printf("%d", chain_events[ci]);
        if (ci + 1 < chain_events.size()) printf(",");
    }
    printf("]\n");
    return 0;
}