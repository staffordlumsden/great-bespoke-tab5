# Ollama access

TAB5 Ollama Analytics connects directly over Internet Wi-Fi or a hotspot. It
uses the station saved by Console and an Ollama key from its private SD profile.

Read-only HTTPS endpoints:

- `https://ollama.com/api/balance`
- `https://ollama.com/api/usage?range=24h&scope=self&group_by=model`

The grouped response can omit models: at most ten groups are selected,
normally by spend; legacy requests can change the ordering. Sort the returned
groups by request count. Claim global top five only if there is no hidden
remainder or its aggregate count cannot exceed the fifth visible model.
Otherwise label the restricted ranking. Grey Other covers unshown requests.

Credit plans report included and purchased balances. Legacy plans report
session and weekly limits. The session response omits its duration, so the
5H meter remains unavailable; the reported weekly limit fills 7D. Reset times
are displayed when reported. Request share is separate from quota.

Poll each endpoint once per minute or slower, honour Retry-After, and verify
TLS certificates/hostnames. Keys and live account payloads remain private.
Sources: [Cloud usage](https://docs.ollama.com/api/cloud-usage),
[Balance](https://docs.ollama.com/api/balance).
