# WebView messages

`messages.json` owns the message envelope, payload shape, and allowed sender windows.
Change it here and run `python3 generate.py` in the same commit. The generated
TypeScript union and embedded C++ schema must pass `generate.py --check`.
`runtime.js` and `validator.h` implement the documented schema subset and run the same
`fixtures.json` compatibility cases. The standalone CMake test requires Boost.JSON;
the input engine itself does not acquire that dependency.

New messages carry `protocolVersion: 1`. A missing version is accepted as legacy v1;
an explicit unknown version, unknown message, invalid payload, or message sent from
the wrong window is rejected before dispatch. This is envelope validation, not a
replacement for domain checks such as dictionary key validation, allowed configuration
values, or the host's HTTPS URL policy.

The name is historical: the contract was written for the WebView2 pages, which have
been removed. Its live consumer is the Direct2D settings window
(`server/src/settings/native/`), which posts client messages to and receives server
messages from `server/src/settings/settings_app.cpp` in exactly this format. The
`candidate`, `toolbar` and `menu` surfaces and the window-chrome messages (`dragStart`,
`resizeHitTest`, `windowControl`, `maximizeButtonRect`, `windowState`, `maxButtonEvent`)
no longer have a sender or receiver in the product; they stay in the schema so old
fixtures keep validating. The generated JavaScript and TypeScript bindings likewise have
no in-repo consumer any more.
