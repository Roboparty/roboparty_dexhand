# LHandProLib Vendor Confirmation Request

This draft is intended for the LHandPro SDK vendor's technical and licensing
contacts. It is a request for written confirmation only; it does not change the
license status of the SDK artifacts in this repository.

Suggested recipient: `tech@leadshine.com`

## Subject

Request for LHandProLib callback-quiescence and redistribution authorization

## Message

Hello,

We are integrating the LHandPro 6DOF S hand through the Linux CAN-FD API in a
GPL-3.0 project. Please provide a written response covering the exact SDK
artifacts below:

- SDK package: `LHandProLib-API-Linux-20260727`
- x86-64 `libLHandProLib.so` SHA-256:
  `3b0e3ec7e40c02b2f5ddd465ac2e22735b8730d9eec568ee6390caf1e66f8640`
- AArch64 `libLHandProLib.so` SHA-256:
  `476f7687ff3063c7adbafef52b4f9326469a1d41f96eb1a516488f9be4064044`
- Header: `LHandProLib.h` from the same package

Please confirm, for this SDK version:

1. `lhandprolib_stop_monitor(handle)` returns only after all callbacks already
   in flight have returned, and no callback can begin after it returns.
2. Setting the CAN-FD callback to null, then stopping and destroying the
   handle, is supported and cannot invoke a stale callback or access a
   destroyed handle.
3. Redistribution of the named header and both binaries is permitted inside a
   GPL-3.0 project, including commercial deployment on x86-64 and AArch64.
4. Sublicensing and packaging with the project's source are permitted, or you
   provide the exact restrictions that apply.
5. You provide the applicable license or written terms and confirm whether
   those terms are compatible with the intended GPL-3.0 distribution model.
6. The permission also covers the combined work formed when the GPL-3.0
   `libdexhand.so` dynamically links to `libLHandProLib.so`, including the
   installed C++ and Python consumers.

Please identify the responding organization, SDK version, response date, and
any agreement or license reference. Until this response is received, we will
not publish or redistribute the vendor binaries.

## Public References (Not Authorization)

- The vendor product page describes the SDK as an "open-source SDK and DEMO"
  but does not identify a specific license:
  <https://www.leadshine.com/robots/dextrous-hands/dh116s-series.html>
- The public SDK manual documents `close()` as terminating connections, but
  does not state the callback in-flight or late-callback guarantees requested
  above:
  <https://en.leadshine.com/upfiles/downloads/2b08becef68de752631be92de0dd26ab_1766731626237.pdf>
- The public sample guide lists the SDK package layout but does not itself
  grant redistribution rights:
  <https://www.leadshine.com/upfiles/downloads/5c21396d3eb9c2eee78f7b768e400324_1766731664504.pdf>

Regards,

`<name>`
`<organization>`
`<contact>`

## Record Of Response

- Vendor/contact:
- SDK version and response date:
- Callback-quiescence confirmation:
- Redistribution/license authorization:
- GPL compatibility review:
- Evidence or agreement reference:
