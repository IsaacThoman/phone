# Cellular message web server

A dependency-free Deno service for setting the message shown on the project's Waveshare 2.13-inch V4
e-paper display. The web preview renders the actual 122 × 250 one-bit framebuffer using the shared
`phone-5x7-v1` bitmap font, word-wrapping, and centering contract.

## Run locally

```sh
cd experiments/test-msg-webserver
deno task dev
```

Open <http://localhost:8000>. Set `PORT` to listen on a different port.

## HTTP API

The ESP32 should make a `GET /api/message` request every 30 seconds:

```json
{
  "message": "cellular link ready.",
  "revision": 1,
  "updatedAt": "1970-01-01T00:00:00.000Z",
  "pollAfterSeconds": 30,
  "display": {
    "width": 122,
    "height": 250,
    "font": "phone-5x7-v1",
    "glyphWidth": 5,
    "glyphHeight": 7,
    "characterAdvance": 6,
    "lineAdvance": 13,
    "horizontalAlign": "center",
    "verticalAlign": "center"
  }
}
```

The response includes an `ETag`. A device can send it back in `If-None-Match`; the server returns
`304 Not Modified` when the display does not need refreshing.

Set a message with:

```sh
curl -X PUT http://localhost:8000/api/message \
  -H 'content-type: application/json' \
  -d '{"message":"hello over cellular."}'
```

Messages are limited to 300 printable ASCII characters plus newlines. The browser and API apply the
same rendering rules.

`GET /healthz` returns `{"ok":true}` for deployment health checks.

## Deploy behind HTTPS

Build from this directory with the included `Dockerfile`. Route HTTPS traffic from the reverse proxy
to container port `8000`, and mount persistent storage at `/app/data` so the current message
survives replacement of the container. The Deno server itself speaks HTTP, as expected behind the
TLS-terminating proxy.
