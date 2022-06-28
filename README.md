# ZCAC
ZCAC (Zealan's Compressed Audio Codec) is a custom audio compression algorithm/format I'm making for fun :D

It currently is in very early development/testing, and only compresses .WAV files.

Current compression results are about 5-10% of the original file size, with almost no noticable difference in audio quality.

**How ZCAC works (for now):**
- Load a .WAV file and determine the raw signal data for each audio channel
- Convert the raw signal data into Fourier-transformed segments (blocks)
- Reduce the size of the Fourier blocks by converting each floating point value to a small integer
- Compress and store the results

# Compression Examples
**Violin Solo Excerpt:** 

- [Original Audio (9,031KB)](audio_examples/violin_solo/violin_solo_original.wav?raw=true)
- [Encoded ZCAC file (646KB)](audio_examples/violin_solo/violin_solo_encoded.zcac?raw=true)
  - *Compression Ratio: **7.15%***
- [Decoded Audio from ZCAC (9,031KB)](audio_examples/violin_solo/violin_solo_decoded.wav?raw=true)
