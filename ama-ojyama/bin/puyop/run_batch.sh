#!/usr/bin/env bash
# 実行: ./run_batch.sh [SUCCESS_TARGET] [BLOCK_SIZE]
# 出力ファイル:
#  - per-sim results -> results.csv
#  - block summaries + final summary -> summary.csv

BINARY="./puyop.exe"
OUTFILE="results.csv"
SUMMARY_OUT="summary.csv"
TMPOUT="puyop_last.out"

SUCCESS_TARGET=${1:-${SUCCESS_TARGET:-1000}}   # 目標（成功）数
BLOCK_SIZE=${2:-${BLOCK_SIZE:-100}}            # 小計を出す単位（デフォルト100）
success=0
attempts=0
errors=0   # replay check NG の回数（含めないがカウント）

# 初期化: per-sim file
echo "seed, total_score, max_chain, chain_events" > "$OUTFILE"

# 初期化: summary file (block summaries + final summary)
echo "block_summary" > "$SUMMARY_OUT"
echo "times, avg_score_block, avg_max_chain_block" >> "$SUMMARY_OUT"
echo "" >> "$SUMMARY_OUT"

# 集計（blockごと）用
sum_score_block=0
sum_max_chain_block=0
count_block=0
block_index=0

# 全体集計
sum_score_total=0
sum_max_chain_total=0
count_total=0

while [ "$success" -lt "$SUCCESS_TARGET" ]; do
  seed=$RANDOM
  attempts=$((attempts+1))
  echo "=== attempt #$attempts seed=$seed (success $success/$SUCCESS_TARGET) ==="

  "$BINARY" "$seed" 2>&1 | tee "$TMPOUT"

  # 判定: replay check が OK かどうか
  if grep -q "\[REPLAY CHECK\] all control entries replayed ok (no mismatch)" "$TMPOUT"; then

      # --- 新規: 警告行の検出 ---
      # ここで警告パターンがないことを確認する。該当パターンがあれば errors++ として次へ。
      has_warn=0
      # 例として明示された警告をチェック（必要なら追加）
      if grep -q "WARNING: drop did not increase any column" "$TMPOUT"; then
          echo "[WARN-DETECT] drop did not increase any column in attempt seed=${seed}"
          has_warn=1
      fi
      if grep -q "AI returned no candidates" "$TMPOUT"; then
          echo "[WARN-DETECT] AI returned no candidates in attempt seed=${seed}"
          has_warn=1
      fi

      # もし警告があれば errors にカウントしてスキップ
      if [ "$has_warn" -ne 0 ]; then
          errors=$((errors + 1))
          echo "[WARN] Treating attempt as ERROR due to detected warning(s) (seed=${seed}). errors=$errors"
          continue
      fi
      # --- 警告検出終了 ---

      # ここから従来どおり成功扱いとして結果を抽出・保存する
      s_seed=$(grep "^seed:" "$TMPOUT" | head -n1 | awk '{print $2}')

      if grep -q "^total score:" "$TMPOUT"; then
          s_score=$(grep "^total score:" "$TMPOUT" | tail -n1 | awk '{print $NF}')
      elif grep -q "score (largest chain seen):" "$TMPOUT"; then
          s_score=$(grep "score (largest chain seen):" "$TMPOUT" | tail -n1 | awk '{print $NF}')
      else
          s_score=""
      fi

      s_chain=$(grep "^chain events:" "$TMPOUT" | tail -n1 | sed -e 's/^chain events: //')

      if [ -z "$s_score" ]; then
          s_score=0
      fi
      s_score=$(echo "$s_score" | sed -E 's/[^0-9-]//g')
      if [ -z "$s_score" ]; then s_score=0; fi

      if echo "$s_chain" | grep -q "\["; then
          numbers=$(echo "$s_chain" | tr -d '[] ' | tr ',' '\n')
          maxchain=0
          for v in $numbers; do
              if [ -z "$v" ]; then continue; fi
              if [ "$v" -gt "$maxchain" ]; then maxchain=$v; fi
          done
      else
          maxchain=0
      fi

      # Append per-simulation row to results.csv
      echo "${s_seed}, ${s_score}, ${maxchain}, \"${s_chain}\"" >> "$OUTFILE"

      # Update block & total aggregates
      sum_score_block=$((sum_score_block + s_score))
      sum_max_chain_block=$((sum_max_chain_block + maxchain))
      count_block=$((count_block + 1))

      sum_score_total=$((sum_score_total + s_score))
      sum_max_chain_total=$((sum_max_chain_total + maxchain))
      count_total=$((count_total + 1))

      success=$((success + 1))

      # BLOCK_SIZE ごとに小計を summary.csv に追記（かつコンソールに表示）
      if [ $((success % BLOCK_SIZE)) -eq 0 ]; then
          block_index=$((block_index + 1))
          avg_score_block=$(awk "BEGIN { printf \"%.2f\", ${sum_score_block}/${count_block} }")
          avg_max_chain_block=$(awk "BEGIN { printf \"%.2f\", ${sum_max_chain_block}/${count_block} }")
          # 範囲を計算：end = success, start = end - count_block + 1
          range_end=$((success))
          range_start=$((range_end - count_block + 1))
          echo "== Progress: $success successful sims (attempts $attempts). Block ${block_index} range ${range_start}~${range_end} avg score=${avg_score_block} avg max_chain=${avg_max_chain_block}"
          # Append block summary to SUMMARY_OUT using range notation
          echo "${range_start} ~ ${range_end}, ${avg_score_block}, ${avg_max_chain_block}" >> "$SUMMARY_OUT"
          # Reset block accumulators
          sum_score_block=0
          sum_max_chain_block=0
          count_block=0
      fi

  else
      # REPLAY CHECK が NG -> errors にする
      errors=$((errors + 1))
      echo "[WARN] replay check FAILED for seed=$seed (attempt $attempts). errors=$errors"
  fi

  sleep 0.01
done

# If a final partial block remains, write it to SUMMARY_OUT
if [ "$count_block" -gt 0 ]; then
    block_index=$((block_index + 1))
    avg_score_block=$(awk "BEGIN { printf \"%.2f\", ${sum_score_block}/${count_block} }")
    avg_max_chain_block=$(awk "BEGIN { printf \"%.2f\", ${sum_max_chain_block}/${count_block} }")
    range_end=$((success))
    range_start=$((range_end - count_block + 1))
    echo "== Final partial block ${block_index} ( ${count_block} sims ): range ${range_start}~${range_end} avg score=${avg_score_block} avg max_chain=${avg_max_chain_block}"
    echo "${range_start} ~ ${range_end}, ${avg_score_block}, ${avg_max_chain_block}" >> "$SUMMARY_OUT"
fi

# 最終集計（成功N回分）
if [ "$count_total" -gt 0 ]; then
    avg_score_total=$(awk "BEGIN { printf \"%.2f\", ${sum_score_total}/${count_total} }")
    avg_max_chain_total=$(awk "BEGIN { printf \"%.2f\", ${sum_max_chain_total}/${count_total} }")
else
    avg_score_total=0
    avg_max_chain_total=0
fi

echo "======================================"
echo "Finished: success=$success attempts=$attempts errors=$errors"
echo "Overall (successful $count_total): avg total score = ${avg_score_total}, avg max chain = ${avg_max_chain_total}"
echo "Results appended to $OUTFILE and $SUMMARY_OUT"

# Append final summary to SUMMARY_OUT
timestamp=$(date '+%Y-%m-%d %H:%M:%S')
echo "" >> "$SUMMARY_OUT"
echo "==================================================================================" >> "$SUMMARY_OUT"
echo "Summary generated_at, successes, attempts, errors, avg_total_score, avg_max_chain" >> "$SUMMARY_OUT"
echo "summary,\"${timestamp}\", ${success}, ${attempts}, ${errors}, ${avg_score_total}, ${avg_max_chain_total}" >> "$SUMMARY_OUT"
echo "==================================================================================" >> "$SUMMARY_OUT"