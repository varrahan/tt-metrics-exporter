# HTTP alerts

## Rejections

Check connection saturation, scrape concurrency, queue depth, and request
deadlines. Only monitoring traffic should reach port 9400. Do not increase
limits before excluding unauthorized or malformed traffic.

## Failures

Group failures by bounded route and status class, then test `/healthz`,
`/readyz`, `/metrics`, and `/v1/devices` from an allowed pod. Inspect exporter
logs and roll back when failures correlate with the Python rollout.
