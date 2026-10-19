/* ws_skip.c - oversized-frame drain arithmetic, split out of ws.c.
 *
 * This is the only part of ws.c that can be tested on the host: it has no
 * socket, no tls_ctx_t and no PS4 headers. The rest of ws_recv_frame() is
 * wired to those, so a bug here used to be invisible until a real gateway
 * handshake on a real console.
 *
 * Why it is worth isolating: drain state lives in the ws_t, so a wrong
 * value here does not fail loudly. On console 2026-10-06 an 11.7 MB READY
 * overflowed the buffer, the drain miscounted, and the next parse read JSON
 * payload as a frame header (op=2 fin=0 plen=125 b1=0x22 -- a quote byte),
 * which surfaced only as "cannot connect to discord".
 *
 * Included by ws.c; compiled directly by tests/test_utils.c. Both paths get
 * the same code, so the tests cannot drift from the payload.
 */
#include "ws.h"
#include <stdio.h>

int ws_build_handshake(char *out, size_t cap, const char *host, const char *resource,
                       const char *key, const char *origin){
    char origin_line[128];
    int n;
    if(!out || cap == 0) return 0;
    if(origin && *origin)
        snprintf(origin_line, sizeof origin_line, "Origin: %s\r\n", origin);
    else
        origin_line[0] = 0;
    n = snprintf(out, cap,
        "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "%s"
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) discord/1.0.9175 Chrome/122.0.6261.112 Electron/30.0.8 "
        "Safari/537.36\r\n"
        "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",
        resource?resource:"/", host, origin_line, key);
    if(n <= 0 || (size_t)n >= cap) return 0;
    return n;
}

uint64_t ws_skip_plan(uint64_t plen, size_t payload_here, uint64_t *skip_left_out){
    if(!skip_left_out) return 0;
    /* payload_here is the number of payload bytes ALREADY REMOVED from rbuf
     * and therefore no longer pending. It must NOT be the number sitting in
     * the buffer: the drain loop consumes those too, so counting them here
     * as well double-counts and the drain terminates early with the stream
     * left mid-frame (console 2026-10-09, op=10 b1=0x3a b2=0x74).
     *
     * ws_recv_frame() removes no payload before arming the drain, so it
     * passes 0. The saturation branch is kept so a caller that does remove
     * bytes cannot underflow. */
    *skip_left_out = ((uint64_t)payload_here >= plen)
                     ? 0 : plen - (uint64_t)payload_here;
    return *skip_left_out;
}