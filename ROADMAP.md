# twitch-adblock C++ port — build roadmap

Goal: replace `local-server/` (Java) and `rust-wrapper/` (Rust) with a single
from-scratch C++ executable on Windows, using only Winsock + Schannel + SSPI +
Win32 (no OpenSSL, no libcurl, no nlohmann/json, no cpp-httplib). The
`extension/` folder stays TypeScript — browser extensions cannot be native
code, that's a platform limit, not a scope choice.

Everything below is a straight port of working, already-understood logic in
`twitch-adblock/local-server/src/main/java/com/hawolt/**`. Each stage names
the Java file(s) it replaces so you can read the reference behavior instead
of guessing at it.

Suggested new folder layout (create alongside the existing `twitch-adblock/`):

```
c-adblock/
  twitch-adblock/        <- existing repo, untouched, used as reference
  cpp-server/
    src/
      net/                socket.cpp/.h, tls_schannel.cpp/.h
      http/                http_client.cpp/.h, http_server.cpp/.h
      json/                json.cpp/.h
      hls/                 attr_list.cpp/.h, master_playlist.cpp/.h
      twitch/              config.cpp/.h, client_id.cpp/.h, instance.cpp/.h,
                            gql.cpp/.h, usher.cpp/.h, poller.cpp/.h
      session/              registry.cpp/.h
      server/                routes.cpp/.h
      tray/                  tray.cpp/.h
      main.cpp
    tests/                small standalone .cpp files per stage, see below
```

Build one `.exe` per stage under `tests/` before wiring it into the real
server — that's how you catch Schannel/parsing bugs without the noise of the
whole system running at once.

---

## Stage 0 — Winsock bootstrap

**What:** `WSAStartup(MAKEWORD(2,2), &wsaData)` once at process start,
`WSACleanup()` at exit. A couple of `std::string` error-formatting helpers
around `WSAGetLastError()`.

**Files:** `net/socket.h` (just the init/cleanup pair for now).

**Link:** `ws2_32.lib`

**Done when:** a 5-line program calls init, prints the Winsock version,
cleans up, exits 0.

---

## Stage 1 — TCP socket wrapper

**What:** RAII class wrapping `socket()`/`connect()`/`send()`/`recv()`/
`closesocket()`, plus a server-side path: `bind()`/`listen()`/`accept()`.
Same primitive serves both the outbound Twitch client and the inbound
localhost server — don't build two.

**Files:** `net/socket.cpp/.h`

**Done when:**
- Client test: connect to `example.com:80`, send a raw `GET / HTTP/1.1`,
  print whatever bytes come back.
- Server test: bind `127.0.0.1:61616`, accept one connection, echo bytes.

---

## Stage 2 — Schannel TLS (outbound only) — the hard stage

**What:** wrap a connected socket in TLS 1.2 using SSPI:
1. `AcquireCredentialsHandle` once (a shared credential handle, reused
   across connections — don't reacquire per-request).
2. Handshake loop: `InitializeSecurityContext`, feeding bytes read from the
   socket, looping on `SEC_I_CONTINUE_NEEDED` and `SEC_E_INCOMPLETE_MESSAGE`
   until you get `SEC_E_OK`.
3. Certificate validation: `QueryContextAttributes` for the remote cert,
   then `CertGetCertificateChain` + `CertVerifyCertificateChainPolicy`
   (`CERT_CHAIN_POLICY_SSL`) with the server name set to the host you
   connected to. **Do not skip this or accept-all** — this connection
   carries the user's real Twitch session cookie.
4. Post-handshake I/O: `EncryptMessage`/`DecryptMessage` wrapping your
   `send`/`recv`, handling `SEC_E_INCOMPLETE_MESSAGE` (need more bytes) and
   partial/leftover buffers between calls (Schannel routinely hands back
   more plaintext than you asked for, or says "extra" data remains).
5. Clean shutdown: send a `close_notify` alert (`InitializeSecurityContext`
   with `SCHANNEL_SHUTDOWN`) before closing the socket — Twitch's servers
   don't care, but it's correct behavior and cheap to get right now.

**Files:** `net/tls_schannel.cpp/.h`

**Link:** `secur32.lib`, `crypt32.lib`

**Done when:** connect to `www.twitch.tv:443`, complete the handshake,
manually write a raw HTTP/1.1 GET request through `EncryptMessage`, decrypt
the response, and print the HTML to stdout. This is the single riskiest
stage in the whole project — budget the most time here. If you get stuck,
the MSDN "Creating a Secure Connection Using Schannel" sample walks the
exact same state machine.

---

## Stage 3a — Minimal HTTP/1.1 client

**What:** on top of Stage 2, a function that takes {host, path, method,
headers, body} and returns {status, headers, body}. You need:
- Request serialization (request line + headers + optional body +
  `Content-Length`).
- Response parsing: status line, headers (case-insensitive lookup, and
  **must** collect *all* `Set-Cookie` headers, there can be several),
  body via `Content-Length` (Twitch's relevant responses aren't chunked,
  so you can skip chunked-transfer-encoding support for v1).

**Replaces:** `ionhttp` usage patterns visible in `Twitch.java`,
`TwitchGQL.java`, `TwitchM3U8.java`, `TwitchClientIdProvider.java`,
`DefaultInstanceSupplier.java`.

**Files:** `http/http_client.cpp/.h`

**Done when:** a test program does a GET to `https://www.twitch.tv/somechannel`
and a POST to `https://gql.twitch.tv/gql` with a hardcoded JSON body,
printing status + headers + body for both.

---

## Stage 3b — Minimal HTTP/1.1 server

**What:** on top of Stage 1 (no TLS), an accept loop that parses an incoming
request line (`METHOD /path HTTP/1.1`), headers, and dispatches to a
handler by path. Response writer that sets the CORS headers Main.java sets
(`Access-Control-Allow-Origin: *`, etc.) plus a JSON/text body.

**Replaces:** the Javalin usage in `Main.java`.

**Files:** `http/http_server.cpp/.h`

**Done when:** `curl http://127.0.0.1:61616/ping` (or a browser fetch)
returns a hardcoded response with the right CORS headers. One thread per
connection is fine at this scale — no need for IOCP.

---

## Stage 4a — JSON value type

**What:** a `JsonValue` variant (object/array/string/number/bool/null) with
a recursive-descent parser and a serializer. This is the module most worth
treating as a real "build it properly" exercise — it's small, self-contained,
and easy to unit test in isolation.

**Needed for:**
- Serializing the GQL request body (`{operationName, query, variables:{...}}`,
  see `TwitchTokenGQL.java`).
- Parsing the GQL response and pulling out
  `data.streamPlaybackAccessToken.{signature,value}` — note `value` is
  itself a JSON string that needs a **second** parse to get `.channel`
  out of it (see `TwitchPlaybackToken.java` line 16). Don't miss that
  double-parse, it's an easy thing to gloss over when porting.
- The `{live, playlist}` response the local server sends back to the
  extension (see `Instance.java`'s `handler` field).

**Files:** `json/json.cpp/.h`

**Done when:** round-trip tests — parse a captured real GQL response,
extract signature/value/channel correctly; serialize a request body and
diff it byte-for-byte against what the Java client would send.

---

## Stage 4b — Three small scanners (no regex engine needed)

**What:** hand-written substring extraction, not a general regex engine —
these are the only three patterns the original code actually uses:

| Java pattern | Purpose | Replaces |
|---|---|---|
| `query='(.*?)'` | pull the GQL query string out of the page's inline script | `TwitchConfiguration.java:11` |
| `.*? (.*?)\(` | pull the operation name (the word right before `(`) out of the query string | `TwitchConfiguration.java:10` |
| `clientId="(.*?)"` | pull the client ID out of the page HTML | `TwitchClientIdProvider.java:14` |

Each is "find literal marker, read until next delimiter" — a `find` +
substring is simpler and faster than building an NFA for three fixed
patterns.

**Files:** fold into `twitch/config.cpp` and `twitch/client_id.cpp`
directly, no shared module needed.

**Done when:** run against a saved copy of `twitch.tv`'s HTML and confirm
the extracted query/operationName/clientId match what the Java version logs.

---

## Stage 4c — HLS attribute-list parser

**What:** parse `KEY=VALUE,KEY2="quoted, value",KEY3=val` (the tag body
after `#EXT-X-STREAM-INF:` or `#EXT-X-MEDIA:`) into a key→value map,
stripping surrounding quotes.

**Replaces:** `M3U8.java` (the regex there is
`([a-zA-Z0-9\-]+)=((\"[^\"]*\")|([^,]*))(,|$)`) — a small hand-written
state machine (scan key, expect `=`, if next char is `"` scan to closing
`"` else scan to next `,`) covers this exactly, including the "quoted value
may itself contain commas" case the regex handles.

**Files:** `hls/attr_list.cpp/.h`

**Done when:** unit test against a real `#EXT-X-STREAM-INF:BANDWIDTH=...,
RESOLUTION="1920x1080",...` line, confirm all keys extracted correctly.

---

## Stage 4d — Master playlist parser

**What:** walk a master playlist line by line; on `#EXT-X-MEDIA` and
`#EXT-X-STREAM-INF` parse via 4c and stash as "pending"; on a non-`#` line
(the URI), attach it to the pending entry and push it to a list.

**Replaces:** `EXTM3U.java` + `PlaylistM3U8.java`.

**Files:** `hls/master_playlist.cpp/.h`

**Done when:** parse a captured `usher.ttvnw.net` master playlist response
and confirm you get the same list of {media attrs, stream attrs, URI}
entries the Java `EXTM3U` would, in order.

---

## Stage 4e — Cookie jar

**What:** parse `Set-Cookie` response headers (name, value, and enough of
the attributes to know the cookie's domain — you don't need expiry/path
enforcement for this use case, just domain bucketing), store per-domain,
and serialize back into a single `Cookie: a=b; c=d` request header. Also a
helper to pull one named cookie's value out of a raw cookie string by
splitting on `;` then `=`.

**Replaces:** `ionhttp`'s `DefaultCookieManager` as used in
`TwitchStream.java` (creates it) and `DefaultInstanceSupplier.java`
(reads `unique_id`/`unique_id_durable` back out of it).

**Files:** fold into `net/tls_schannel.cpp`'s caller or a small
`http/cookie_jar.cpp/.h` — whichever keeps `http_client` from needing to
know about Twitch-specific cookie names. A generic jar + a Twitch-side
helper that reads `unique_id`/`unique_id_durable` out of it is cleaner
than baking cookie names into the jar itself.

**Done when:** after a GET to `twitch.tv/{channel}`, correctly extract
`unique_id` (or fall back to `unique_id_durable`) matching
`DefaultInstanceSupplier.java`'s logic exactly, including the fallback.

---

## Stage 5 — Twitch domain logic (wires 3a + 4a–4e together)

Build and test these in order — each depends on the previous succeeding:

1. **Config fetch** (`twitch/config.cpp`) — GET the channel page, run the
   two Stage 4b scanners, produce {operationName, query}.
   Replaces `Twitch.java` + `TwitchConfiguration.java`.
2. **Client ID provider** (`twitch/client_id.cpp`) — GET `twitch.tv/?`,
   scan for `clientId`, cache with a 1-hour TTL (store a timestamp,
   `GetTickCount64()` or `std::chrono::steady_clock` for the check).
   Replaces `TwitchClientIdProvider.java`.
3. **Instance bootstrap** (`twitch/instance.cpp`) — GET the channel page
   (can reuse the response from step 1 if you restructure slightly — the
   Java version does two separate requests, matching that is fine for a
   first pass), pull `unique_id` via 4e.
   Replaces `DefaultInstanceSupplier.java` + `TwitchInstance.java`.
4. **GQL playback token** (`twitch/gql.cpp`) — build the JSON body via 4a
   (operationName, query, variables per `TwitchTokenGQL.java`), POST to
   `gql.twitch.tv/gql` with `Cookie` + `Client-ID` headers, parse response,
   remember the double-parse for `.channel` (see Stage 4a note).
   Replaces `TwitchGQL.java` + `TwitchTokenGQL.java` +
   `TwitchPlaybackToken.java`.
5. **Usher request** (`twitch/usher.cpp`) — GET
   `usher.ttvnw.net/api/channel/hls/{channel}.m3u8?allow_source=true&sig=...
   &token=...`, parse via 4d, pick the entry with the highest `BANDWIDTH`
   value in its stream attrs (numeric compare, watch out — it's a string
   in the map, needs `strtoll`/`stoll`).
   Replaces `TwitchM3U8.java` + the comparator in `Instance.fetch()`.
6. **Live playlist poller** (`twitch/poller.cpp`) — every 2 seconds, GET
   the chosen rendition URL. Validate first line is `#EXTM3U` (else it's a
   malformed response, treat as an error). Copy the first 4 lines verbatim
   (the playlist header). Then scan for lines starting with
   `#EXT-X-PROGRAM-DATE-TIME` and, for each, append that line plus the
   *next two* lines (the `#EXTINF` tag and the segment URI) to the output
   — **this three-line copy is the entire ad-stripping mechanism**: ad
   segments don't carry a program-date-time tag, so they're silently
   dropped by never being reached. Store the rebuilt text. On HTTP 404,
   signal "stream offline." Auto-stop if nobody's asked for the playlist
   in 60 seconds (track `lastPlaylistRequest`, compare each tick).
   Replaces `Instance.java`'s `execute()`/`fetch()`.

**Done when:** a console test program takes a channel name as argv[1] and
prints a working, ad-stripped playlist URL (or "offline") end to end —
this is the moment the whole Twitch side is proven to work.

---

## Stage 6 — Session registry

**What:** `std::unordered_map<std::string, std::shared_ptr<Instance>>`
behind a `std::mutex`. Starting an instance spawns a `std::thread` running
the 2-second poll loop from Stage 5.6; the instance removes itself from the
map (via a callback, same as `InstanceCallback.java`) when it shuts down.

**Replaces:** `Main.java`'s `instances` map + launch logic, and
`Instance.java`'s `ScheduledExecutorService` (a plain sleeping thread loop
is sufficient here — you don't need a real thread pool for ~1 thread per
watched channel).

**Files:** `session/registry.cpp/.h`

**Done when:** requesting the same channel twice reuses the existing
instance; requesting a second channel spins up a second thread; an idle
instance cleans itself out of the map without leaking the thread.

---

## Stage 7 — Route handlers

**What:** wire Stage 3b's server to Stage 6:
- `OPTIONS *` → 200, CORS headers only.
- `GET /live/{username}` → lowercase the username, launch an instance if
  missing, then poll (sleep ~20ms, up to ~3s) for the first playlist to
  populate before responding `{"live": bool, "playlist": "http://127.0.0.1:
  61616/live/{user}/playlist.m3u8"}` via Stage 4a.
- `GET /live/{username}/playlist.m3u8` → return the instance's current
  playlist text as-is (content type doesn't matter much, extension just
  reads the body).

**Replaces:** `Main.java`'s route table + `Instance.java`'s `handler`.

**Files:** `server/routes.cpp/.h`

**Done when:** point the *existing TypeScript extension* at your C++
server (it already just does `fetch('http://localhost:61616/...')`, see
`extension/utils/api.ts`) and confirm it plays a real Twitch stream through
your server with no Java involved.

---

## Stage 8 — Tray icon

**What:** a hidden `HWND` (message-only window is fine, or a normal hidden
one), `Shell_NotifyIcon(NIM_ADD, ...)`, handle the tray's callback message
for a right-click, show a `TrackPopupMenu` with one "Exit" item that calls
`PostQuitMessage(0)`.

**Replaces:** `Tray.java`.

**Files:** `tray/tray.cpp/.h`

**Done when:** icon appears in the system tray, right-click → Exit cleanly
shuts down the server thread and the process.

---

## Stage 9 — main.cpp + packaging

**What:** wire startup order — `WSAStartup` → `AcquireCredentialsHandle`
(Stage 2's shared handle) → start Stage 7's server on a background thread
→ run the tray's message loop on the main thread → on exit, join the
server thread, `WSACleanup`.

Compile as a single static-linked Release `.exe`. This is the point where
`rust-wrapper/` becomes fully redundant — there's no JVM to bundle, no
temp-dir extraction, just a real native binary you can hand someone
directly.

**Done when:** a clean checkout builds one `.exe` that, run standalone,
does everything `local-server.jar` + `rust-wrapper.exe` did together.

---

## Notes on things it's fine to *not* build from scratch

- `std::thread`, `std::mutex`, `std::chrono` — these are standard library,
  not third-party dependencies. Using them isn't in tension with the
  "from scratch" goal; the goal is no external libraries (OpenSSL, curl,
  json libs), not no standard library.
- IOCP / async I/O — out of scope. This server handles at most a handful
  of concurrent connections (one browser, a few channels); a thread per
  connection is correct here and matches the Java version's model.
- Chunked transfer-encoding, HTTP/1.1 keep-alive, redirects — none of the
  specific Twitch endpoints this project talks to need them for a working
  v1. Add later only if you hit a response that actually requires it.
