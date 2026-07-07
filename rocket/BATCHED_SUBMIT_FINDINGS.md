# Batched multi-task submit on RK3588 — mechanism, implementation, and the dead ends

The dispatch-floor lever "fire a job's many tasks in one HW kick instead of one
submit + completion IRQ per task" is implemented and validated on the RK1
(mainline `rocket` 7.1 and the 7.1.1 rebuild, 600 MHz, `performance` governor;
the patch carries across the kernel minor-version bump unchanged). A multi-task job runs as
one kick over a contiguous stream of self-chaining register-command programs,
with `PC_TASK_CON.TASK_NUMBER = N` gating a single completion interrupt. It is a
coordinated userspace (regcmd generation) + kernel (one-kick dispatch) change;
the userspace half is the substantive one.

The two halves must be enabled together (see "Coordinated halves" below). Chaining is
selected per job by the `DRM_ROCKET_JOB_BATCHED` uAPI flag (userspace sets it only on
the jobs it self-chained), so chained fp16 and gapped int8/int4/prepacked jobs coexist in
one process. Userspace gates the layout + flag on `ROCKET_BATCH_SUBMIT=1` (off by default),
so the stock per-task path is unchanged until it is turned on.

> The per-job flag REQUIRES the current kernel half — verify it is loaded before trusting
> `ROCKET_BATCH_SUBMIT=1` [HW sweep 2026-06-30]. The per-job `DRM_ROCKET_JOB_BATCHED`
> selectivity is what lets chained fp16 and gapped int8/int4/norm jobs coexist. The shipped
> `085` patch defaults the master `rocket_batch_submit` param to `1`, and that is safe precisely
> because batching is gated per job: the kernel batches only a job whose userspace set
> `DRM_ROCKET_JOB_BATCHED` (self-chained fp16, under `ROCKET_BATCH_SUBMIT=1`), never a gapped
> job. An older `rocket` build that has only the global `rocket_batch_submit` param (no
> per-job flag) has no such selectivity: with the param on it force-batches every multi-task
> job — fp16-chained jobs run correct, but every gapped job
> (int8/int4/fp32-out/reduce/rmsnorm/softmax/cross-entropy/layernorm, and flash-attn's gapped
> batches) streams into its inter-task gap → garbage or a job-timeout, and a real F16 LLM
> prefill wedges the NPU (the graph is not pure fp16-chained-matmul). Such a global-param-only
> build must therefore keep the param off and gives no per-job coexistence regardless — do
> not rely on chaining there; chaining needs the per-job flag, i.e. the `085` patch.
> **Gate before use:** run `mixed_chain_coexist_rocket` with the kernel param at 1 — it must
> PASS (chained fp16 + gapped int8 in one process). On a per-job-flag kernel it passes; on a
> global-param-only kernel it FAILS (the gapped int8 half garbles). The RK1's `7.1.1-1` build of
> 2026-06-24 is global-param-only (predates the 2026-06-29 per-job-flag patch); rebuild + reload
> the module from `085-rocket-drv-batched-submit.patch` before evaluating chaining there.

## The mechanism (HW-validated 2026-06-24)

A multi-task job on RK3588 runs as one kick over a contiguous stream of
self-chaining regcmds:

1. The N tasks' regcmds are laid contiguously in one BO at a uniform per-task
   stride (no inter-task gap). The stride is the regcmd's u64 word count rounded up
   to a 128-bit (2-u64) boundary — exactly how far the PC advances per task (it
   reads `(regcmd_count+1)/2` 128-bit chunks). Every matmul tile shares one
   data-independent word count (126 for fp16), so the stride is uniform.
2. Each task's trailer is rewritten to link to the next task:
   - an embedded `PC_BASE_ADDRESS` op carries the next task's address, and
   - the embedded `PC_REGISTER_AMOUNTS` op carries the next segment's encoded
     length (`(count+1)/2 - 1`).
   The last task's link is zeroed (it has no successor).
3. The kernel programs only task 0's `PC_DATA_ADDR` (= `tasks[0].regcmd`) and
   `PC_DATA_AMOUNT`, sets `PC_TASK_CON.TASK_NUMBER = N`, leaves
   `PC_TASK_DMA_BASE_ADDR = 0`, and kicks once. The PC executes one `OP_ENABLE` per
   task and follows each task's `PC_BASE_ADDRESS` link to the next.
4. `TASK_NUMBER` gates a single completion IRQ — it fires once, after the last
   task completes. The kernel retires the whole job on that one IRQ.

### The `PC_BASE_ADDRESS` redirect is load-bearing [HW sweep]

The open question from the prior session — does the trailer `PC_REGISTER_AMOUNTS`
carry its own length, the next task's length, or act as an end marker — was settled
empirically. On a 2-tile matmul (`512 256 128`, kernel `TASK_NUMBER = N`):

- **Contiguous, trailer left at 0** → only task 0 runs, half the output is correct,
  the counter sticks at 1, 500 ms timeout. The trailer 0 is a single task's
  "0 instructions left" end marker; against a chain it stops the PC after task 0.
- **Contiguous + trailer rewritten to the encoded next length** → identical stall.
  Setting the next segment's length without its address is not enough.
- **Contiguous + the encoded length AND an embedded `PC_BASE_ADDRESS` op pointing at
  the next task** → correct (cosine 1.000000 vs the per-task path), one IRQ, no
  timeout. The PC has to be told where the next task is; the redirect op is the
  one that advances it.

This matches the only working multi-task reference (the allbilly-npu RE
project's `rknnops.h`), which emits both a `PC_BASE_ADDRESS`
(→ next task) and a `PC_REGISTER_AMOUNTS` op per task. Our matmul regcmd already
carries the amount op; the chaining repurposes the inert `OP_NONE` filler just
before it into the base-address op, keeping the op count (and so the stride) intact.

### One IRQ per job, gated by `TASK_NUMBER` — not by a counter read [HW sweep]

A 2..64-task batch raises exactly one completion IRQ, after the last task. Do
not gate the retire on a `PC_TASK_STATUS` read: at IRQ time on this kernel
`PC_TASK_STATUS` (0x3c) reads `0x0000f000`, i.e. `& 0xfff == 0`, not the completed
count — a guard of the form `(TASK_STATUS & 0xfff) >= task_count` never fires and
the job times out despite correct output. The kernel instead advances
`next_task_idx` to `task_count` at submit, so the existing IRQ-handler retire path
(`next_task_idx == task_count`) signals the fence on the single gated IRQ. No
intermediate IRQs were observed up to a 64-task batch, so no premature-IRQ guard is
needed.

## Implementation

- **`rocket-userspace`** (`src/rocket_matmul.c`): `mm_pack_regcmd` lays each tile's
  regcmd contiguously and links its trailer to the next; `mm_seal_chain` zeroes the
  final link. Gated by `ROCKET_BATCH_SUBMIT=1` (`mm_batch_chained`), default off,
  with the gapped per-task layout kept as the fallback. Off-device gate:
  `tests/chain_layout_rocket.c` (verifies the even/uniform word count, the trailer
  op location, the contiguous stride, and the amount/base rewrites without
  hardware). Wired into the `mm_compute` path (the default independent-tile fp16
  matmul, also used per-worker by the multicore `mt` path).
- **`rocket` kernel** (`rocket-patches/085-rocket-drv-batched-submit.patch` + the canonical
  uAPI header `rocket-patches/uapi/rocket_accel.h`): a per-job `DRM_ROCKET_JOB_BATCHED`
  flag in `drm_rocket_job.flags`. `rocket_ioctl_submit_job` captures it into
  `rjob->want_batched`; `rocket_job_run` sets `job->batched` when the flag is set, the
  master `rocket_batch_submit` param is on (**default `1`; a global kill-switch over the per-job
  flag — set it to 0 to force every job back onto the stock per-task path**), the
  job has > 1 task, and the count fits the 12-bit `TASK_NUMBER` field; `rocket_job_hw_submit` then sets
  `TASK_NUMBER = task_count` and advances `next_task_idx` to the end. The IRQ handler is
  otherwise the stock per-task path. The `flags` field is appended past the original struct
  and copied `min(job_struct_size, sizeof)` + zero-fill, so a stock userspace is unaffected.

### Coordinated halves

The two halves must be enabled together. A job that carries the flag but whose regcmds
are laid gapped runs task 0, streams into the gap → counter sticks `< N` → 500 ms timeout
(recoverable; drm_sched resets the core; no hard-lock observed across the bring-up,
including the deliberate mismatch runs). Userspace must set `DRM_ROCKET_JOB_BATCHED` only on
jobs it actually self-chained — which is exactly what `rocket-userspace` does (it passes the
`batched` arg = the same `ROCKET_BATCH_SUBMIT` chained-layout decision through to the submit).
Because the flag is per-job, a chained fp16 job and a gapped int8/int4/prepacked job coexist
in one process; the master param is just a global kill-switch (set 0 to force all per-task).

## Performance (measured 2026-06-24, RK1, 600 MHz, `performance` governor)

`matmul_tiled_rocket 512 3840 4096` (320 tiles → 5 batches of 64), warm, single-fd:

- `ROCKET_MM_PROFILE` `wait` term ~62 → ~48 ms (the per-tile IRQ round-trips:
  320 → 5).
- GFLOP/s ~94 → ~96 median (best 99 vs stock 95).

Reproduced 2026-06-25 on the 7.1.1 rebuild (warm `wait` ~68 → ~46 ms; chained GFLOP/s
≥ stock in every paired run, ~85 → ~93 median, best 94). The absolute `wait` baseline
shifts a few ms with the kernel build and the clock-warmth at sample time; the drop
(the 320 → 5 IRQ-round-trip collapse) is the stable signal. The no-pin NPU clock parks
at idle and ramps during each ~170 ms run, so the A/B has to interleave stock/chained
per iteration (pairing cancels the slow clock drift) — a naive back-to-back run buries
the ~20 ms drop under ~15 ms of clock-state noise.

The win is the dispatch slice of a matmul that is otherwise NPU-compute /
readback-bound at this operating point — modest here, larger on submit-overhead-bound
paths (many small tiles: the detection convs, multi-fd contention, decode). It
composes with the other dispatch-floor levers (IRQ affinity cuts a submit's wakeup
latency, IOMMU keep-attached cuts a submit's kernel work, batched submit cuts the
number of submits). `submit_overhead_rocket` is not a probe for this lever — it
submits 1-task jobs, which fall back to the per-task path.

## Scope and remaining work

The lever pays on jobs that decompose into independent tasks (a matmul's
`nMt·nNt` output tiles). It does not collapse a data-dependent chain: a transformer
prefill is sequential across layers, and the `ROCKET_KACC` K-tiles ping-pong (each
reads the prior partial), so they still fence in order. The realistic ceiling is
below the vendor's "1 submit per inference," a feed-forward vision-CNN number.

Chaining is wired into all fp16 matmul submit paths: the one-shot `mm_compute`, the
default `mm_compute_kacc` (resident/prepacked + stream backend), and the flash-attn
`rocket_mm_batch_run`, all via the per-job flag (HW sweep 2026-06-29: bit-exact, full ctest
green under `ROCKET_BATCH_SUBMIT=1`). `mm_compute_kacc` chains the independent tiles WITHIN
each `ki`-job; chaining ACROSS the `ki` accumulation submits (they still fence in order, each
reading the prior partial) is the remaining step (FP16_ACTION_PLAN #9). `mm_compute_reuse` /
`mm_compute_pipe` stay gapped (non-default paths). The int8/int4 paths force gapped (CACC,
below). The earlier "prepacked path times out under chaining" was a symptom of the global
param (it forced `TASK_NUMBER=N` onto a gapped-layout submit in the prepacked pipeline); with
the per-job flag only self-chained jobs are flagged, and the prepacked/resident path chains
with no timeout, numerically identical to gapped, single- and multi-worker at prefill scale.

## The dead end — do not rebuild it

A kernel-only model — build an `rknpu_task`-format descriptor table from the job's
`tasks[]`, map it into the job's IOMMU domain, point `PC_TASK_DMA_BASE_ADDR` at it,
set `TASK_NUMBER = N`, retire on one IRQ — was implemented and disproven on HW.
Every variant (`regcfg_amount` as `count−4` and as the encoded `(count+1)/2−1`;
per-task vs all-DPU `int_mask`; `op_idx`/`enable_mask=0x7f`/`int_clear=0x1ffff` per
phhusson; the table mapped READ and READ|WRITE; the BSP `OP_EN` 1→0 pulse) produced
no completion IRQ, a 500 ms timeout, a drm_sched reset, and garbage out. A 1-task
DMA-walk was the decisive isolation: a task that runs correctly with
`PC_TASK_DMA_BASE_ADDR = 0` stalls the instant that register is pointed at the
descriptor table. There is no kernel-built descriptor table the PC DMA-walks on
RK3588 (allbilly-npu's `rknnops.h` sets `task_base_addr = 0`;
the rknpu-reverse-engineering project's `hello2.c` sets it to the regcmd buffer — it is
don't-care for the stream). The `rknpu_task` array in the BSP exists only so the BSP
kernel can read per-task fields CPU-side. Do not point `PC_TASK_DMA_BASE_ADDR` at
anything, and do not expect the trailer amount alone (without the `PC_BASE_ADDRESS`
redirect) to chain.

## Chaining is fp16-only — the integer datapath garbles (HW sweep 2026-06-28)

Extending the chain to the resident int8 / int4 matmul paths (`rocket_prepacked_int8.c`,
`rocket_prepacked_int4.c`) is HW-blocked, not the mechanical follow-up it looked like. The
chained layout is dtype-independent and verified off-device — `tests/chain_layout_rocket.c`
now checks int8 and int4 as well as fp16, and all three share the same 126-word even op count
and `[OP_NONE, PC_REGISTER_AMOUNTS, OP_40, OP_ENABLE]` trailer (trailer idx 123, `OP_NONE`
filler at 122). But on hardware a chained int8/int4 batch computes the **first task bit-exact
and every subsequent task garbage**, identically whether the second tile comes from the M, N,
or K split (`matmul_int8_prepacked_rocket M K N W` under `ROCKET_BATCH_SUBMIT=1` + kernel
`rocket_batch_submit=1`: `256 640 256 1` = 1 tile PASS; `512 640 256 1` / `256 1280 256 1` /
`256 640 512 1` all fail on the 2nd tile). fp16 chains any batch length bit-exactly.

**Root cause:** the integer int32 accumulator (CACC) clears per HW kick, not per task.
Chaining runs N tasks in one kick, so task N+1 accumulates onto task N's residual — the same
accumulator property behind the no-cross-op-int32-accumulate ceiling (`FINDINGS` in the notes:
`encodings/k-accumulation.md`, `encodings/sdp-stage-precision.md`). fp16 doesn't carry that
accumulator across tiles, so it is immune. Re-enabling integer chaining needs a per-task
CACC-clear op in the chained regcmd (not known to exist).

**What shipped instead:** the chain primitives were extracted to a shared module
`src/rocket_chain.{c,h}` (`rkt_chain_enabled` / `rkt_chain_pack` / `rkt_chain_seal`, with the
caller's gapped slot stride as a parameter). The fp16 matmul paths in `rocket_matmul.c` now use
it (bit-exact; the full 56-gate ctest stays green). The resident int8/int4 paths call it with
`chained = 0` forced and a comment pointing here. So the integer paths keep lever 1
(one ioctl, N gapped tasks = N separate kicks, CACC clears per task — the submit-overhead win
they already had); only lever 2 (one kick / one IRQ) is fp16-only.

**Per-job flag (resolves the old global-param limitation):** chaining is now selected per job by
`DRM_ROCKET_JOB_BATCHED`, not a global param, so fp16-chained and int8/int4-gapped jobs run in the
same process without mismatch — the int8/int4 paths simply leave the flag clear (`chained = 0`)
and the kernel runs them per-task. Validated by `mixed_chain_coexist_rocket` (chained fp16 cos
1.000000 + gapped int8 bit-exact, interleaved on one fd, PASS). The master `rocket_batch_submit`
param remains as a global kill-switch (default 1 honors the flag; 0 forces all per-task).
