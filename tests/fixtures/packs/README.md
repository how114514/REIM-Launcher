# M3 resource/data pack fixtures (stage 4, 4A-I)

These are synthetic packs, not Mojang assets. `res_ok` overrides one language
key, the M3 sound id 1, and `block/stone.png` (16×16). `data_ok` overrides one
recipe and one item tag. `res_missing` references a texture that is not in the
block atlas and a model, so the Runtime must warn and continue. `res_malicious.zip`
contains a `../evil.txt` entry and must be rejected.

Directory fixture digests (sorted relative path, NUL, file hash, LF):

```text
res_ok          72ab7d135d3e325b4169ff327bc915375330309438b782940c5c13802a567309
data_ok         0c0d656b122800376acafc9443d4fa6dddf345aaffe34b3b7a858f26429918e4
res_missing     2261b41c3d9bcb42c85217a09b76a564016d5a892ee6138ec8c68fbcdef7efc4
res_malicious.zip (file SHA-256)
                1a880f865216f1ffe334c6f97b00781b45b13aada8c22a1ad553f2150a20f518
```

Regenerate from this directory's content with the M3 scenario suite; packs are
startup-only and never hot-reloaded.
