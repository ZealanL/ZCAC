# ZCAC
ZCAC (Zealan's Compressed Audio Codec) is a custom audio compression algorithm/format I'm making for fun :D

It currently is in very early development/testing, and only compresses .WAV files.

Current compression results are about 5-15% of the original file size, with almost no noticable difference in audio quality.

**How ZCAC works (for now):**
- Load a .WAV file and determine the raw signal data for each audio channel
- Convert the raw signal data into Fourier-transformed segments (blocks)
- Reduce the size of the Fourier blocks by converting each floating point value to a small integer
- Encode the results and compress everything with ZLIB

**Planned future features:**
- Adjustable quality/compression settings
- Adaptive bitrate encoding for individual FFT frequency buckets
- Custom Huffman tree compression for FFT data

# Compression Examples
**Violin Solo Excerpt:** 
- [Original Audio (9,031KB)](audio_examples/violin_solo/violin_solo_original.wav?raw=true)
- [Encoded ZCAC file (646KB)](audio_examples/violin_solo/violin_solo_encoded.zcac?raw=true)
  - *Compression Ratio: **7.15%***
- [Decoded Audio from ZCAC (9,031KB)](audio_examples/violin_solo/violin_solo_decoded.wav?raw=true)

**Single Piano Note:**
- [Original Audio (682KB)](audio_examples/piano_note/piano_note_original.wav?raw=true)
- [Encoded ZCAC file (23KB)](audio_examples/piano_note/piano_note_encoded.zcac?raw=true)
  - *Compression Ratio: **3.37%***
- [Decoded Audio from ZCAC (682KB)](audio_examples/piano_note/piano_note_decoded.wav?raw=true)

**White Noise:** (WORST-CASE SCENARIO!) 
- [Original Audio (517KB)](audio_examples/white_noise/white_noise_original.wav?raw=true)
- [Encoded ZCAC file (124KB)](audio_examples/white_noise/white_noise_encoded.zcac?raw=true)
  - *Compression Ratio: **23.9%***
- [Decoded Audio from ZCAC (517KB)](audio_examples/white_noise/white_noise_decoded.wav?raw=true)

# Libraries Used
- ZLIB for end-result compression (https://zlib.net/)
