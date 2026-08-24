# LHandPro Vendor SDK

This directory contains the binary-only LHandProLib runtime and C header from
`LHandProLib-API-Linux-20260727` supplied for RoboParty hardware integration.

| Architecture | SHA-256 |
| --- | --- |
| x86-64 | `3b0e3ec7e40c02b2f5ddd465ac2e22735b8730d9eec568ee6390caf1e66f8640` |
| AArch64 | `476f7687ff3063c7adbafef52b4f9326469a1d41f96eb1a516488f9be4064044` |

The repository GPL-3.0 declaration applies to RoboParty-authored source; it
does not relicense these vendor artifacts. The project owner has confirmed the
right to redistribute the exact two binaries listed above. This is an owner
authorization for this release record, not a new license grant or a claim that
the vendor manual defines callback-quiescence semantics.

The public SDK manual footer says `Copyright Notice: © Leadtron All rights
reserved` and `Using this software indicates that you agree to the User
Agreement`; the User Agreement text is not included in the supplied SDK tree.
The project owner has separately attested the redistribution right for this
release; the supplied manual still does not provide a standalone license for
other users or distribution models.

## Release Authorization Record

The following record identifies the SDK version and applies to both supplied
binaries. The project release policy accepts the owner authorization below for
redistribution. Callback-quiescence remains an empirical project acceptance
criterion and is not represented as a vendor contractual guarantee:

- x86-64 `3b0e3ec7e40c02b2f5ddd465ac2e22735b8730d9eec568ee6390caf1e66f8640`
- AArch64 `476f7687ff3063c7adbafef52b4f9326469a1d41f96eb1a516488f9be4064044`

The release record covers the following:

1. The project owner authorizes redistribution of the named header and both
   binaries for this release.
2. The exact x86-64 and AArch64 hashes above are the authorized artifacts.
3. Callback evidence is tied to those exact hashes and the tested Orange Pi,
   can0, node-1, 6DOF-S configuration.
4. Any SDK upgrade requires a new authorization and renewed physical evidence.

The status of this release record is **OWNER-ATTESTED**. The owner assertion is
not a substitute for vendor documentation in a different distribution or
licensing model.

Owner authorization record:

- Vendor/contact: Project owner authorization; vendor contact not required for
  this owner-held distribution right.
- SDK version and response date: LHandProLib-API-Linux-20260727; 2026-08-24.
- Callback-quiescence confirmation: Empirical project acceptance only; see the
  Orange Pi evidence paths and callback stress commits.
- Redistribution/license authorization: Owner confirms redistribution rights
  for the exact x86-64 and AArch64 artifacts listed above.
- GPL compatibility review: Owner confirms the intended package distribution
  is authorized; vendor artifacts remain separately licensed.
- Evidence or agreement reference: `main` release commit `66045a4` and
  `/home/orangepi/lhandpro_callback_decode_20260824_r4/evidence`.

The release-only structural guard can be run from the repository root:

```sh
PYTHONDONTWRITEBYTECODE=1 /usr/bin/python3 thirdparty/check_vendor_gate.py \
  --readme thirdparty/README.md \
  --x86 thirdparty/lib/x86_64/libLHandProLib.so \
  --aarch64 thirdparty/lib/aarch64/libLHandProLib.so
```

It must return nonzero while this record is incomplete. A zero result only means
the recorded response is complete and the artifact hashes match; it does not
replace human/legal review of the vendor response.

## Auxiliary Local Smoke Evidence

On 2026-08-24, the x86-64 binary matching the SHA above was exercised in a
100-iteration lifecycle smoke. Each iteration performed only
`create -> set_send_canfd_callback -> start_monitor -> stop_monitor -> set
callback(NULL) -> close -> destroy`; it did not call `initial_ex`, configure a
CAN interface, or issue a motor command. The result was
`PASS iterations=100 callback_calls=0 no_initial_ex=1`, and a concurrent
`strace` found no socket, ioctl, send, recv, or connect syscall.

This is auxiliary evidence only. Because no callback was generated, it does
not prove in-flight callback draining, late-callback safety, or vendor
quiescence semantics. It does not replace the stronger empirical evidence
recorded for this release below.

The same 100-iteration source was then built natively as an AArch64 executable
on the Orange Pi and linked to the installed binary matching
`476f7687ff3063c7adbafef52b4f9326469a1d41f96eb1a516488f9be4064044`.
It also returned
`PASS iterations=100 callback_calls=0 no_initial_ex=1`. The can0 TX counters
were unchanged at 862 bytes and 55 packets before and after the run; can0
remained `ERROR-ACTIVE` at 1 Mbit/s nominal and 5 Mbit/s data rate with zero
bus, RX, TX, and dropped-frame errors. `strace` was not installed on the board,
so no syscall-trace claim is made for this run.

As with the x86-64 smoke, the AArch64 result generated no callback and does not
prove callback quiescence or grant redistribution rights.

## Supplementary Current-Binary Inspection

The current x86-64 blob at the SHA above exports a private
`LHandProLibPrivate::stop_monitor_thread()` implementation whose observed
machine code clears the monitor flag and calls `std::thread::join()`. The
current AArch64 blob exports the same private function and contains the
`std::thread::join` symbol. This is useful evidence about these exact blobs,
but it is not a vendor API contract and does not prove callback-pointer update
ordering, in-flight callback draining, or behavior after `destroy()`. It must
not be used to replace the required written vendor response.

An additional x86-64 probe ran inside a separate unprivileged network
namespace with a virtual `can0` (`vcan`); the physical `can0` configuration
hash was identical before and after the probe. `initial_ex(C_LCN_CANFD, 1)`
returned `0`, the callback count reached `7`, and after callback removal,
close, destroy, and a 100 ms wait the count was still `7`:
`initial_rc=0 model_rc=0 callback_calls=5`,
`before_destroy=7 after_destroy=7`, `RESULT callback_observed`. The probe used
only a virtual bus and did not call homing or enable APIs. This demonstrates
that callbacks can occur and that no late callback was observed in this run;
it still does not force an in-flight callback across `stop_monitor()` or prove
the vendor's general shutdown contract.
