# Open Speech Repository fixtures

Source: **Open Speech Repository**, Telchemy / VoIP Troubleshooter.
https://www.voiptroubleshooter.com/open_speech/american.html

Original mono PCM16 8 kHz English Harvard sentence recordings:

| File | Upstream description | SHA256 |
| --- | --- | --- |
| OSR_us_000_0010_8k.wav | Female | a4bf9becd046d7aedb6d05b6e12347a6294a44f74d263089c636fb0a2b1e6561 |
| OSR_us_000_0030_8k.wav | Male | d3e1cfba98a44ddfc6e99508e663794c8dc41bdb2baced224f779c453143b622 |

Downloaded on 2026-11-20 from the `american/` directory under that URL. Files
are unchanged. These are recorded dry-source components for **simulated** echo
scenarios, not physical echo recordings or guaranteed noise-free studio speech.
They were introduced as evaluation holdouts after the 20 Hz integration policy
was fixed using the earlier Microsoft fixture and an analytic tone model.

The upstream conditions permit copying, downloading, broadcasting, modification
and incorporation into test equipment, and require source identification as
"Open Speech Repository":
https://www.voiptroubleshooter.com/open_speech/

These audio fixtures retain those conditions and are not relicensed under
Baffle's MIT license. No Hungarian recording is included.
