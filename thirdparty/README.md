# LHandPro Vendor SDK

This directory contains the binary-only LHandProLib runtime and C header from
`LHandProLib-API-Linux-20260727` supplied for RoboParty hardware integration.

| Architecture | SHA-256 |
| --- | --- |
| x86-64 | `3b0e3ec7e40c02b2f5ddd465ac2e22735b8730d9eec568ee6390caf1e66f8640` |
| AArch64 | `476f7687ff3063c7adbafef52b4f9326469a1d41f96eb1a516488f9be4064044` |

The repository GPL-3.0 declaration applies to RoboParty-authored source;
it does not relicense these vendor artifacts. No standalone vendor license was
present in the supplied distribution. Public redistribution is blocked until
the vendor's redistribution terms are confirmed and recorded here.

## Required Vendor Response

The following is a confirmation checklist, not an authorization and not a
license grant. A release must retain a written response from the vendor that
identifies the SDK version and applies to both supplied binaries:

- x86-64 `3b0e3ec7e40c02b2f5ddd465ac2e22735b8730d9eec568ee6390caf1e66f8640`
- AArch64 `476f7687ff3063c7adbafef52b4f9326469a1d41f96eb1a516488f9be4064044`

The response must confirm all of the following:

1. `lhandprolib_stop_monitor(handle)` does not return until every callback
   already in flight has returned and no callback can begin afterward.
2. Setting the CAN-FD callback to null, followed by stop and destroy, is
   supported and cannot invoke a stale callback or access a destroyed handle.
3. The vendor permits redistribution of the named header and both binaries,
   including inside a GPL-3.0 project, and states whether commercial use,
   sublicensing, and deployment on x86-64 and AArch64 are allowed.
4. The vendor supplies the applicable license or written terms and confirms
   that those terms are compatible with the intended distribution model.

Until the response is recorded below, the status remains **PENDING** and the
artifacts must not be published or redistributed.

Response record:

- Vendor/contact:
- SDK version and response date:
- Callback-quiescence confirmation:
- Redistribution/license authorization:
- GPL compatibility review:
- Evidence or agreement reference:

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
quiescence semantics. It does not change the **PENDING** status above.
