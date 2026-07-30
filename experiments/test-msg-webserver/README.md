# Cellular message web server

A Deno service for setting the message shown on the project's Waveshare 2.13-inch V4 e-paper
display. The web preview renders the actual 122 × 250 one-bit framebuffer using the shared
`phone-5x7-v1` bitmap font, word-wrapping, and centering contract.

Message delivery and telemetry use retained MQTT over TLS through the public HiveMQ test broker. The
device keeps its MQTT session alive for 120 seconds and falls back to the HTTPS API every 15
minutes.

## Run locally

```sh
cd experiments/test-msg-webserver
deno task dev
```

Open <http://localhost:8000>. Set `PORT` to listen on a different port.

## Message delivery

The browser updates `PUT /api/message`. The server saves the message and publishes the complete
message state as a retained QoS 1 MQTT message. The device subscribes to it and normally receives
updates immediately.

`GET /api/message` remains the device's 15-minute fallback:

```json
{
  "message": "cellular link ready.",
  "revision": 1,
  "updatedAt": "1970-01-01T00:00:00.000Z",
  "delivery": {
    "primary": "mqtt",
    "connected": true,
    "keepAliveSeconds": 120
  },
  "httpFallbackAfterSeconds": 900,
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

## Device telemetry

The phone normally publishes its non-sensitive connection state over MQTT every 30 seconds. The HTTP
endpoint remains available to the fallback path and for diagnostics:

```sh
curl -X POST http://localhost:8000/api/device/status \
  -H 'content-type: application/json' \
  -d '{
    "deviceId":"phone-01",
    "firmwareVersion":"1.0.0",
    "uptimeSeconds":120,
    "signalRssiDbm":-81,
    "signalPercent":63,
    "operator":"Example Wireless",
    "networkType":"LTE",
    "ipAddress":"10.0.0.2",
    "lastMessageRevision":2,
    "displayUpdated":true,
    "lastPollOk":true,
    "lastError":""
  }'
```

`GET /api/device/status` returns the latest report with server-calculated presence and age. The
server deliberately does not collect IMEI, ICCID, IMSI, phone number, or location.

The broker namespace contains a random project identifier to prevent accidental topic collisions.
The public test broker does not provide authentication or delivery guarantees suitable for
production; move the same topics to an authenticated private broker before using this beyond the
prototype.

## Deploy behind HTTPS

Build from this directory with the included `Dockerfile`. Route HTTPS traffic from the reverse proxy
to container port `8000`, and mount persistent storage at `/app/data` so the current message
survives replacement of the container. The Deno server itself speaks HTTP, as expected behind the
TLS-terminating proxy.
